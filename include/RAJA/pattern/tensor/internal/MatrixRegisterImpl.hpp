/*!
 ******************************************************************************
 *
 * \file
 *
 * \brief   RAJA header file defining SIMD/SIMT register operations.
 *
 ******************************************************************************
 */

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2016-19, Lawrence Livermore National Security, LLC
// and RAJA project contributors. See the RAJA/COPYRIGHT file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

#ifndef RAJA_pattern_tensor_MatrixRegisterImpl_HPP
#define RAJA_pattern_tensor_MatrixRegisterImpl_HPP

#include "camp/camp.hpp"
#include "RAJA/config.hpp"
#include "RAJA/pattern/tensor/MatrixRegister.hpp"
#include "RAJA/pattern/tensor/internal/MatrixMatrixMultiply.hpp"

//#define DEBUG_MATRIX_LOAD_STORE


namespace RAJA
{


  /*
   * 2D (Matrix) specialization of TensorRegister
   */
  template<typename REGISTER_POLICY, typename T, camp::idx_t ROW_ORD, camp::idx_t COL_ORD, camp::idx_t ROW_SIZE, camp::idx_t COL_SIZE>
  class TensorRegister<REGISTER_POLICY, T, TensorLayout<ROW_ORD, COL_ORD>, camp::idx_seq<ROW_SIZE, COL_SIZE>> :
    public internal::TensorRegisterBase<TensorRegister<REGISTER_POLICY, T, TensorLayout<ROW_ORD, COL_ORD>, camp::idx_seq<ROW_SIZE, COL_SIZE>>>
  {
    public:
      using self_type = TensorRegister<REGISTER_POLICY, T, TensorLayout<ROW_ORD, COL_ORD>, camp::idx_seq<ROW_SIZE, COL_SIZE>>;
      using base_type = internal::TensorRegisterBase<TensorRegister<REGISTER_POLICY, T, TensorLayout<ROW_ORD, COL_ORD>, camp::idx_seq<ROW_SIZE, COL_SIZE>>>;
      using vector_type = VectorRegister<T, REGISTER_POLICY>;
      using register_policy = REGISTER_POLICY;
      using element_type = T;
      using layout_type = TensorLayout<ROW_ORD, COL_ORD>;

      using transpose_tensor_type = TensorRegister<REGISTER_POLICY, T, TensorLayout<!ROW_ORD, !COL_ORD>, camp::idx_seq<ROW_SIZE, COL_SIZE>>;

    private:

      static constexpr camp::idx_t s_register_width =
          RegisterTraits<REGISTER_POLICY,T>::s_num_elem;

      // Number of registers that completely contain this matrix
      // this is regarless of the layout, just how many registers are needed
      // to fit all of the coefficients
      static constexpr camp::idx_t s_num_registers =
          (ROW_SIZE*COL_SIZE) / s_register_width;

      // We only allow matrix sizes that exactly fit in some number of registers
      static_assert((ROW_SIZE*COL_SIZE) == s_num_registers*s_register_width,
          "Matrix must exactly fit into an integer number of registers");


      // Matrix size for the within-register dimension
      // for row-major, this is the number of columns
      // for column-major, this is the nubmer of rows
      static constexpr camp::idx_t s_reg_matrix_size =
          layout_type::is_row_major() ? COL_SIZE : ROW_SIZE;

      // Number of segments each register is broken into
      // If a register is big enough that it represents more than 1 row or
      // column, then this is the number of rows or columns it represents
      // If multiple registers are needed for a given dimension, then this
      // number is zero (OR DO WE WANT 1???)
      static constexpr camp::idx_t s_segments_per_register =
          s_register_width / s_reg_matrix_size;


      // Number of registers for each register dimension
      // If we need more than 1 register to represent  a row or column, then
      // this is the number of registers needed.
      static constexpr camp::idx_t s_registers_per_dim =
          s_reg_matrix_size / s_register_width;


      vector_type m_registers[s_num_registers];


    public:

      RAJA_HOST_DEVICE
      RAJA_INLINE
      constexpr
      TensorRegister(){}

      RAJA_HOST_DEVICE
      RAJA_INLINE
      TensorRegister(element_type c)
      {
        broadcast(c);
      }


      RAJA_INLINE
      RAJA_HOST_DEVICE
      TensorRegister(self_type const &c) :
        base_type(c)
      {
        copy(c);
      }

      /*
       * Overload for:    assignment of ET to a TensorRegister
       */
      template<typename RHS,
        typename std::enable_if<std::is_base_of<RAJA::internal::ET::TensorExpressionConcreteBase, RHS>::value, bool>::type = true>
      RAJA_INLINE
      RAJA_HOST_DEVICE
      TensorRegister(RHS const &rhs)
      {
        // evaluate a single tile of the ET, storing in this TensorRegister
        *this = rhs.eval(base_type::s_get_default_tile());
      }


      template<typename ... REGS>
      explicit
      RAJA_HOST_DEVICE
      RAJA_INLINE
      TensorRegister(vector_type reg0, REGS const &... regs) :
        m_registers{reg0, regs...}
      {
        static_assert(1+sizeof...(REGS) == s_num_registers,
            "Incompatible number of registers");
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      static
      constexpr
      bool is_root() {
        return vector_type::is_root();
      }


      /*!
       * Returns true if the underlying data packed for a given tensor ref
       *
       * This is true if either:
       *   It's column major and the rows are stride one
       *   It's row major and the columns are stride one
       */
      template<camp::idx_t STRIDE_ONE_DIM>
      RAJA_HOST_DEVICE
      RAJA_INLINE
      static
      constexpr
      bool is_ref_packed() {
        return (STRIDE_ONE_DIM == 0 && layout_type::is_column_major()) ||
            (STRIDE_ONE_DIM == 1 && layout_type::is_row_major());
      }

      /*!
       * Gets the maximum size of matrix along specified dimension
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      static
      constexpr camp::idx_t s_dim_elem(camp::idx_t dim){
        return dim == 0 ? ROW_SIZE : COL_SIZE;
      }

      /*!
       * @brief Set entire vector to a single scalar value
       * @param value Value to set all vector elements to
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &operator=(element_type value)
      {
        broadcast(value);
        return *this;
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      TensorRegister &operator=(self_type const &c){
        return copy(c);
      }


      /*!
       * Provide matrix-matrix multiply for operator* between to matrices
       */
      template<typename T2, typename L, typename RP>
      self_type
      operator*(SquareMatrixRegister<T2, L, RP> const &y) const
      {
        return matrix_multiply(y);
      }

      /*!
       * Provide right matrix-vector multiply for operator* between this
       * matrix and a vector.
       */
      template<typename T2, typename RP>
      VectorRegister<T2, RP>
      operator*(VectorRegister<T2, RP> const &y) const
      {
        return right_multiply_vector(y);
      }


      /*!
       * Copy contents of another matrix
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &copy(self_type const &c){
        for(camp::idx_t i = 0;i < s_num_registers;++ i){
          m_registers[i] = c.m_registers[i];
        }
        return *this;
      }




      /*!
       * Sets all elements to zero
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &clear(){
        for(camp::idx_t i = 0;i < s_num_registers;++ i){
          m_registers[i] = vector_type(0);
        }


        return *this;
      }



      /*!
       * @brief Performs load specified by TensorRef object.
       */
      template<typename POINTER_TYPE, typename INDEX_TYPE, internal::TensorTileSize TENSOR_SIZE, camp::idx_t STRIDE_ONE_DIM>
      RAJA_INLINE
      RAJA_HOST_DEVICE
      self_type &load_ref(internal::TensorRef<POINTER_TYPE, INDEX_TYPE, TENSOR_SIZE, 2, STRIDE_ONE_DIM> const &ref){

        auto ptr = ref.m_pointer + ref.m_tile.m_begin[0]*ref.m_stride[0] +
                                   ref.m_tile.m_begin[1]*ref.m_stride[1];

        // check for packed data
        if(is_ref_packed<STRIDE_ONE_DIM>()){
          // full vector?
          if(TENSOR_SIZE == internal::TENSOR_FULL){
            load_packed(ptr, ref.m_stride[0], ref.m_stride[1]);
          }
          // partial
          else{
            load_packed_nm(ptr, ref.m_stride[0], ref.m_stride[1],
                                ref.m_tile.m_size[0], ref.m_tile.m_size[1]);
          }

        }
        // strided data
        else
        {
          // full vector?
          if(TENSOR_SIZE == internal::TENSOR_FULL){
            load_strided(ptr, ref.m_stride[0], ref.m_stride[1]);
          }
          // partial
          else{
            load_strided_nm(ptr, ref.m_stride[0], ref.m_stride[1],
                                ref.m_tile.m_size[0], ref.m_tile.m_size[1]);
          }
        }
        return *this;
      }


      /*!
       * @brief Performs load specified by TensorRef object.
       */
      template<typename POINTER_TYPE, typename INDEX_TYPE, internal::TensorTileSize TENSOR_SIZE, camp::idx_t STRIDE_ONE_DIM>
      RAJA_INLINE
      RAJA_HOST_DEVICE
      self_type const &store_ref(internal::TensorRef<POINTER_TYPE, INDEX_TYPE, TENSOR_SIZE,2, STRIDE_ONE_DIM> const &ref) const {

        auto ptr = ref.m_pointer + ref.m_tile.m_begin[0]*ref.m_stride[0] +
                                   ref.m_tile.m_begin[1]*ref.m_stride[1];

        // check for packed data
        if(is_ref_packed<STRIDE_ONE_DIM>())
        {
          // full vector?
          if(TENSOR_SIZE == internal::TENSOR_FULL){
            store_packed(ptr, ref.m_stride[0], ref.m_stride[1]);
          }
          // partial
          else{
            store_packed_nm(ptr, ref.m_stride[0], ref.m_stride[1],
                                ref.m_tile.m_size[0], ref.m_tile.m_size[1]);
          }

        }
        // strided data
        else
        {
          // full vector?
          if(TENSOR_SIZE == internal::TENSOR_FULL){
            store_strided(ptr, ref.m_stride[0], ref.m_stride[1]);
          }
          // partial
          else{
            store_strided_nm(ptr, ref.m_stride[0], ref.m_stride[1],
                                ref.m_tile.m_size[0], ref.m_tile.m_size[1]);
          }
        }
        return *this;
      }



      /*!
       * Loads a dense full matrix from memory.
       *
       * For row-major, column entries must be stride-1
       * For column-major, row entries must be stride-1
       *
       * Non-stride-1 dimension can have any striding... so this is can
       * be a "semi-dense" matrix.
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &load_packed(element_type const *ptr,
          int row_stride, int col_stride)
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: load_packed, stride=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, col_stride);
#endif
        // if it's dense in columns and rows, just do a dense load
        if((layout_type::is_row_major()&&(row_stride==ROW_SIZE)) ||
           (layout_type::is_column_major()&&(col_stride==COL_SIZE))){

          for(camp::idx_t reg = 0;reg < s_num_registers;++ reg){
            m_registers[reg].load_packed(ptr + reg*s_register_width);
          }

        }
        // Do semi-dense load for row-major
        else if(layout_type::is_row_major()){

          // one or more registers per column
          if(s_registers_per_dim){
            for(camp::idx_t row = 0;row < ROW_SIZE;++ row){
              for(camp::idx_t dimreg = 0;dimreg < s_registers_per_dim;++ dimreg){

                camp::idx_t reg = dimreg + row*s_registers_per_dim;

                camp::idx_t offset = row*row_stride + dimreg*s_register_width;

                m_registers[reg].load_packed(ptr + offset);

              }
            }
          }
          // more than one column per register
          else{
            // yikes!
          }
        }
        // Do semi-dense load for column-major
        else{
          // one or more registers per row
          if(s_registers_per_dim){
            for(camp::idx_t col = 0;col < COL_SIZE;++ col){
              for(camp::idx_t dimreg = 0;dimreg < s_registers_per_dim;++ dimreg){

                camp::idx_t reg = dimreg + col*s_registers_per_dim;

                camp::idx_t offset = col*col_stride + dimreg*s_register_width;

                m_registers[reg].load_packed(ptr + offset);

              }
            }
          }
          // more than one row per register
          else{
            // yikes!
          }
        }

        return *this;
      }

      /*!
       * Loads a strided full matrix from memory
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &load_strided(element_type const *ptr,
          int row_stride, int col_stride)
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: load_strided, stride=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, col_stride);
#endif
        if(layout_type::is_row_major()){
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            m_registers[i].load_strided(ptr+i*row_stride, col_stride);
          }
        }
        else{
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            m_registers[i].load_strided(ptr+i*col_stride, row_stride);
          }
        }

        return *this;
      }

      /*!
       * Loads a dense partial matrix from memory
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &load_packed_nm(element_type const *ptr,
          int row_stride, int col_stride,
          int num_rows, int num_cols)
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: load_packed_nm, stride=%d,%d, nm=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, 1, num_rows, num_cols);
#endif

        if(layout_type::is_row_major()){
          for(camp::idx_t i = 0;i < num_rows;++ i){
            m_registers[i].load_packed_n(ptr+i*row_stride, num_cols);
          }
          for(camp::idx_t i = num_rows;i < s_num_registers;++ i){
            m_registers[i] = vector_type(0); // clear remainder
          }
        }
        else{
          for(camp::idx_t i = 0;i < num_cols;++ i){
            m_registers[i].load_packed_n(ptr+i*col_stride, num_rows);
          }
          for(camp::idx_t i = num_cols;i < s_num_registers;++ i){
            m_registers[i] = vector_type(0); // clear remainder
          }
        }

        return *this;
      }

      /*!
       * Loads a strided partial matrix from memory
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &load_strided_nm(element_type const *ptr,
          int row_stride, int col_stride,
          int num_rows, int num_cols)
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: load_strided_nm, stride=%d,%d, nm=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, col_stride, num_rows, num_cols);
#endif

        if(layout_type::is_row_major()){
          for(camp::idx_t i = 0;i < num_rows;++ i){
            m_registers[i].load_strided_n(ptr+i*row_stride, col_stride, num_cols);
          }
          for(camp::idx_t i = num_rows;i < s_num_registers;++ i){
            m_registers[i] = vector_type(0); // clear remainder
          }
        }
        else{
          for(camp::idx_t i = 0;i < num_cols;++ i){
            m_registers[i].load_strided_n(ptr+i*col_stride, row_stride, num_rows);
          }
          for(camp::idx_t i = num_cols;i < s_num_registers;++ i){
            m_registers[i] = vector_type(0); // clear remainder
          }
        }

        return *this;
      }



      /*!
       * Store a dense full matrix to memory.
       *
       * Column entries must be stride-1, rows may be any striding
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type const &store_packed(element_type *ptr,
          int row_stride, int col_stride) const
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: store_packed, stride=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, 1);
#endif

        if(layout_type::is_row_major()){
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            m_registers[i].store_packed(ptr+i*row_stride);
          }
        }
        else{
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            m_registers[i].store_packed(ptr+i*col_stride);
          }
        }

        return *this;
      }

      /*!
       * Store a strided full matrix to memory
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type const &store_strided(element_type *ptr,
          int row_stride, int col_stride) const
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: store_strided, stride=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, col_stride);
#endif

        if(layout_type::is_row_major()){
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            m_registers[i].store_strided(ptr+i*row_stride, col_stride);
          }
        }
        else{
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            m_registers[i].store_strided(ptr+i*col_stride, row_stride);
          }
        }


        return *this;
      }

      /*!
       * Store a dense partial matrix to memory
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type const &store_packed_nm(element_type *ptr,
          int row_stride, int col_stride,
          int num_rows, int num_cols) const
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: RM store_packed_nm, stride=%d,%d, nm=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, 1, num_rows, num_cols);
#endif

        if(layout_type::is_row_major()){
          for(camp::idx_t i = 0;i < num_rows;++ i){
            m_registers[i].store_packed_n(ptr+i*row_stride, num_cols);
          }
        }
        else{
          for(camp::idx_t i = 0;i < num_cols;++ i){
            m_registers[i].store_packed_n(ptr+i*col_stride, num_rows);
          }
        }

        return *this;
      }

      /*!
       * Store a strided partial matrix to memory
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type const &store_strided_nm(element_type *ptr,
          int row_stride, int col_stride,
          int num_rows, int num_cols) const
      {
#if defined(__CUDA_ARCH__) && defined(DEBUG_MATRIX_LOAD_STORE)
        printf("th%d,%d: RM store_strided_nm, stride=%d,%d, nm=%d,%d\n",
            threadIdx.x, threadIdx.y, row_stride, col_stride, num_rows, num_cols);
#endif

        if(layout_type::is_row_major()){
          for(camp::idx_t i = 0;i < num_rows;++ i){
            m_registers[i].store_strided_n(ptr+i*row_stride, col_stride, num_cols);
          }
        }
        else{
          for(camp::idx_t i = 0;i < num_cols;++ i){
            m_registers[i].store_strided_n(ptr+i*col_stride, row_stride, num_rows);
          }
        }

        return *this;
      }




      /*!
       * Copy contents of another matrix operator
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &broadcast(element_type v){
        for(camp::idx_t i = 0;i < s_num_registers;++ i){
          m_registers[i].broadcast(v);
        }
        return *this;
      }


      /*!
       * Matrix transpose, keeping layout
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type transpose() const {

        static constexpr camp::idx_t num_elem = vector_type::s_num_elem;

        /*
         * We use Eklundh's Algorithm: Recursive block transpose because
         * it's easy to implement using SIMD register permutation primitives
         *
         * Executes in n*log(n) row operations
         *
         */
        self_type result = *this;
        for(camp::idx_t lvl = 0; (1<<lvl) < num_elem;++ lvl){
          // At this level, we do block transposes of NxN sub-matrices, where
          // N = 1<<lvl

          auto const &vals = result.m_registers;

          self_type tmp;
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            if(((i>>lvl)&0x1) == 0){
              tmp.m_registers[i] = vals[i - (i&(1<<lvl))].transpose_shuffle_left(lvl, vals[i - (i&(1<<lvl)) + (1<<lvl)]);
            }
            else{
              tmp.m_registers[i] = vals[i - (i&(1<<lvl))].transpose_shuffle_right(lvl, vals[i - (i&(1<<lvl)) + (1<<lvl)]);
            }
          }
          result = tmp;
        }

        return result;

      }


