#include "util.hpp"
#include "add.hpp"
#include "internal/1t/dense/add.hpp"

namespace tblis
{
namespace internal
{

template <typename T>
void add_full(const communicator& comm, const config& cfg,
              T alpha, bool conj_A, const indexed_varray_view<const T>& A,
              const dim_vector& idx_A_A,
              const dim_vector& idx_A_AB,
              T  beta, bool conj_B, const indexed_varray_view<      T>& B,
              const dim_vector& idx_B_B,
              const dim_vector& idx_B_AB)
{
    varray<T> A2, B2;

    comm.broadcast(
    [&](varray<T>& A2, varray<T>& B2)
    {
        if (comm.master())
        {
            block_to_full(A, A2);
            block_to_full(B, B2);
        }

        auto len_A = stl_ext::select_from(A2.lengths(), idx_A_A);
        auto len_B = stl_ext::select_from(B2.lengths(), idx_B_B);
        auto len_AB = stl_ext::select_from(A2.lengths(), idx_A_AB);
        auto stride_A_A = stl_ext::select_from(A2.strides(), idx_A_A);
        auto stride_B_B = stl_ext::select_from(B2.strides(), idx_B_B);
        auto stride_A_AB = stl_ext::select_from(A2.strides(), idx_A_AB);
        auto stride_B_AB = stl_ext::select_from(B2.strides(), idx_B_AB);

        add(comm, cfg, len_A, len_B, len_AB,
            alpha, conj_A, A2.data(), stride_A_A, stride_A_AB,
             beta, conj_B, B2.data(), stride_B_B, stride_B_AB);

        if (comm.master())
        {
            full_to_block(B2, B);
        }
    },
    A2, B2);
}

template <typename T>
void trace_block(const communicator& comm, const config& cfg,
                 T alpha, bool conj_A, const indexed_varray_view<const T>& A,
                 const dim_vector& idx_A_A,
                 const dim_vector& idx_A_AB,
                 T  beta, bool conj_B, const indexed_varray_view<      T>& B,
                 const dim_vector& idx_B_AB)
{
    index_group<2> group_AB(A, idx_A_AB, B, idx_B_AB);
    index_group<1> group_A(A, idx_A_A);

    group_indices<2> indices_A(A, group_AB, 0, group_A, 0);
    group_indices<1> indices_B(B, group_AB, 1);
    auto nidx_A = indices_A.size();
    auto nidx_B = indices_B.size();

    dynamic_task_set tasks(comm, nidx_B, stl_ext::prod(group_AB.dense_len)*
                                         stl_ext::prod(group_A.dense_len));

    stride_type idx = 0;
    stride_type idx_A = 0;
    stride_type idx_B = 0;

    while (idx_A < nidx_A && idx_B < nidx_B)
    {
        if (indices_A[idx_A].key[0] < indices_B[idx_B].key[0])
        {
            idx_A++;
        }
        else if (indices_A[idx_A].key[0] > indices_B[idx_B].key[0])
        {
            if (beta != T(1) || (is_complex<T>::value && conj_B))
            {
                tasks.visit(idx++,
                [&,idx_B](const communicator& subcomm)
                {
                    auto data_B = B.data(0) + indices_B[idx_B].offset[0];

                    if (beta == T(0))
                    {
                        set(subcomm, cfg, B.dense_lengths(),
                            T(0), data_B, B.dense_strides());
                    }
                    else
                    {
                        scale(subcomm, cfg, B.dense_lengths(),
                              beta, conj_B, data_B, B.dense_strides());
                    }
                });
            }

            idx_B++
        }
        else
        {
            auto next_A = idx_A;

            do
            {
                next_A++;
            }
            while (next_A < nidx_A && indices_A[next_A].key[0] == indices_B[idx_B].key[0]);

            tasks.visit(idx++,
            [&,idx_A,idx_B,next_A,beta,conj_B](const communicator& subcomm)
            {
                stride_type off_A_AB, off_B_AB;
                get_local_offset(indices_A[idx_A].idx[0], group_AB,
                                 off_A_AB, 0, off_B_AB, 1);

                auto data_B = B.data(0) + indices_B[idx_B].offset[0];

                if (!group_AB.mixed_pos[1].empty())
                {
                    //
                    // Pre-scale B since we will only by adding to a
                    // portion of it
                    //

                    if (beta == T(0))
                    {
                        set(subcomm, cfg, B.dense_lengths(),
                            T(0), data_B, B.dense_strides());
                    }
                    else if (beta != T(1) || (is_complex<T>::value && conj_B))
                    {
                        scale(subcomm, cfg, B.dense_lengths(),
                              beta, conj_B, data_B, B.dense_strides());
                    }

                    data_B += off_B_AB;

                    beta = T(1);
                    conj_B = false;
                }

                for (;idx_A < next_A;idx_A++)
                {
                    auto data_A = A.data(0) + indices_A[idx_A].offset[0] + off_A_AB;

                    add(subcomm, cfg, group_A.dense_len, {}, group_AB.dense_len,
                        alpha, conj_A, data_A, group_A.dense_stride[0],
                                               group_AB.dense_stride[0],
                         beta, conj_B, data_B, {}, group_AB.dense_stride[1]);

                    beta = T(1);
                    conj_B = false;
                }
            });

            idx_A = next_A;
            idx_B++;
        }
    }

    if (beta != T(1) || (is_complex<T>::value && conj_B))
    {
        while (idx_B < nidx_B)
        {
            tasks.visit(idx++,
            [&,idx_B](const communicator& subcomm)
            {
                auto data_B = B.data(0) + indices_B[idx_B].offset[0];

                if (beta == T(0))
                {
                    set(subcomm, cfg, B.dense_lengths(),
                        T(0), data_B, B.dense_strides());
                }
                else
                {
                    scale(subcomm, cfg, B.dense_lengths(),
                          beta, conj_B, data_B, B.dense_strides());
                }
            });

            idx_B++;
        }
    }
}

template <typename T>
void replicate_block(const communicator& comm, const config& cfg,
                     T alpha, bool conj_A, const indexed_varray_view<const T>& A,
                     const dim_vector& idx_A_AB,
                     T  beta, bool conj_B, const indexed_varray_view<      T>& B,
                     const dim_vector& idx_B_B,
                     const dim_vector& idx_B_AB)
{
    index_group<2> group_AB(A, idx_A_AB, B, idx_B_AB);
    index_group<1> group_B(B, idx_B_B);

    group_indices<1> indices_A(A, group_AB, 0);
    group_indices<2> indices_B(B, group_AB, 1, group_B, 0);
    auto nidx_A = indices_A.size();
    auto nidx_B = indices_B.size();

    dynamic_task_set tasks(comm, nidx_B, stl_ext::prod(group_AB.dense_len)*
                                         stl_ext::prod(group_B.dense_len));

    stride_type idx = 0;
    stride_type idx_A = 0;
    stride_type idx_B = 0;

    while (idx_A < nidx_A && idx_B < nidx_B)
    {
        if (indices_A[idx_A].key[0] < indices_B[idx_B].key[0])
        {
            idx_A++;
        }
        else if (indices_A[idx_A].key[0] > indices_B[idx_B].key[0])
        {
            if (beta != T(1) || (is_complex<T>::value && conj_B))
            {
                tasks.visit(idx++,
                [&,idx_B](const communicator& subcomm)
                {
                    auto data_B = B.data(0) + indices_B[idx_B].offset[0];

                    if (beta == T(0))
                    {
                        set(subcomm, cfg, B.dense_lengths(),
                            T(0), data_B, B.dense_strides());
                    }
                    else
                    {
                        scale(subcomm, cfg, B.dense_lengths(),
                              beta, conj_B, data_B, B.dense_strides());
                    }
                });
            }

            idx_B++
        }
        else
        {
            do
            {
                tasks.visit(idx++,
                [&,idx_A,idx_B](const communicator& subcomm)
                {
                    stride_type off_A_AB, off_B_AB;
                    get_local_offset(indices_A[idx_A].idx[0], group_AB,
                                     off_A_AB, 0, off_B_AB, 1);

                    auto data_A = A.data(0) + indices_A[idx_A].offset[0] + off_A_AB;
                    auto data_B = B.data(0) + indices_B[idx_B].offset[0];

                    if (!group_AB.mixed_pos[1].empty())
                    {
                        //
                        // Pre-scale B since we will only by adding to a
                        // portion of it
                        //

                        if (beta == T(0))
                        {
                            set(subcomm, cfg, B.dense_lengths(),
                                T(0), data_B, B.dense_strides());
                        }
                        else if (beta != T(1) || (is_complex<T>::value && conj_B))
                        {
                            scale(subcomm, cfg, B.dense_lengths(),
                                  beta, conj_B, data_B, B.dense_strides());
                        }

                        data_B += off_B_AB;

                        add(subcomm, cfg, {}, group_B.dense_len, group_AB.dense_len,
                            alpha, conj_A, data_A, {}, group_AB.dense_stride[0],
                             T(1),  false, data_B, group_B.dense_stride[0],
                                                   group_AB.dense_stride[1]);
                    }
                    else
                    {
                        add(subcomm, cfg, {}, group_B.dense_len, group_AB.dense_len,
                            alpha, conj_A, data_A, {}, group_AB.dense_stride[0],
                             beta, conj_B, data_B, group_B.dense_stride[0],
                                                   group_AB.dense_stride[1]);
                    }
                });

                idx_B++;
            }
            while (idx_B < nidx_B && indices_A[idx_A].key[0] == indices_B[idx_B].key[0]);

            idx_A++;
        }
    }

    if (beta != T(1) || (is_complex<T>::value && conj_B))
    {
        while (idx_B < nidx_B)
        {
            tasks.visit(idx++,
            [&,idx_B](const communicator& subcomm)
            {
                auto data_B = B.data(0) + indices_B[idx_B].offset[0];

                if (beta == T(0))
                {
                    set(subcomm, cfg, B.dense_lengths(),
                        T(0), data_B, B.dense_strides());
                }
                else
                {
                    scale(subcomm, cfg, B.dense_lengths(),
                          beta, conj_B, data_B, B.dense_strides());
                }
            });

            idx_B++;
        }
    }
}

template <typename T>
void transpose_block(const communicator& comm, const config& cfg,
                     T alpha, bool conj_A, const indexed_varray_view<const T>& A,
                     const dim_vector& idx_A_AB,
                     T  beta, bool conj_B, const indexed_varray_view<      T>& B,
                     const dim_vector& idx_B_AB)
{
    index_group<2> group_AB(A, idx_A_AB, B, idx_B_AB);

    group_indices<1> indices_A(A, group_AB, 0);
    group_indices<1> indices_B(B, group_AB, 1);
    auto nidx_A = indices_A.size();
    auto nidx_B = indices_B.size();

    dynamic_task_set tasks(comm, nidx_B, stl_ext::prod(group_AB.dense_len));

    stride_type idx = 0;
    stride_type idx_A = 0;
    stride_type idx_B = 0;

    while (idx_A < nidx_A && idx_B < nidx_B)
    {
        if (indices_A[idx_A].key[0] < indices_B[idx_B].key[0])
        {
            idx_A++;
        }
        else if (indices_A[idx_A].key[0] > indices_B[idx_B].key[0])
        {
            if (beta != T(1) || (is_complex<T>::value && conj_B))
            {
                tasks.visit(idx++,
                [&,idx_B](const communicator& subcomm)
                {
                    auto data_B = B.data(0) + indices_B[idx_B].offset[0];

                    if (beta == T(0))
                    {
                        set(subcomm, cfg, B.dense_lengths(),
                            T(0), data_B, B.dense_strides());
                    }
                    else
                    {
                        scale(subcomm, cfg, B.dense_lengths(),
                              beta, conj_B, data_B, B.dense_strides());
                    }
                });
            }

            idx_B++
        }
        else
        {
            tasks.visit(idx++,
            [&,idx_A,idx_B](const communicator& subcomm)
            {
                stride_type off_A_AB, off_B_AB;
                get_local_offset(indices_A[idx_A].idx[0], group_AB,
                                 off_A_AB, 0, off_B_AB, 1);

                auto data_A = A.data(0) + indices_A[idx_A].offset[0] + off_A_AB;
                auto data_B = B.data(0) + indices_B[idx_B].offset[0];

                if (!group_AB.mixed_pos[1].empty())
                {
                    //
                    // Pre-scale B since we will only by adding to a
                    // portion of it
                    //

                    if (beta == T(0))
                    {
                        set(subcomm, cfg, B.dense_lengths(),
                            T(0), data_B, B.dense_strides());
                    }
                    else if (beta != T(1) || (is_complex<T>::value && conj_B))
                    {
                        scale(subcomm, cfg, B.dense_lengths(),
                              beta, conj_B, data_B, B.dense_strides());
                    }

                    data_B += off_B_AB;

                    add(subcomm, cfg, {}, {}, group_AB.dense_len,
                        alpha, conj_A, data_A, {}, group_AB.dense_stride[0],
                         T(1),  false, data_B, {}, group_AB.dense_stride[1]);
                }
                else
                {
                    add(subcomm, cfg, {}, {}, group_AB.dense_len,
                        alpha, conj_A, data_A, {}, group_AB.dense_stride[0],
                         beta, conj_B, data_B, {}, group_AB.dense_stride[1]);
                }
            });

            idx_A++;
            idx_B++;
        }
    }

    if (beta != T(1) || (is_complex<T>::value && conj_B))
    {
        while (idx_B < nidx_B)
        {
            tasks.visit(idx++,
            [&,idx_B](const communicator& subcomm)
            {
                auto data_B = B.data(0) + indices_B[idx_B].offset[0];

                if (beta == T(0))
                {
                    set(subcomm, cfg, B.dense_lengths(),
                        T(0), data_B, B.dense_strides());
                }
                else
                {
                    scale(subcomm, cfg, B.dense_lengths(),
                          beta, conj_B, data_B, B.dense_strides());
                }
            });

            idx_B++;
        }
    }
}

template <typename T>
void add(const communicator& comm, const config& cfg,
         T alpha, bool conj_A, const indexed_varray_view<const T>& A,
         const dim_vector& idx_A_A,
         const dim_vector& idx_A_AB,
         T  beta, bool conj_B, const indexed_varray_view<      T>& B,
         const dim_vector& idx_B_B,
         const dim_vector& idx_B_AB)
{
    if (dpd_impl == FULL)
    {
        add_full(comm, cfg,
                 alpha, conj_A, A, idx_A_A, idx_A_AB,
                  beta, conj_B, B, idx_B_B, idx_B_AB);
    }
    else if (!idx_A_A.empty())
    {
        trace_block(comm, cfg,
                    alpha, conj_A, A, idx_A_A, idx_A_AB,
                     beta, conj_B, B, idx_B_AB);
    }
    else if (!idx_B_B.empty())
    {
        replicate_block(comm, cfg,
                        alpha, conj_A, A, idx_A_AB,
                         beta, conj_B, B, idx_B_B, idx_B_AB);
    }
    else
    {
        transpose_block(comm, cfg,
                        alpha, conj_A, A, idx_A_AB,
                         beta, conj_B, B, idx_B_AB);
    }
}

#define FOREACH_TYPE(T) \
template void add(const communicator& comm, const config& cfg, \
                  T alpha, bool conj_A, const indexed_varray_view<const T>& A, \
                  const dim_vector& idx_A, \
                  const dim_vector& idx_A_AB, \
                  T  beta, bool conj_B, const indexed_varray_view<      T>& B, \
                  const dim_vector& idx_B, \
                  const dim_vector& idx_B_AB);
#include "configs/foreach_type.h"

}
}