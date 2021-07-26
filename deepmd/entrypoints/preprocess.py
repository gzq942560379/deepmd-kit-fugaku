"""Module used for transfering parameters between models."""

from typing import Dict, Optional, Sequence, Tuple
from deepmd.env import tf
import re
import numpy as np
import logging
import sys

__all__ = ["transfer"]

log = logging.getLogger(__name__)

PRECISION_MAPPING: Dict[int, type] = {
    1: np.float32,
    2: np.float64,
    19: np.float16,
}

def load_graph(graph_name: str) -> tf.Graph:
    """Load graph from passed in path.

    Parameters
    ----------
    graph_name : str
        path to frozen graph on disk

    Returns
    -------
    tf.Graph
        tf graph object
    """
    graph_def = tf.GraphDef()
    with open(graph_name, "rb") as f:
        graph_def.ParseFromString(f.read())
    with tf.Graph().as_default() as graph:
        tf.import_graph_def(graph_def, name="")
        return graph



def preprocess(old_model: str, output: str):
    """Transfer operation from old fron graph to new prepared raw graph.

    Parameters
    ----------
    old_model : str
        frozen old graph model
    output : str
        new model with transfered parameters will be saved to this location
    """
    old_graph = load_graph(old_model)
    print(f"{len(old_graph.as_graph_def().node)} ops in the old graph")

    new_graph_def = preprocess_graph(old_graph)

    with tf.gfile.GFile(output, mode="wb") as f:
        f.write(new_graph_def.SerializeToString())

    print("the output model is saved in " + output)


def preprocess_tabulate_table_transpose(node):
    tabulate_table_node_pattern = re.compile(r"filter_type_\d/TabulateFusion(_\d)?/table")
    if tabulate_table_node_pattern.fullmatch(node.name) is None:
        return
    print(f"preprocess_tabulate_table match node : {node.name}")
    print("precessing tabulate table")
    tensor_proto = node.attr["value"].tensor
    node_type = PRECISION_MAPPING[tensor_proto.dtype]
    array = np.frombuffer(tensor_proto.tensor_content,dtype=node_type)
    array = array.reshape([-1,128,6]).transpose((0,2,1))
    node.attr["value"].tensor.tensor_content = array.tostring()

def preprocess_tabulate_table_packing(node):
    tabulate_table_node_pattern = re.compile(r"filter_type_\d/TabulateFusion(_\d)?/table")
    if tabulate_table_node_pattern.fullmatch(node.name) is None:
        return
    print(f"preprocess_tabulate_table match node : {node.name}")
    print("precessing tabulate table")
    tensor_proto = node.attr["value"].tensor
    node_type = PRECISION_MAPPING[tensor_proto.dtype]
    if node_type == np.float64:
        array = np.frombuffer(tensor_proto.tensor_content,dtype=np.float64)
        array = array.reshape([-1,8,16,6]).transpose((0,1,3,2))
        node.attr["value"].tensor.tensor_content = array.tostring() 
    elif node_type == np.float32:
        array = np.frombuffer(tensor_proto.tensor_content,dtype=np.float32)
        array = array.reshape([-1,4,32,6]).transpose((0,1,3,2))
        node.attr["value"].tensor.tensor_content = array.tostring() 
    else :
        print("Not support type !!!")
        assert False

def preprocess_std_reciprocal(node):
    tabulate_table_node_pattern = re.compile(r"descrpt_attr/t_std")
    if tabulate_table_node_pattern.fullmatch(node.name) is None:
        return
    print(f"preprocess_std_reciprocal match node : {node.name}")
    print("precessing std")

    tensor_proto = node.attr["value"].tensor
    node_type = PRECISION_MAPPING[tensor_proto.dtype]
    array = np.frombuffer(tensor_proto.tensor_content,dtype=node_type)
    array = 1. / array
    
    node.attr["value"].tensor.tensor_content = array.tostring()

def preprocess_graph(old_graph):
    old_graph_def = old_graph.as_graph_def()

    for node in old_graph_def.node:
        # print(f"node name : {node.name} in the old graph")
        preprocess_tabulate_table_packing(node)
        preprocess_std_reciprocal(node)
    return old_graph_def
        


if __name__ == "__main__" :
    if len(sys.argv) != 3:
        print(f"{sys.argv[0]} old_model output")
    model_path = sys.argv[1]
    output_path = sys.argv[2]
    print(f"model path : {model_path}")
    print(f"output path : {output_path}")
    preprocess(model_path, output_path)