      /*!
       * Matrix transpose inplace
       *
       * Modifies contents of this matrix
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      void inplace_transpose() {
        *this = transpose();
      }

      /*!
       * Transpose this matrix by swapping row/column majorness
       *
       * Row major matrix returns column major, and visa versa.
       *
       * This has zero cost.
       *
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      transpose_tensor_type const &transpose_type() const {
        return reinterpret_cast<transpose_tensor_type const &>(*this);
      }

      /*!
       * Matrix vector product
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      vector_type right_multiply_vector(vector_type v) const {
        if(layout_type::is_row_major()){
          vector_type result;
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            result.set(v.dot(m_registers[i]), i);
          }
          return result;
        }
        else{
          vector_type result(0);
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            result +=  m_registers[i] * v.get(i);
          }
          return result;
        }
      }

      /*!
       * Matrix vector product
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      vector_type left_multiply_vector(vector_type v) const {
        if(layout_type::is_column_major()){
          vector_type result;
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            result.set(v.dot(m_registers[i]), i);
          }
          return result;
        }
        else{
          vector_type result(0);
          for(camp::idx_t i = 0;i < s_num_registers;++ i){
            result +=  m_registers[i] * v.get(i);
          }
          return result;
        }
      }


      /*!
       * Matrix vector product with accumulation into another vector
       *
       * acc += (this) * v
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      void right_multiply_vector_accumulate(vector_type &acc, vector_type v) const {
        acc.inplace_add(right_multiply_vector(v));
      }

      /*!
       * Matrix vector product with accumulation into another vector
       *
       * acc += v * (this)
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      void left_multiply_vector_accumulate(vector_type &acc, vector_type v) const {
        acc.inplace_add(left_multiply_vector(v));
      }


      /*!
       * element-wise multiplication
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type multiply(self_type mat) const {
        self_type result;
        for(camp::idx_t i = 0;i < s_num_registers;++ i){
          result.m_registers[i] = m_registers[i].multiply(mat.m_registers[i]);
        }
        return result;
      }

      /*!
       * element-wise fused multiply add
       */
      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type multiply_add(self_type mat, self_type add) const {
        self_type result;
        for(camp::idx_t i = 0;i < s_num_registers;++ i){
          result.m_registers[i] = m_registers[i].multiply_add(mat.m_registers[i], add.m_registers[i]);
        }
        return result;
      }


