#ifndef _TBLIS_SCAL2V_HPP_
#define _TBLIS_SCAL2V_HPP_

#include "tblis.hpp"

namespace tblis
{

template <typename T>
void tblis_scal2v(bool conj_A, idx_type n,
                  T alpha, const T* A, stride_type inc_A,
                                 T* B, stride_type inc_B);

template <typename T>
void tblis_scal2v(T alpha, const_row_view<T> A, row_view<T> B);

}

#endif
