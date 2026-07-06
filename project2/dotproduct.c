#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h> 

typedef struct timespec TimeSpec;

double get_time_diff(TimeSpec start, TimeSpec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec)/1e9;
}

int check_and_parse_size(const char *str, int *val) {
    char *endptr;
    if(!str||!val){
        return 0;
    }
    long temp = strtol(str, &endptr, 10);
    
    if (endptr == str || (*endptr != '\n' && *endptr != '\0')) {
        return 0;
    }
    if (temp < 0) {
        return 0;
    }
    
    *val = (int)temp;
    return 1;
}

void int_dot_product(int size);
void float_dot_product(int size);
void double_dot_product(int size);
void signed_char_dot_product(int size);
void short_dot_product(int size);

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

// 封装成各类基本数据类型的随机生成函数
int generate_int() {
    return (int)(pcg32_random_r() % 21) - 10;
}

float generate_float() {
    return (float)generate_int() + (float)(pcg32_random_r() % 1000) / 1000.0f;
}

double generate_double() {
    return (double)generate_int() + (double)(pcg32_random_r() % 1000000) / 1000000.0;
}

signed char generate_signed_char() {
    return (signed char)generate_int();
}

short generate_short() {
    return (short)generate_int();
}

int main() {
    char line[100];
    int type;

    // 预定义的需要测试的所有 size 数组
    int sizes[] = {10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    while (1) {
        printf("Please select the data type for calculation:\n");
        printf("0. Exit program\n");
        printf("1. int\n");
        printf("2. float\n");
        printf("3. double\n");
        printf("4. signed char\n");
        printf("5. short\n");
        printf("6. all\n");
        printf("> ");

        if (!fgets(line, sizeof(line), stdin)) {
            printf("An error occurred.\n");
            continue;
        }

        if (check_and_parse_size(line, &type)) {
            if (type == 0) {
                printf("Exiting program...\n");
                break;
            } else if (type >= 1 && type <= 6) {
                printf("\n[ Type %d selected, running all sizes ]\n", type);
                for (int i = 0; i < num_sizes; i++) {
                    int size = sizes[i];
                    printf("\n--- Size: %d ---\n", size);
                    switch (type) {
                        case 1: int_dot_product(size); break;
                        case 2: float_dot_product(size); break;
                        case 3: double_dot_product(size); break;
                        case 4: signed_char_dot_product(size); break;
                        case 5: short_dot_product(size); break;
                        case 6:
                            int_dot_product(size);
                            float_dot_product(size);
                            double_dot_product(size);
                            signed_char_dot_product(size);
                            short_dot_product(size);
                            break;
                        default: break;
                    }
                }
                printf("\n[ All sizes computed for type %d. Waiting for next instruction. ]\n", type);
                continue;
            }
        }
        printf("Invalid input. Please enter an integer from 0 to 6.\n");
    }

    return 0;
}
void int_dot_product(int size) {
    int *a = (int *)malloc(size * sizeof(int));
    int *b = (int *)malloc(size * sizeof(int));
    if (!a || !b) { if(a) free(a); if(b) free(b); return; }
    
    rng.state = (uint64_t)time(NULL); 
    for (int i = 0; i < size; i++) {
        a[i] = generate_int();
        b[i] = generate_int();
    }

    long long sum = 0;
    TimeSpec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < size; i++) {
        sum += (long long)a[i] * b[i];
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    printf("int dot product time: %.6f s (sum: %lld)\n", get_time_diff(start, end), sum);
    free(a); free(b);
}

void float_dot_product(int size) {
    float *a = (float *)malloc(size * sizeof(float));
    float *b = (float *)malloc(size * sizeof(float));
    if (!a || !b) { if(a) free(a); if(b) free(b); return; }
    
    rng.state = (uint64_t)time(NULL);
    for (int i = 0; i < size; i++) {
        a[i] = generate_float();
        b[i] = generate_float();
    }

    float sum = 0;
    TimeSpec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < size; i++) {
        sum += a[i] * b[i];
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    printf("float dot product time: %.6f s (sum: %f)\n", get_time_diff(start, end), sum);
    free(a); free(b);
}

void double_dot_product(int size) {
    double *a = (double *)malloc(size * sizeof(double));
    double *b = (double *)malloc(size * sizeof(double));
    if (!a || !b) { if(a) free(a); if(b) free(b); return; }
    
    rng.state = (uint64_t)time(NULL);

    for (int i = 0; i < size; i++) {
        a[i] = generate_double();
        b[i] = generate_double();
    }

    double sum = 0;
    TimeSpec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < size; i++) {
        sum += a[i] * b[i];
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    printf("double dot product time: %.6f s (sum: %f)\n", get_time_diff(start, end), sum);
    free(a); free(b);
}

void signed_char_dot_product(int size) {
    signed char *a = (signed char *)malloc(size * sizeof(signed char));
    signed char *b = (signed char *)malloc(size * sizeof(signed char));
    if (!a || !b) { if(a) free(a); if(b) free(b); return; }
    
    rng.state = (uint64_t)time(NULL);

    for (int i = 0; i < size; i++) {
        a[i] = generate_signed_char();
        b[i] = generate_signed_char();
    }

    long long sum = 0;
    TimeSpec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < size; i++) {
        sum += (long long)(a[i] * b[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    printf("signed char dot product time: %.6f s (sum: %lld)\n", get_time_diff(start, end), sum);
    free(a); free(b);
}

void short_dot_product(int size) {
    short *a = (short *)malloc(size * sizeof(short));
    short *b = (short *)malloc(size * sizeof(short));
    if (!a || !b) { if(a) free(a); if(b) free(b); return; }
    
    rng.state = (uint64_t)time(NULL);

    for (int i = 0; i < size; i++) {
        a[i] = generate_short();
        b[i] = generate_short();
    }

    long long sum = 0;
    TimeSpec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < size; i++) {
        sum += (long long)(a[i] * b[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    printf("short dot product time: %.6f s (sum: %lld)\n", get_time_diff(start, end), sum);
    free(a); free(b);
}