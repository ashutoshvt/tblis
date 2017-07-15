#include "shift.hpp"
#include "internal/1t/dense/shift.hpp"

#include "util/tensor.hpp"

namespace tblis
{
namespace internal
{

template <typename T>
void shift(const communicator& comm, const config& cfg,
           T alpha, T beta, bool conj_A, const indexed_varray_view<T>& A,
           const dim_vector& idx_A_A)
{
    A.for_each_index(
    [&](const varray_view<T>& local_A)
    {
        shift(comm, cfg, local_A.lengths(), alpha, beta, conj_A, local_A.data(),
              local_A.strides());
    });
}

#define FOREACH_TYPE(T) \
template void shift(const communicator& comm, const config& cfg, \
                    T alpha, T beta, bool conj_A, const dpd_varray_view<T>& A, \
                    const dim_vector&);
#include "configs/foreach_type.h"

}
}