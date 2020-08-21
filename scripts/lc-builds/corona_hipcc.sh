#!/usr/bin/env bash

###############################################################################
# Copyright (c) 2016-20, Lawrence Livermore National Security, LLC
# and RAJA project contributors. See the RAJA/COPYRIGHT file for details.
#
# SPDX-License-Identifier: (BSD-3-Clause)
###############################################################################

BUILD_SUFFIX=lc_corona-hipcc

rm -rf build_${BUILD_SUFFIX} >/dev/null
mkdir build_${BUILD_SUFFIX} && cd build_${BUILD_SUFFIX}

 
module load cmake/3.14.5


cmake \
  -DHIP_ROOT_DIR="/opt/rocm-3.6.0/hip" \
  -DHIP_CLANG_PATH=/opt/rocm-3.6.0/llvm/bin \
  -DCMAKE_C_COMPILER=/opt/rocm-3.6.0/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/rocm-3.6.0/llvm/bin/clang++ \
  -C ../host-configs/lc-builds/toss3/hip.cmake \
  ..
