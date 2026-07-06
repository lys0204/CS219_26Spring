#ifndef MATRIX_H
#define MATRIX_H

#include <stddef.h>
#include <stdbool.h>

// 矩阵结构体定义
typedef struct {
    size_t rows;
    size_t cols;
    float *data;
} Matrix;


// 分配并创建一个矩阵
Matrix* create_matrix(size_t rows, size_t cols);

// 释放矩阵内存
void free_matrix(Matrix *mat);

// 随机初始化矩阵（用于测试）
void init_random_matrix(Matrix *mat);

Matrix* matmul_plain(const Matrix *A, const Matrix *B);

Matrix* matmul_improved(const Matrix *A, const Matrix *B);

Matrix* matmul_openblas(const Matrix *A, const Matrix *B);

#endif