      /*!
       * Matrix-Matrix product
       */
      template<typename RMAT>
      RAJA_HOST_DEVICE
      RAJA_INLINE
      typename internal::MatrixMatrixMultiplyHelper<self_type, RMAT>::result_type
      matrix_multiply(RMAT const &mat) const {
        typename internal::MatrixMatrixMultiplyHelper<self_type, RMAT>::result_type res(0);
        internal::MatrixMatrixMultiplyHelper<self_type,RMAT>::multiply(*this, mat, res);
        return res;
      }

      /*!
       * Matrix-Matrix multiply add
       */
      template<typename RMAT>
      RAJA_HOST_DEVICE
      RAJA_INLINE
      typename internal::MatrixMatrixMultiplyHelper<self_type, RMAT>::result_type
      matrix_multiply_add(RMAT const &B, typename internal::MatrixMatrixMultiplyHelper<self_type, RMAT>::result_type const &C) const {
        typename internal::MatrixMatrixMultiplyHelper<self_type, RMAT>::result_type res(C);
        internal::MatrixMatrixMultiplyHelper<self_type,RMAT>::multiply_accumulate(*this, B, res);
        return res;
      }

      /*!
       * Matrix-Matrix multiply accumulate
       */
      template<typename ACCMAT, typename RMAT>
      RAJA_HOST_DEVICE
      RAJA_INLINE
      void
      matrix_multiply_accumulate(ACCMAT &acc, RMAT const &B) const {
        internal::MatrixMatrixMultiplyHelper<self_type,RMAT>::multiply_accumulate(*this, B, acc);
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type add(self_type mat) const {
        self_type result;
        for(camp::idx_t i = 0;i < s_num_registers;++ i){
          result.m_registers[i] = m_registers[i].add(mat.m_registers[i]);
        }
        return result;
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type subtract(self_type mat) const {
        self_type result;
        for(camp::idx_t i = 0;i < s_num_registers;++ i){
          result.m_registers[i] = m_registers[i].subtract(mat.m_registers[i]);
        }
        return result;
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type divide(self_type mat) const {
        self_type result;
        for(camp::idx_t i = 0;i < s_num_registers;++ i){
          result.m_registers[i] = m_registers[i].divide(mat.m_registers[i]);
        }
        return result;
      }



      RAJA_HOST_DEVICE
      RAJA_INLINE
      self_type &set(element_type val, int row, int col){
        if(layout_type::is_row_major()){
          m_registers[row].set(val, col);
        }
        else{
          m_registers[col].set(val, row);
        }
        return *this;
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      element_type get(int row, int col) const {
        return layout_type::is_row_major() ?
             m_registers[row].get(col) :
             m_registers[col].get(row);
      }


      RAJA_HOST_DEVICE
      RAJA_INLINE
      vector_type &vec(int i){
        return m_registers[i];
      }

      RAJA_HOST_DEVICE
      RAJA_INLINE
      constexpr
      vector_type const &vec(int i) const{
        return m_registers[i];
      }


      template<typename IDX_I, typename IDX_J>
      RAJA_HOST_DEVICE
      RAJA_INLINE
      element_type operator()(IDX_I row, IDX_J col){
        return this->get(row, col);
      }





      /*!
       * @brief Converts to matrix to a string
       *
       *
       */
      RAJA_INLINE
      std::string toString(bool one_line=false) const {
        std::string s = "Matrix(" + std::to_string(vector_type::s_num_elem) +
            "x" + std::to_string(vector_type::s_num_elem);
        if(!one_line){
          s +=")\n";
        }


        s += "[ ";

        //
        for(camp::idx_t r = 0;r < vector_type::s_num_elem; ++ r){
          if(r > 0){
            s += ", ";
            if(!one_line){
              s+= "\n  ";
            }
          }
          s += "[";
          for(camp::idx_t c = 0;c < vector_type::s_num_elem; ++ c){
            if(c > 0){
              s += ", ";
            }
            s += std::to_string(this->get(r,c));
          }
          s += "]";
        }

        s += " ]";
        if(!one_line){
          s+="\n";
        }
        return s;
      }

  }; // MatrixImpl - ROW MAJOR







}  // namespace RAJA




#endif
