#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "Matrix.h"

typedef struct timespec TimeSpec;
double get_time_diff(TimeSpec start, TimeSpec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
}
void run_benchmark(size_t size) {
    printf("=========================================\n");
    printf("Benchmarking Matrix Size: %zu x %zu\n", size, size);
    // 1. 创建矩阵 A 和 B
    Matrix *A = create_matrix(size, size);
    Matrix *B = create_matrix(size, size);
    init_random_matrix(A);
    init_random_matrix(B);

    TimeSpec start, end;
    Matrix *C_plain = NULL;

    // 测试 Plain 版本
    if(size<8192){
        clock_gettime(CLOCK_MONOTONIC, &start);
        C_plain = matmul_plain(A, B);
        clock_gettime(CLOCK_MONOTONIC, &end);
        printf("[Plain version]    Time cost: %.6f seconds\n", get_time_diff(start, end));
    }

    // 测试 Improved 版本
    clock_gettime(CLOCK_MONOTONIC, &start);
    Matrix *C_improved = matmul_improved(A, B);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("[Improved version] Time cost: %.6f seconds\n", get_time_diff(start, end));


    //  OpenBLAS 对比测试
    clock_gettime(CLOCK_MONOTONIC, &start);
    Matrix *C_openblas = matmul_openblas(A, B);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("[OpenBLAS version] Time cost: %.6f seconds\n", get_time_diff(start, end));

    free_matrix(A);
    free_matrix(B);
    if(size<8192&&C_plain){
        free_matrix(C_plain);
    }
    if(C_improved) free_matrix(C_improved);
    if(C_openblas) free_matrix(C_openblas);
}

int main() {
    srand((unsigned int)time(NULL));
    size_t sizes[] = { 16, 128, 1024,4096,8192,16384,32768};
    size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int i = 0; i < num_sizes; i++) {
        run_benchmark(sizes[i]);
    }

    return 0;
}