#!/usr/bin/env python3
"""This script freezes TF trained graph so it can be used with LAMMPS ain i-PI.

References
----------
https://blog.metaflow.fr/tensorflow-how-to-freeze-a-model-and-serve-it-with-a-python-api-d4f3596b3adc
"""

from deepmd.env import tf
from deepmd.env import op_module
from os.path import abspath

# load grad of force module
import deepmd._prod_force_grad
import deepmd._prod_virial_grad
import deepmd._prod_force_se_a_grad
import deepmd._prod_virial_se_a_grad
import deepmd._prod_force_se_r_grad
import deepmd._prod_virial_se_r_grad
import deepmd._soft_min_force_grad
import deepmd._soft_min_virial_grad

from typing import List, Optional, TYPE_CHECKING


if TYPE_CHECKING:
    try:
        from typing import Protocol  # python >=3.8
    except ImportError:
        from typing_extensions import Protocol  # type: ignore

    class ArgsProto(Protocol):
        """Prococol mimicking parser object."""

        folder: str
        output: str
        nodes: str


def _make_node_names(model_type: str, modifier_type: Optional[str] = None) -> List[str]:
    """Get node names based on model type.

    Parameters
    ----------
    model_type : str
        str type of model
    modifier_type : Optional[str], optional
        modifier type if any, by default None

    Returns
    -------
    List[str]
        list with all node names to freeze

    Raises
    ------
    RuntimeError
        if unknown model type
    """
    if model_type == "ener":
        nodes = [
            "o_energy",
            "o_force",
            "o_virial",
            "o_atom_energy",
            "o_atom_virial",
            "descrpt_attr/rcut",
            "descrpt_attr/ntypes",
            "fitting_attr/dfparam",
            "fitting_attr/daparam",
            "model_attr/tmap",
            "model_attr/model_type",
        ]
    elif model_type == "wfc":
        nodes = [
            "o_wfc",
            "descrpt_attr/rcut",
            "descrpt_attr/ntypes",
            "model_attr/tmap",
            "model_attr/sel_type",
            "model_attr/model_type",
        ]
    elif model_type == "dipole":
        nodes = [
            "o_dipole",
            "o_rmat",
            "o_rmat_deriv",
            "o_nlist",
            "o_rij",
            "descrpt_attr/rcut",
            "descrpt_attr/ntypes",
            "descrpt_attr/sel",
            "descrpt_attr/ndescrpt",
            "model_attr/tmap",
            "model_attr/sel_type",
            "model_attr/model_type",
            "model_attr/output_dim",
        ]
    elif model_type == "polar":
        nodes = [
            "o_polar",
            "descrpt_attr/rcut",
            "descrpt_attr/ntypes",
            "model_attr/tmap",
            "model_attr/sel_type",
            "model_attr/model_type",
        ]
    elif model_type == "global_polar":
        nodes = [
            "o_global_polar",
            "descrpt_attr/rcut",
            "descrpt_attr/ntypes",
            "model_attr/tmap",
            "model_attr/sel_type",
            "model_attr/model_type",
        ]
    else:
        raise RuntimeError("unknow model type " + model_type)
    if modifier_type == "dipole_charge":
        nodes += [
            "modifier_attr/type",
            "modifier_attr/mdl_name",
            "modifier_attr/mdl_charge_map",
            "modifier_attr/sys_charge_map",
            "modifier_attr/ewald_h",
            "modifier_attr/ewald_beta",
            "dipole_charge/descrpt_attr/rcut",
            "dipole_charge/descrpt_attr/ntypes",
            "dipole_charge/model_attr/tmap",
            "dipole_charge/model_attr/model_type",
            "o_dm_force",
            "dipole_charge/model_attr/sel_type",
            "dipole_charge/o_dipole",
            "dipole_charge/model_attr/output_dim",
            "o_dm_virial",
            "o_dm_av",
        ]
    return nodes


def freeze_graph(
    model_folder: str, output: str, output_node_names: Optional[str] = None
):
    """Freeze the graph in supplied folder.

    Parameters
    ----------
    model_folder : str
        location of the folder with model
    output : str
        output file name
    output_node_names : Optional[str], optional
        names of nodes to output, by default None
    """
    # We retrieve our checkpoint fullpath
    checkpoint = tf.train.get_checkpoint_state(model_folder)
    input_checkpoint = checkpoint.model_checkpoint_path

    # expand the output file to full path
    output_graph = abspath(output)

    # Before exporting our graph, we need to precise what is our output node
    # This is how TF decides what part of the Graph he has to keep
    # and what part it can dump
    # NOTE: this variable is plural, because you can have multiple output nodes
    # output_node_names = "energy_test,force_test,virial_test,t_rcut"

    # We clear devices to allow TensorFlow to control
    # on which device it will load operations
    clear_devices = True

    # We import the meta graph and retrieve a Saver
    saver = tf.train.import_meta_graph(
        f"{input_checkpoint}.meta", clear_devices=clear_devices
    )

    # We retrieve the protobuf graph definition
    graph = tf.get_default_graph()
    input_graph_def = graph.as_graph_def()
    nodes = [n.name for n in input_graph_def.node]

    # We start a session and restore the graph weights
    with tf.Session() as sess:
        saver.restore(sess, input_checkpoint)
        model_type = sess.run("model_attr/model_type:0", feed_dict={}).decode("utf-8")
        if "modifier_attr/type" in nodes:
            modifier_type = sess.run("modifier_attr/type:0", feed_dict={}).decode(
                "utf-8"
            )
        else:
            modifier_type = None
        if output_node_names is None:
            output_node_list = _make_node_names(model_type, modifier_type)
        else:
            output_node_list = output_node_names.split(",")
        print(f"The following nodes will be frozen: {output_node_list}")

        # We use a built-in TF helper to export variables to constants
        output_graph_def = tf.graph_util.convert_variables_to_constants(
            sess,  # The session is used to retrieve the weights
            input_graph_def,  # The graph_def is used to retrieve the nodes
            output_node_list,  # The output node names are used to select the usefull nodes
        )

        # Finally we serialize and dump the output graph to the filesystem
        with tf.gfile.GFile(output_graph, "wb") as f:
            f.write(output_graph_def.SerializeToString())
        print(f"{len(output_graph_def.node):d} ops in the final graph.")


def freeze(args: "ArgsProto"):
    """Graph freeze script entry point.

    Parameters
    ----------
    args : ArgsProto
        parser instance
    """
    freeze_graph(args.checkpoint_folder, args.output, args.nodes)
