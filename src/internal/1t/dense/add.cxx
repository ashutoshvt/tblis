#include "add.hpp"

#include "util/tensor.hpp"

namespace tblis
{
namespace internal
{

template <typename T>
void add(const communicator& comm, const config& cfg,
         const len_vector& len_A_,
         const len_vector& len_B_,
         const len_vector& len_AB_,
         T alpha, bool conj_A, const T* A,
         const stride_vector& stride_A_,
         const stride_vector& stride_A_AB_,
         T  beta, bool conj_B,       T* B,
         const stride_vector& stride_B_,
         const stride_vector& stride_B_AB_)
{
    auto perm_A = detail::sort_by_stride(stride_A_);
    auto perm_B = detail::sort_by_stride(stride_B_);
    auto perm_AB = detail::sort_by_stride(stride_B_AB_, stride_A_AB_);

    auto len_A = stl_ext::permuted(len_A_, perm_A);
    auto len_B = stl_ext::permuted(len_B_, perm_B);
    auto len_AB = stl_ext::permuted(len_AB_, perm_AB);

    auto stride_A = stl_ext::permuted(stride_A_, perm_A);
    auto stride_B = stl_ext::permuted(stride_B_, perm_B);
    auto stride_A_AB = stl_ext::permuted(stride_A_AB_, perm_AB);
    auto stride_B_AB = stl_ext::permuted(stride_B_AB_, perm_AB);

    if (!len_A.empty())
    {
        //TODO sum (reduce?) ukr
        //TODO fused ukr

        viterator<1> iter_A(len_A, stride_A);
        viterator<2> iter_AB(len_AB, stride_A_AB, stride_B_AB);
        len_type n = stl_ext::prod(len_AB);

        len_type n_min, n_max;
        std::tie(n_min, n_max, std::ignore) = comm.distribute_over_threads(n);

        iter_AB.position(n_min, A, B);

        for (len_type i = n_min;i < n_max;i++)
        {
            iter_AB.next(A, B);

            T sum_A = T();
            while (iter_A.next(A)) sum_A += *A;
            sum_A = alpha*(conj_A ? conj(sum_A) : sum_A);

            TBLIS_SPECIAL_CASE(conj_B,
            TBLIS_SPECIAL_CASE(beta == T(0),
            {
                *B = sum_A + beta*(conj_B ? conj(*B) : *B);
            }
            ))
        }
    }
    else if (!len_B.empty())
    {
        //TODO replicate ukr
        //TODO fused ukr

        viterator<1> iter_B(len_B, stride_B);
        viterator<2> iter_AB(len_AB, stride_A_AB, stride_B_AB);
        len_type n = stl_ext::prod(len_AB);

        len_type n_min, n_max;
        std::tie(n_min, n_max, std::ignore) = comm.distribute_over_threads(n);

        iter_AB.position(n_min, A, B);

        for (len_type i = n_min;i < n_max;i++)
        {
            iter_AB.next(A, B);

            T tmp_A = alpha*(conj_A ? conj(*A) : *A);

            TBLIS_SPECIAL_CASE(conj_B,
            TBLIS_SPECIAL_CASE(beta == T(0),
            {
                while (iter_B.next(B))
                {
                    *B = tmp_A + beta*(conj_B ? conj(*B) : *B);
                }
            }
            ))
        }
    }
    else if (!len_AB.empty())
    {
        //TODO transpose ukr

        len_type len0 = len_AB[0];
        len_vector len1(len_AB.begin()+1, len_AB.end());

        stride_type stride_A0 = stride_A_AB[0];
        stride_vector stride_A1(stride_A_AB.begin()+1,
                                           stride_A_AB.end());

        stride_type stride_B0 = stride_B_AB[0];
        stride_vector stride_B1(stride_B_AB.begin()+1,
                                           stride_B_AB.end());

        viterator<2> iter_AB(len1, stride_A1, stride_B1);
        len_type n = stl_ext::prod(len1);

        len_type m_min, m_max, n_min, n_max;
        std::tie(m_min, m_max, std::ignore,
                 n_min, n_max, std::ignore) =
            comm.distribute_over_threads_2d(len0, n);

        iter_AB.position(n_min, A, B);
        A += m_min*stride_A0;
        B += m_min*stride_B0;

        if (beta == T(0))
        {
            for (len_type i = n_min;i < n_max;i++)
            {
                iter_AB.next(A, B);
                cfg.copy_ukr.call<T>(m_max-m_min,
                                     alpha, conj_A, A, stride_A0,
                                                    B, stride_B0);
            }
        }
        else
        {
            for (len_type i = n_min;i < n_max;i++)
            {
                iter_AB.next(A, B);
                cfg.add_ukr.call<T>(m_max-m_min,
                                    alpha, conj_A, A, stride_A0,
                                     beta, conj_B, B, stride_B0);
            }
        }
    }
    else
    {
        if (beta == T(0))
        {
            cfg.copy_ukr.call<T>(1, alpha, conj_A, A, 0,
                                                   B, 0);
        }
        else
        {
            cfg.add_ukr.call<T>(1, alpha, conj_A, A, 0,
                                    beta, conj_B, B, 0);
        }
    }

    comm.barrier();
}

#define FOREACH_TYPE(T) \
template void add(const communicator& comm, const config& cfg, \
                  const len_vector& len_A, \
                  const len_vector& len_B, \
                  const len_vector& len_AB, \
                  T alpha, bool conj_A, const T* A, \
                  const stride_vector& stride_A, \
                  const stride_vector& stride_A_AB, \
                  T  beta, bool conj_B,       T* B, \
                  const stride_vector& stride_B, \
                  const stride_vector& stride_B_AB);
#include "configs/foreach_type.h"

}
}