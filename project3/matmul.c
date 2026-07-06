#include "Matrix.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <immintrin.h>
#include <omp.h>

// 定义 PCG 的状态结构体
typedef struct { 
    uint64_t state;  
    uint64_t inc; 
} pcg32_random_t;

// 初始化一个全局实例
pcg32_random_t rng = {0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL};

// 生成 32 位无符号随机数
uint32_t pcg32_random_r() {
    uint64_t oldstate = rng.state;
    rng.state = oldstate * 6364136223846793005ULL + (rng.inc | 1);
    uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
    uint32_t rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}
Matrix* create_matrix(size_t rows, size_t cols) {
    Matrix *new = (Matrix*)malloc(sizeof(Matrix));
    if(!new){
        return NULL;
    }
    new->rows = rows;
    new->cols = cols;
    new->data = (float*)_mm_malloc(sizeof(float)*rows*cols, 32);
    if(!new->data){
        free(new);
        return NULL;
    }
    return new;
}

void free_matrix(Matrix *mat) {
    if (mat) {
        _mm_free(mat->data);
        free(mat);
    }
}

void init_random_matrix(Matrix *mat) {
    if (!mat || !mat->data) return;
    size_t total_elements = mat->rows * mat->cols;
    for (size_t i = 0; i < total_elements; ++i) {
        // 将 pcg32_random_r() 的 [0, UINT32_MAX] 映射到 [0.0f, 1.0f]
        mat->data[i] = (float)pcg32_random_r() / 4294967295.0f;
    }
}

Matrix* matmul_plain(const Matrix *A, const Matrix *B) {
    if(!A || !B) {
        return NULL;
    }
    
    size_t ra = A->rows;
    size_t ca = A->cols;
    size_t rb = B->rows;
    size_t cb = B->cols;
    
    float* da = A->data;
    float* db = B->data;
    
    if(!da || !db) {
        return NULL;
    }
    if(ca != rb) {
        printf("Matrix size doesn't match\n");
        return NULL;
    }
    Matrix* new_mat = create_matrix(ra, cb);
    if (!new_mat) return NULL;
    for (size_t i = 0; i < ra; ++i) {
        for (size_t j = 0; j < cb; ++j) {
            float sum = 0.0f;
            for (size_t k = 0; k < ca; ++k) {
                sum += da[i * ca + k] * db[k * cb + j];
            }
            new_mat->data[i * cb + j] = sum;
        }
    }
    
    return new_mat;
}

