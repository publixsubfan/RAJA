/*!
 ******************************************************************************
 *
 * \file
 *
 * \brief   Header file containing RAJA segment template methods for
 *          execution via CUDA kernel launch.
 *
 *          These methods should work on any platform that supports
 *          CUDA devices.
 *
 ******************************************************************************
 */

#ifndef RAJA_forall_cuda_HPP
#define RAJA_forall_cuda_HPP

#include "RAJA/config.hpp"

#if defined(RAJA_ENABLE_CUDA)

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016, Lawrence Livermore National Security, LLC.
//
// Produced at the Lawrence Livermore National Laboratory
//
// LLNL-CODE-689114
//
// All rights reserved.
//
// This file is part of RAJA.
//
// For additional details, please also read RAJA/LICENSE.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the disclaimer below.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the disclaimer (as noted below) in the
//   documentation and/or other materials provided with the distribution.
//
// * Neither the name of the LLNS/LLNL nor the names of its contributors may
//   be used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL LAWRENCE LIVERMORE NATIONAL SECURITY,
// LLC, THE U.S. DEPARTMENT OF ENERGY OR CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
// IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

#include "RAJA/util/defines.hpp"
#include "RAJA/util/types.hpp"

#include "RAJA/internal/fault_tolerance.hpp"

#include "RAJA/policy/cuda/MemUtils_CUDA.hpp"
#include "RAJA/policy/cuda/policy.hpp"
#include "RAJA/policy/cuda/raja_cudaerrchk.hpp"

#include "RAJA/index/IndexSet.hpp"

