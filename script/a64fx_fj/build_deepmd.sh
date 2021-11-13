#!/bin/bash
#PJM -L  "node=1"                           # Number of assign node 8 (1 dimention format)
#PJM -L  "freq=2200"                         
#PJM -L  "rscgrp=int"                     # Specify resource group
#PJM -L  "elapse=00:10:00"                  # Elapsed time limit 1 hour
#PJM --mpi "max-proc-per-node=1"            # Maximum number of MPI processes created per node
export PLE_MPI_STD_EMPTYFILE=off

if [ -z $deepmd_root ]
then
    echo "not found envoriment variable : deepmd_root"
fi

source $deepmd_root/script/a64fx_fj/env.sh

set -ex

cd $deepmd_root
# rm -rf build
mkdir -p build
cd build

cmake   -DTENSORFLOW_ROOT=$tensorflow_root      \
        -DCMAKE_INSTALL_PREFIX=$deepmd_root     \
        -DLAMMPS_SOURCE_ROOT=$lammps_root       \
        -DBUILD_FUGAKU=ON                       \
        ../source

make VERBOSE=1 -j48
make install