Matrix* matmul_improved(const Matrix *A, const Matrix *B) {
    if (!A || !B) return NULL;
    
    size_t M = A->rows;
    size_t K = A->cols;
    size_t K2 = B->rows;
    size_t N = B->cols;
    
    if (K != K2) {
        printf("Matrix size doesn't match\n");
        return NULL;
    }
    
    Matrix* C = create_matrix(M, N);
    if (!C) return NULL;
    
    float* da = A->data;
    float* db = B->data;
    float* dc = C->data;
    
    // First-touch allocation: 让对应线程初始化自己将要乘的行，保证 NUMA 本地化
    #pragma omp parallel for schedule(static)
    for (size_t i = 0; i < M; ++i) {
        for (size_t j = 0; j < N; ++j) {
            dc[i * N + j] = 0.0f;
        }
    }

    // 针对极小规模矩阵的单核 SIMD 高速通道（砍掉 OpenMP 与分块开销）
    if (M <= 64 && N <= 64) {
        for (size_t i = 0; i < M; ++i) {
            for (size_t k = 0; k < K; ++k) {
                float a_ik = da[i * K + k];
                __m256 va = _mm256_set1_ps(a_ik);
                size_t j = 0;
                for (; j + 31 < N; j += 32) {
                    __m256 vb0 = _mm256_load_ps(&db[k * N + j]);
                    __m256 vb1 = _mm256_load_ps(&db[k * N + j + 8]);
                    __m256 vb2 = _mm256_load_ps(&db[k * N + j + 16]);
                    __m256 vb3 = _mm256_load_ps(&db[k * N + j + 24]);
                    
                    __m256 vc0 = _mm256_load_ps(&dc[i * N + j]);
                    __m256 vc1 = _mm256_load_ps(&dc[i * N + j + 8]);
                    __m256 vc2 = _mm256_load_ps(&dc[i * N + j + 16]);
                    __m256 vc3 = _mm256_load_ps(&dc[i * N + j + 24]);
                    
                    vc0 = _mm256_fmadd_ps(va, vb0, vc0);
                    vc1 = _mm256_fmadd_ps(va, vb1, vc1);
                    vc2 = _mm256_fmadd_ps(va, vb2, vc2);
                    vc3 = _mm256_fmadd_ps(va, vb3, vc3);
                    
                    _mm256_store_ps(&dc[i * N + j], vc0);
                    _mm256_store_ps(&dc[i * N + j + 8], vc1);
                    _mm256_store_ps(&dc[i * N + j + 16], vc2);
                    _mm256_store_ps(&dc[i * N + j + 24], vc3);
                }
                for (; j + 7 < N; j += 8) {
                    __m256 vb0 = _mm256_load_ps(&db[k * N + j]);
                    __m256 vc0 = _mm256_load_ps(&dc[i * N + j]);
                    vc0 = _mm256_fmadd_ps(va, vb0, vc0);
                    _mm256_store_ps(&dc[i * N + j], vc0);
                }
                for (; j < N; ++j) {
                    dc[i * N + j] += a_ik * db[k * N + j];
                }
            }
        }
        return C;
    }

    size_t BLOCK_SIZE = 64;

    #pragma omp parallel for collapse(2) schedule(dynamic)
    for (size_t i0 = 0; i0 < M; i0 += BLOCK_SIZE) {
        for (size_t j0 = 0; j0 < N; j0 += BLOCK_SIZE) {
            for (size_t k0 = 0; k0 < K; k0 += BLOCK_SIZE) {
                
                size_t imax = (i0 + BLOCK_SIZE > M) ? M : i0 + BLOCK_SIZE;
                size_t kmax = (k0 + BLOCK_SIZE > K) ? K : k0 + BLOCK_SIZE;
                size_t jmax = (j0 + BLOCK_SIZE > N) ? N : j0 + BLOCK_SIZE;

                for (size_t i = i0; i < imax; ++i) {
                    for (size_t k = k0; k < kmax; ++k) {
                        float a_ik = da[i * K + k];
                        __m256 va = _mm256_set1_ps(a_ik);
                        
                        size_t j = j0;
                        for (; j + 31 < jmax; j += 32) {
                            __m256 vb0 = _mm256_load_ps(&db[k * N + j]);
                            __m256 vb1 = _mm256_load_ps(&db[k * N + j + 8]);
                            __m256 vb2 = _mm256_load_ps(&db[k * N + j + 16]);
                            __m256 vb3 = _mm256_load_ps(&db[k * N + j + 24]);
                            
                            __m256 vc0 = _mm256_load_ps(&dc[i * N + j]);
                            __m256 vc1 = _mm256_load_ps(&dc[i * N + j + 8]);
                            __m256 vc2 = _mm256_load_ps(&dc[i * N + j + 16]);
                            __m256 vc3 = _mm256_load_ps(&dc[i * N + j + 24]);
                            
                            vc0 = _mm256_fmadd_ps(va, vb0, vc0);
                            vc1 = _mm256_fmadd_ps(va, vb1, vc1);
                            vc2 = _mm256_fmadd_ps(va, vb2, vc2);
                            vc3 = _mm256_fmadd_ps(va, vb3, vc3);
                            
                            _mm256_store_ps(&dc[i * N + j], vc0);
                            _mm256_store_ps(&dc[i * N + j + 8], vc1);
                            _mm256_store_ps(&dc[i * N + j + 16], vc2);
                            _mm256_store_ps(&dc[i * N + j + 24], vc3);
                        }
                        for (; j + 7 < jmax; j += 8) {
                            __m256 vb0 = _mm256_load_ps(&db[k * N + j]);
                            __m256 vc0 = _mm256_load_ps(&dc[i * N + j]);
                            vc0 = _mm256_fmadd_ps(va, vb0, vc0);
                            _mm256_store_ps(&dc[i * N + j], vc0);
                        }
                        for (; j < jmax; ++j) {
                            dc[i * N + j] += a_ik * db[k * N + j];
                        }
                    }
                }
            }
        }
    }
    
    return C;
}

#include "OpenBLAS/cblas.h"

Matrix* matmul_openblas(const Matrix *A, const Matrix *B) {
    if (!A || !B) return NULL;
    
    size_t M = A->rows;
    size_t K = A->cols;
    size_t K2 = B->rows;
    size_t N = B->cols;
    
    if (K != K2) {
        printf("Matrix size doesn't match\n");
        return NULL;
    }
    
    Matrix* C = create_matrix(M, N);
    if (!C) return NULL;
    
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                M, N, K,
                1.0f, A->data, K,
                B->data, N,
                0.0f, C->data, N);
                
    return C;
}
