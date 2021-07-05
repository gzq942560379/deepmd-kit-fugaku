#!/bin/bash -e
#PJM -L  "node=1"                          # Number of assign node 8 (1 dimention format)
#PJM -L  "freq=2200"                         
#PJM -L "rscgrp=small"         # Specify resource group
#PJM -L  "elapse=00:05:00"                 # Elapsed time limit 1 hour
#PJM --mpi "shape=1"
#PJM --mpi "max-proc-per-node=48"          # Maximum number of MPI processes created per node
#PJM -s                                    # Statistical information output

deepmd_root=$HOME/gzq/deepmd-kit
source $deepmd_root/script/fugaku/env.sh
bash $deepmd_root/script/fugaku/build_deepmd.sh

export PLE_MPI_STD_EMPTYFILE=off
# export PRINT_TIME=1
export OMP_NUM_THREADS=1
export TF_INTRA_OP_PARALLELISM_THREADS=1
export TF_INTER_OP_PARALLELISM_THREADS=1
export TF_CPP_MIN_LOG_LEVEL=3
export HAVE_PREPROCESSED=1

set -x

name=baseline
ln -sf ../model/graph_$name.pb ../model/graph.pb 
ln -sf ../model/graph-compress_$name.pb ../model/graph-compress.pb
ln -sf ../model/graph-compress-preprocess_$name.pb ../model/graph-compress-preprocess.pb
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_1  >  out.$name
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_compress_1  > out.compress-$name
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_compress_preprocess_1 > out.compress-preprocess-$name

name=gemm
ln -sf ../model/graph_$name.pb ../model/graph.pb 
ln -sf ../model/graph-compress_$name.pb ../model/graph-compress.pb
ln -sf ../model/graph-compress-preprocess_$name.pb ../model/graph-compress-preprocess.pb
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_1  >  out.$name
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_compress_1  > out.compress-$name
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_compress_preprocess_1 > out.compress-preprocess-$name

name=gemm_tanh
ln -sf ../model/graph_$name.pb ../model/graph.pb 
ln -sf ../model/graph-compress_$name.pb ../model/graph-compress.pb
ln -sf ../model/graph-compress-preprocess_$name.pb ../model/graph-compress-preprocess.pb
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_1  >  out.$name
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_compress_1  > out.compress-$name
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_compress_preprocess_1 > out.compress-preprocess-$name

name=gemm_tanh_fusion
ln -sf ../model/graph_$name.pb ../model/graph.pb 
ln -sf ../model/graph-compress_$name.pb ../model/graph-compress.pb
ln -sf ../model/graph-compress-preprocess_$name.pb ../model/graph-compress-preprocess.pb
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_1  >  out.$name
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_compress_1  > out.compress-$name
likwid-pin -c 0 lmp_serial -echo screen -in ./in.water_compress_preprocess_1 > out.compress-preprocess-$name