namespace RAJA
{

namespace impl
{

template <typename SEG_EXEC_POLICY_T, typename LOOP_BODY>
RAJA_INLINE void executeRangeList_forall(const IndexSetSegInfo* seg_info,
                                         LOOP_BODY&& loop_body);

template <typename SEG_EXEC_POLICY_T, typename LOOP_BODY>
RAJA_INLINE void executeRangeList_forall_Icount(const IndexSetSegInfo* seg_info,
                                                LOOP_BODY&& loop_body);

//
//////////////////////////////////////////////////////////////////////
//
// CUDA kernel templates.
//
//////////////////////////////////////////////////////////////////////
//

// INTERNAL namespace to encapsulate helper functions
namespace INTERNAL
{

/*!
 ******************************************************************************
 *
 * \brief calculate global thread index from 1D grid of 1D blocks
 *
 ******************************************************************************
 */
__device__ __forceinline__ unsigned int getGlobalIdx_1D_1D()
{
  unsigned int blockId = blockIdx.x;
  unsigned int threadId = blockId * blockDim.x + threadIdx.x;
  return threadId;
}
__device__ __forceinline__ unsigned int getGlobalNumThreads_1D_1D()
{
  unsigned int numThreads = blockDim.x * gridDim.x;
  return numThreads;
}

/*!
 ******************************************************************************
 *
 * \brief calculate global thread index from 3D grid of 3D blocks
 *
 ******************************************************************************
 */
__device__ __forceinline__ unsigned int getGlobalIdx_3D_3D()
{
  unsigned int blockId =
      blockIdx.x + blockIdx.y * gridDim.x + gridDim.x * gridDim.y * blockIdx.z;
  unsigned int threadId = blockId * (blockDim.x * blockDim.y * blockDim.z)
                        + (threadIdx.z * (blockDim.x * blockDim.y))
                        + (threadIdx.y * blockDim.x) + threadIdx.x;
  return threadId;
}
__device__ __forceinline__ unsigned int getGlobalNumThreads_3D_3D()
{
  unsigned int numThreads =
      blockDim.x * blockDim.y * blockDim.z * gridDim.x * gridDim.y * gridDim.z;
  return numThreads;
}

}  // end INTERNAL namespace for helper functions


/*!
 ******************************************************************************
 *
 * \brief  CUDA kernal forall template for indirection array.
 *
 ******************************************************************************
 */
template <typename Iterator, typename LOOP_BODY, typename IndexType>
__global__ void forall_cuda_kernel(LOOP_BODY loop_body,
                                   const Iterator idx,
                                   IndexType length)
{
  auto body = loop_body;
  auto ii = static_cast<IndexType>(INTERNAL::getGlobalIdx_1D_1D());
  if (ii < length) {
    body(idx[ii]);
  }
}

/*!
 ******************************************************************************
 *
 * \brief  CUDA kernal forall_Icount template for indiraction array.
 *
 *         NOTE: lambda loop body requires two args (icount, index).
 *
 ******************************************************************************
 */
template <typename Iterator, typename LOOP_BODY, typename IndexType, typename IndexType2>
__global__ void forall_Icount_cuda_kernel(LOOP_BODY loop_body,
                                          const Iterator idx,
                                          IndexType length,
                                          IndexType2 icount)
{
  auto body = loop_body;
  auto ii = static_cast<IndexType>(INTERNAL::getGlobalIdx_1D_1D());
  if (ii < length) {
    body(static_cast<IndexType>(ii + icount), idx[ii]);
  }
}

//
////////////////////////////////////////////////////////////////////////
//
// Function templates for CUDA execution over iterables.
//
////////////////////////////////////////////////////////////////////////
//

template <size_t BLOCK_SIZE, bool Async, typename Iterable, typename LOOP_BODY>
RAJA_INLINE void forall(cuda_exec<BLOCK_SIZE, Async>,
                        Iterable&& iter,
                        LOOP_BODY&& loop_body)
{
  auto begin = std::begin(iter);
  auto end   = std::end(iter);

  auto len = std::distance(begin, end);

  if (len > 0 && BLOCK_SIZE > 0) {

    auto gridSize = RAJA_DIVIDE_CEILING_INT(len, BLOCK_SIZE);

    beforeCudaKernelLaunch(gridSize, BLOCK_SIZE);

    RAJA_FT_BEGIN;

    forall_cuda_kernel<<<RAJA_CUDA_LAUNCH_PARAMS(gridSize, BLOCK_SIZE)>>>(
        loop_body, std::move(begin), len);

    RAJA_CUDA_CHECK_AND_SYNC(Async);

    RAJA_FT_END;

    afterCudaKernelLaunch();
  }
}


template <size_t BLOCK_SIZE, bool Async, typename Iterable, typename LOOP_BODY, typename IndexType>
RAJA_INLINE void forall_Icount(cuda_exec<BLOCK_SIZE, Async>,
                               Iterable&& iter,
                               IndexType icount,
                               LOOP_BODY&& loop_body)
{
  auto begin = std::begin(iter);
  auto end   = std::end(iter);

  auto len = std::distance(begin, end);

  if (len > 0 && BLOCK_SIZE > 0) {

    auto gridSize = RAJA_DIVIDE_CEILING_INT(len, BLOCK_SIZE);

    beforeCudaKernelLaunch(gridSize, BLOCK_SIZE);

    RAJA_FT_BEGIN;

    forall_Icount_cuda_kernel<<<RAJA_CUDA_LAUNCH_PARAMS(
        gridSize, BLOCK_SIZE)>>>(loop_body, std::move(begin), len, icount);

    RAJA_CUDA_CHECK_AND_SYNC(Async);

    RAJA_FT_END;

    afterCudaKernelLaunch();
  }
}

//
//////////////////////////////////////////////////////////////////////
//
// The following function templates iterate over index set segments
// using the explicitly named segment iteration policy and execute
// segments as CUDA kernels.
//
//////////////////////////////////////////////////////////////////////
//

/*!
 ******************************************************************************
 *
 * \brief  Sequential iteration over segments of index set and
 *         CUDA execution for segments.
 *
 ******************************************************************************
 */
template <size_t BLOCK_SIZE, bool Async, typename LOOP_BODY>
RAJA_INLINE void forall(
    IndexSet::ExecPolicy<seq_segit, cuda_exec<BLOCK_SIZE, Async>>,
    const IndexSet& iset,
    LOOP_BODY&& loop_body)
{

  int num_seg = iset.getNumSegments();
  for (int isi = 0; isi < num_seg; ++isi) {
    const IndexSetSegInfo* seg_info = iset.getSegmentInfo(isi);
    executeRangeList_forall<cuda_exec<BLOCK_SIZE, true>>(seg_info, loop_body);

  }  // iterate over segments of index set

  RAJA_CUDA_CHECK_AND_SYNC(Async);
}

/*!
 ******************************************************************************
 *
 * \brief  Sequential iteration over segments of index set and
 *         CUDA execution for segments.
 *
 *         This method passes index count to segment iteration.
 *
 *         NOTE: lambda loop body requires two args (icount, index).
 *
 ******************************************************************************
 */
template <size_t BLOCK_SIZE, bool Async, typename LOOP_BODY>
RAJA_INLINE void forall_Icount(
    IndexSet::ExecPolicy<seq_segit, cuda_exec<BLOCK_SIZE, Async>>,
    const IndexSet& iset,
    LOOP_BODY&& loop_body)
{
  auto num_seg = iset.getNumSegments();
  for (decltype(num_seg) isi = 0; isi < num_seg; ++isi) {
    const IndexSetSegInfo* seg_info = iset.getSegmentInfo(isi);
    executeRangeList_forall_Icount<cuda_exec<BLOCK_SIZE, true>>(seg_info,
                                                                loop_body);

  }  // iterate over segments of index set

  RAJA_CUDA_CHECK_AND_SYNC(Async);
}

}  // closing brace for impl namespace

}  // closing brace for RAJA namespace

#endif  // closing endif for RAJA_ENABLE_CUDA guard

#endif  // closing endif for header file include guard
