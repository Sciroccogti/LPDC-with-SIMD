#include "main.hpp"

#include <omp.h>

#include <bitset>
#include <iostream>
#include <vector>

#include "Alist/Alist.hpp"
#include "MatrixMath/NoramlMath.hpp"
#include "MatrixMath/SIMDMath.hpp"
// #include "LDPC/LDPC.hpp"

int main(int argc, char* argv[]) {
    // Eigen::initParallel();
    char* alist_path = NULL;
    bool enable_SIMD = true;

    if (opt(argc, argv, alist_path, enable_SIMD)) {
        return -1;
    }

    // Eigen::SparseMatrix<int> H = ldpc.getH(), G = ldpc.getG();
    // std::cout << G << H << std::endl;
    // std::cout << G * H.transpose() << std::endl;

    Alist<alist_matrix> G_alist(alist_path);
    const size_t M = 64, N = 128;
    Eigen::SparseMatrix<int> G = G_alist.getMat();

    int count[129] = {0};
    double start = omp_get_wtime();
    if (enable_SIMD) {
        // store a 64 bit with int64
        std::vector<u_int64_t> G_int;

        // store a row as a uint64, and every 4 uint64 as a batch
        // outerSize: M
        // https://eigen.tuxfamily.org/dox/group__TutorialSparse.html
        for (int k = 0; k < G.outerSize(); k++) {
            // bitset to turn bits into uint64
            // https://www.cplusplus.com/reference/bitset/bitset/
            std::bitset<M> col(0);
            for (Eigen::SparseMatrix<int>::InnerIterator it(G, k); it; ++it) {
                col[M - it.row()] = it.value();
            }
            G_int.push_back(col.to_ullong());
        }

        std::size_t inc = b_type::size;
        std::size_t size = G_int.size();
        // size for which the vectorization is possible
        std::size_t vec_size = size - size % inc;
        u_int64_t two[4] = {2, 2, 2, 2};
        b_type two_vec = xsimd::load_unaligned(two);

#pragma omp parallel for
        for (size_t i = 0; i < 4096000; i++) {
            int weight = 0;
            u_int64_t I[4] = {i, i, i, i};

            for (int j = 0; j < vec_size; j += inc) {
                b_type G_vec = xsimd::load_unaligned(&G_int[j]);
                b_type I_vec = xsimd::load_unaligned(I);
                b_type tmp_vec = G_vec & I_vec;
                tmp_vec = hamming(tmp_vec);
                tmp_vec = tmp_vec % two_vec;
                for (size_t k = 0; k < inc; k++) {
                    if (tmp_vec[k] != 0) {
                        weight++;
                    }
                }
            }
            count[weight]++;
        }
    } else {  // Non SIMD
        // store a 64 bit with int64
        u_int64_t G_int[N];

#pragma omp parallel for
        for (size_t i = 0; i < N; i++) {
            std::bitset<M> col(0);
            for (Eigen::SparseMatrix<int>::InnerIterator it(G, i); it; ++it) {
                col[it.row()] = it.value();
            }
            G_int[i] = col.to_ullong();
        }

#pragma omp parallel for
        for (size_t i = 0; i < 4096000; i++) {
            int weight = 0;
#pragma omp parallel for
            for (size_t j = 0; j < N; j++) {
                u_int64_t G_tmp = G_int[j] & i;
                G_tmp = hamming(G_tmp);
                if (G_tmp % 2) {
                    weight++;
                }
            }
            count[weight]++;
        }
    }
    double end = omp_get_wtime();

    printf("Use Time:%f\n", end - start);
    for (int i = 0; i < 129; i++) {
        printf("%d: %d\n", i, count[i]);
    }

    //     printf("Threads: %d\n", Eigen::nbThreads());
}