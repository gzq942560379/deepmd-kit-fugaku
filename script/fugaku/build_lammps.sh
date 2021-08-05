#!/bin/bash -e

deepmd_root=$HOME/gzq/deepmd-kit
source $deepmd_root/script/fugaku/env.sh
bash $deepmd_root/script/fugaku/build_deepmd.sh


cd $DEEPMD_BUILD_DIR
make lammps

mkdir -p $LAMMPS_BUILD_DIR
cd $LAMMPS_BUILD_DIR

LAMMPS_VERSION=stable_29Oct2020
if [ ! -d "lammps-${LAMMPS_VERSION}" ]
then
	curl -L -o lammps.tar.gz https://github.com/lammps/lammps/archive/refs/tags/${LAMMPS_VERSION}.tar.gz
	tar xzf lammps.tar.gz
fi
curl -L -o lammps.patch https://github.com/deepmd-kit-recipes/lammps-dp-feedstock/raw/fdd954a1af4fadabe5c0dd2f3bed260a484175a4/recipe/deepmd.patch
cd ${LAMMPS_BUILD_DIR}/lammps-${LAMMPS_VERSION}
patch -f -p1 < ../lammps.patch || true 
# 
rm -rf ${LAMMPS_BUILD_DIR}/lammps-${LAMMPS_VERSION}/src/USER-DEEPMD
mkdir -p ${LAMMPS_BUILD_DIR}/lammps-${LAMMPS_VERSION}/src/USER-DEEPMD
cp -r ${DEEPMD_BUILD_DIR}/USER-DEEPMD/* ${LAMMPS_BUILD_DIR}/lammps-${LAMMPS_VERSION}/src/USER-DEEPMD


# rm -rf ${LAMMPS_BUILD_DIR}/lammps-${LAMMPS_VERSION}/build
# mkdir -p ${LAMMPS_BUILD_DIR}/lammps-${LAMMPS_VERSION}/build
# cd ${LAMMPS_BUILD_DIR}/lammps-${LAMMPS_VERSION}/build

if [ ${FLOAT_PREC} == "high" ]; then
    export PREC_DEF="-DHIGH_PREC"
fi

# cmake -C ../cmake/presets/all_off.cmake -D PKG_USER-DEEPMD=ON -D PKG_KSPACE=ON -D CMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} -D CMAKE_CXX_FLAGS="${PREC_DEF} -I${INSTALL_PREFIX}/include -L${INSTALL_PREFIX}/lib -I$tensorflow_root/include  -L$tensorflow_root/lib -Wl,--no-as-needed -lrt -ldeepmd_op -ldeepmd -ldeepmd_cc -ltensorflow_cc -ltensorflow_framework -Wl,-rpath=${INSTALL_PREFIX}/lib" ../cmake

# make -j48
# make install

lammps_root=${LAMMPS_BUILD_DIR}/lammps-${LAMMPS_VERSION}
cp -r $deepmd_root/script/fugaku/fugaku_lammps_patch/* $lammps_root/src
cd $lammps_root/src
# make clean-all
# make no-user-deepmd
make yes-user-deepmd
make yes-kspace
make serial -j16
make mpi -j16