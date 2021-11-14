#!/bin/bash -e
#PJM -L  "node=1"                           # Number of assign node 8 (1 dimention format)
#PJM -L  "freq=2200"                         
#PJM -L  "rscgrp=int"                     # Specify resource group
#PJM -L  "elapse=00:10:00"                  # Elapsed time limit 1 hour
#PJM --mpi "max-proc-per-node=1"            # Maximum number of MPI processes created per node
export PLE_MPI_STD_EMPTYFILE=off


source $deepmd_root/script/a64fx_fj/env.sh

export OMPI_MCA_plm_ple_memory_allocation_policy=bind_local
export PLE_MPI_STD_EMPTYFILE=off
export OMP_NUM_THREADS=1
export TF_INTER_OP_PARALLELISM_THREADS=-1
export TF_INTRA_OP_PARALLELISM_THREADS=1
export TF_CPP_MIN_LOG_LEVEL=3

export DEEPMD_NUM_THREADS=1
mpiexec -n 1 lmp -echo screen -in ../lmp/in.plugin.lammps_compress_1
