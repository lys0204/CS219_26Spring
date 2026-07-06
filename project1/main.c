#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#define BASE 1000
#define BASE_DIGITS 3
/*
 * NumericVar 是本计算器的核心数据结构，用于表示任意精度的浮点数。
 * 内存布局示意图:
 * 假设 BASE = 1000 (千进制)，数值 12345.006 可表示如下:
 * digits = [12, 345, 0, 6]  (数组长度 ndigits = 4)
 * weight = 1                (意味着 digits[0] 对应 1000^1, digits[1] 对应 1000^0 ...)
 *                           12 * 1000^1 + 345 * 1000^0 + 0 * 1000^-1 + 6 * 1000^-2
 *                           = 12000 + 345 + 0 + 0.006 = 12345.006
 * scale = 3                 (仅用于输出格式控制，表示显示时小数点后保留几位)
 */
typedef struct { 
    unsigned int *digits; 
    int ndigits;        
    int weight;        
    int sign;
    int scale;
} NumericVar;
// 为避免内存泄漏，每次使用完 NumericVar 必须调用此函数。
static void free_var(NumericVar *var);
// 为 NumericVar 的数字数组分配内存
static bool alloc_var_digits(NumericVar *var, int ndigits);
// 大数运算中经常需要保存中间结果或进行非破坏性操作，
// 因此需要深拷贝函数，完整复制 digits 数组的内容，而不仅仅是复制指针。
static bool copy_var(const NumericVar *src, NumericVar *dst);
// 这是一个辅助函数，用于快速确定数字的“最右端”在数轴上的位置。
static int var_low_weight(const NumericVar *v);

// 如果请求的权重 w 在 digits 数组范围内，返回对应数字；
// 如果超出范围（例如请求更高的高位或更低的低位，即前导/尾随零区域），直接返回 0。
// 简化了加减法对齐逻辑。
static unsigned int digit_at_weight(const NumericVar *v, int w);

// 移除 NumericVar 的前导零和尾随零.保持数据的紧凑性，节省内存并提高后续运算速度。
static void strip_var(NumericVar *v);
// 比较两个 NumericVar 的绝对值
// 用于决定减法时的操作顺序（大减小）以及除法时的试商逻辑。
static int cmp_abs(const NumericVar *a, const NumericVar *b);
// 绝对值相加: res = |var1| + |var2|
static bool add_abs(const NumericVar *var1, const NumericVar *var2, NumericVar *res);

// 绝对值相减: res = |var1| - |var2| (要求 |var1| >= |var2|)
static bool sub_abs(const NumericVar *var1, const NumericVar *var2, NumericVar *res);

// 通用加法: res = var1 + var2 (处理符号)
// 它会根据操作数的符号决定是调用底层加法 (同号相加) 还是底层减法 (异号相减)。
static bool add_var(const NumericVar *var1, const NumericVar *var2, NumericVar *res);

// 通用乘法: res = var1 * var2
// 实现了经典的长乘法 (Long Multiplication)，时间复杂度 O(N*M)。
// 类似于我们在纸上手算的乘法：逐位乘、移位、累加。
static bool mul_var(const NumericVar *var1, const NumericVar *var2, NumericVar *res);

// NumericVar 乘以单个数字: res = var * digit
// 乘法的一个特例优化，用于除法中的试商乘积计算。
static bool mul_var_digit(const NumericVar *var, unsigned int digit, NumericVar *res);

// 原地绝对值相减: a = |a| - |b|
static bool sub_abs_inplace(NumericVar *a, const NumericVar *b);

// 整数除法: quot = var1 / var2, rem = var1 % var2
static bool div_var(const NumericVar *var1, const NumericVar *var2, NumericVar *quot, NumericVar *rem);

// 仅保留商的整数除法 (截断余数)
static bool div_var_trunc(const NumericVar *a, const NumericVar *b, NumericVar *q);

// 使用牛顿法计算高精度平方根
// 算法: x_{n+1} = (x_n + a / x_n) / 2
static bool sqrt_scaled_var(const NumericVar *in, int target_scale, NumericVar *out, char *err, size_t err_size);

// 从 32 位无符号整数创建 NumericVar
// 便捷函数，用于将 C 语言原生类型转换为大数对象。
static bool make_u32_var(unsigned int value, NumericVar *v);

// 估算 NumericVar 的十进制位数
// 用于输出格式化前的缓冲区计算。
static int decimal_digits_estimate(const NumericVar *v);

// 原地乘以 10 的幂: v = v * 10^pow10
// 这是一个位移操作。由于 BASE=1000，乘以 10^k 需要精细处理：
static bool mul_var_pow10_inplace(NumericVar *v, int pow10);

// 计算整数幂: res = base ^ exp
static bool pow_var_u32(const NumericVar *base, unsigned int exp, NumericVar *res);

// 解析科学计数法字符串的指数部分
static bool parse_exponent_part(const char *str, int start, int end, int *exp_out);

// 将字符串解析为 NumericVar
// 这是输入的入口。需要处理：
// 1. 正负号
// 2. 小数点
// 3. 科学计数法 (e/E)
// 4. 去除多余空格
// 5. 将 ASCII 字符转换为 BASE 进制的数组
static bool parse_to_var(const char *str, NumericVar *out, char *err, size_t err_size);

// 检查字符是否为支持的运算符
// 目前支持: +, -, *, /, x, X, ^
static bool is_supported_operator(char op);

// 原地去除字符串首尾的空白字符
static char *trim_in_place(char *s);

// 将表达式字符串分割为左操作数、运算符和右操作数
// 简单的词法分析器，寻找主操作符 split 点。
static bool split_expression(char *input, char **lhs, char *op, char **rhs);

// 检查输入字符串是否为 quit/exit 命令
static bool is_quit_command(const char *s);

// 解析一元表达式 (例如 "sqrt(2)")
static bool parse_unary_expression(char *input, bool *is_sqrt, char **arg, char *err, size_t err_size);

// 将 NumericVar 转换为 32 位无符号整数
// 用于检查指数是否过大，因为乘方操作的指数通常是基本整数。
static bool var_to_u32(const NumericVar *v, unsigned int *out);

// 将 NumericVar 转换为指定小数位数的字符串
// 这是输出的出口。将内部的 BASE 进制数组转换回人类可读的十进制字符串。
static bool var_to_string_scaled(const NumericVar *v, int scale, char *buf, size_t buf_size);

// 对齐两个 NumericVar 的小数位 (用于加/减法)
// 本函数通过将精度较低的数扩大 10 的幂次来实现对齐。
static bool align_scales(NumericVar *lhs, NumericVar *rhs);

// 计算二元表达式 (例如 "1 + 2")
// 调度中心：解析 -> 对齐 -> 运算 -> 资源回收。
static bool evaluate_expression(const char *lhs_str, char op, const char *rhs_str, char **out_buf_ptr, char *err, size_t err_size);

// 计算一元表达式的结果
static bool calculate_unary_expression(const char *arg, bool is_sqrt, char **out_buf_ptr, char *err, size_t err_size);

// 处理单行输入的主函数
// 封装了错误处理、内存分配和流程控制。
static bool process_input(const char *raw_input, char *err, size_t err_size);

#define THROW_ERR(msg) do { \
    if (err != NULL && err_size > 0) snprintf(err, err_size, "%s", (msg)); \
    ret = false; \
    goto cleanup; \
} while (0)

static void free_var(NumericVar *var) { 
    if (!var) return; 
    free(var->digits); 
    var->digits = NULL; 
    var->ndigits = 0; 
    var->weight = 0; 
    var->sign = 0; 
    var->scale = 0; 
} 

static bool alloc_var_digits(NumericVar *var, int ndigits) { 
    if (!var || ndigits <= 0) return false; 
    var->digits = (unsigned int *)calloc((size_t)ndigits, sizeof(unsigned int)); 
    if (!var->digits) return false; 
    var->ndigits = ndigits; 
    return true; 
} 
static bool copy_var(const NumericVar *src, NumericVar *dst) { 
    if (!src || !dst) return false; 
    free_var(dst); 
    if (src->ndigits <= 0 || !src->digits || src->sign == 0) { 
        *dst = (NumericVar){0}; 
        return true; 
    } 
    if (!alloc_var_digits(dst, src->ndigits)) return false; 
    //实现深拷贝
    memcpy(dst->digits, src->digits, (size_t)src->ndigits * sizeof(unsigned int)); 
    dst->weight = src->weight; 
    dst->sign = src->sign; 
    dst->scale = src->scale; 
    return true; 
} 

static int var_low_weight(const NumericVar *v) { 
    return v->weight - (v->ndigits - 1); 
} 


static unsigned int digit_at_weight(const NumericVar *v, int w) { 
    if (!v || !v->digits || v->ndigits <= 0 || v->sign == 0) return 0U; 
    int idx = v->weight - w; 
    if (idx < 0 || idx >= v->ndigits) return 0U; 
    return v->digits[idx]; 
} 
static void strip_var(NumericVar *v) { 
    if (!v || !v->digits || v->ndigits <= 0) { 
        if (v) *v = (NumericVar){0}; 
        return; 
    } 
    
    // 1. 去除前导零
    int first = 0; 
    while (first < v->ndigits && v->digits[first] == 0U) first++; 

    // 如果全是零，则该数为 0
    if (first == v->ndigits) { 
        free_var(v); 
        return; 
    } 

    // 2. 去除尾随零
    int last = v->ndigits - 1; 
    while (last > first && v->digits[last] == 0U) last--; 

    int new_ndigits = last - first + 1; 
    // 如果没有变化，直接返回
    if (first == 0 && new_ndigits == v->ndigits) return; 
    // 重新分配内存
    unsigned int *new_digits = (unsigned int *)malloc((size_t)new_ndigits * sizeof(unsigned int)); 
    if (!new_digits) return; 
    // 复制有效数据段
    memcpy(new_digits, v->digits + first, (size_t)new_ndigits * sizeof(unsigned int)); 
    free(v->digits); 
    v->digits = new_digits; 
    // 权重的调整。去掉前导零意味着最高有效位变低了。
    v->weight -= first; 
    v->ndigits = new_ndigits; 
    // 如果结果是 0 (虽然上面处理了全零，但防止副作用)，符号置 0
    if (v->ndigits == 1 && v->digits[0] == 0U) v->sign = 0; 
} 

//先比较最高位的权重。权重大的数一定大。
//如果权重相同，说明数量级相同，需要从高位到低位逐位比较 
static int cmp_abs(const NumericVar *a, const NumericVar *b) { 
    if (!a || a->sign == 0 || a->ndigits <= 0 || !a->digits) { 
        if (!b || b->sign == 0 || b->ndigits <= 0 || !b->digits) return 0; 
        return -1;
    } 
    if (!b || b->sign == 0 || b->ndigits <= 0 || !b->digits) return 1; 

    // 确定比较范围的上限和下限
    int top = (a->weight > b->weight) ? a->weight : b->weight; 
    int low = (var_low_weight(a) < var_low_weight(b)) ? var_low_weight(a) : var_low_weight(b); 

    for (int w = top; w >= low; w--) { 
        unsigned int da = digit_at_weight(a, w); 
        unsigned int db = digit_at_weight(b, w); 
        if (da > db) return 1; 
        if (da < db) return -1; 
    } 
    return 0; // 完全相等
} 
static bool add_abs(const NumericVar *var1, const NumericVar *var2, NumericVar *res) { 
    if (!var1 || !var2 || !res) return false; 
    free_var(res); 

    // 处理零值加法
    if (var1->sign == 0) { 
        if (!copy_var(var2, res)) return false; 
        if (res->sign != 0) res->sign = 1; // 结果总是正的 
        return true; 
    } 
    if (var2->sign == 0) { 
        if (!copy_var(var1, res)) return false; 
        if (res->sign != 0) res->sign = 1; 
        return true; 
    } 
    // 计算结果的权重范围
    int top_w = (var1->weight > var2->weight) ? var1->weight : var2->weight; 
    int low_w = (var_low_weight(var1) < var_low_weight(var2)) ? var_low_weight(var1) : var_low_weight(var2); 
    int res_ndigits = top_w - low_w + 2; 

    if (!alloc_var_digits(res, res_ndigits)) return false; 
    res->weight = top_w + 1; // 假设发生了进位
    res->sign = 1; 
    int carry = 0; 
    // 所以这里的便利方向是从数组末尾到头部
    for (int i = res_ndigits - 1; i >= 0; i--) { 
        int target_w = res->weight - i; 
        // 核心加法: 获取两个数在当前权重位的数值并加上进位
        int sum = carry + (int)digit_at_weight(var1, target_w) + (int)digit_at_weight(var2, target_w); 
    
        if (sum >= BASE) { 
            sum -= BASE; 
            carry = 1; 
        } else { 
            carry = 0; 
        } 
        res->digits[i] = (unsigned int)sum; 
    } 
    strip_var(res); 
    if (res->ndigits > 0) res->sign = 1; 
    return true; 
} 

static bool sub_abs(const NumericVar *var1, const NumericVar *var2, NumericVar *res) { 
    if (!var1 || !var2 || !res) return false; 
    free_var(res); 

    int cmp = cmp_abs(var1, var2); 
    if (cmp == 0) { 
        *res = (NumericVar){0}; // 相等相减得零
        return true; 
    } 
    if (cmp < 0) return false; // 违反前置条件
    int top_w = var1->weight; 
    int low_w = (var_low_weight(var1) < var_low_weight(var2)) ? var_low_weight(var1) : var_low_weight(var2); 
    int res_ndigits = top_w - low_w + 1; 
    if (!alloc_var_digits(res, res_ndigits)) return false; 
    res->weight = top_w; 
    res->sign = 1; 
    int borrow = 0; 
    for (int i = res_ndigits - 1; i >= 0; i--) { 
        int target_w = res->weight - i; 
        int diff = (int)digit_at_weight(var1, target_w) - (int)digit_at_weight(var2, target_w) - borrow; 
        if (diff < 0) { 
            diff += BASE; // 向高位借位
            borrow = 1; 
        } else { 
            borrow = 0; 
        } 
        res->digits[i] = (unsigned int)diff; 
    } 

    strip_var(res); 
    if (res->ndigits > 0) res->sign = 1; 
    return true; 
} 
static bool add_var(const NumericVar *var1, const NumericVar *var2, NumericVar *res) { 
    if (!var1 || !var2 || !res) return false; 
    // 零值优化：任何数加零等于它本身
    // 需要深拷贝，因为 NumericVar 是含有动态内存的对象。
    if (var1->sign == 0) { 
        free_var(res); 
        return copy_var(var2, res); 
    } 
    if (var2->sign == 0) { 
        free_var(res); 
        return copy_var(var1, res); 
    } 
    NumericVar tmp = {0}; 
    if (var1->sign == var2->sign) { 
        // 同号相加：绝对值相加，符号不变
        if (!add_abs(var1, var2, &tmp)) return false; 
        tmp.sign = var1->sign; 
    } else { 
        // 异号相加：绝对值大减小
        int cmp = cmp_abs(var1, var2); 
        if (cmp == 0) { 
            // 绝对值相等且符号相反，结果为 0
            free_var(res); 
            *res = (NumericVar){0}; 
            return true; 
        } 
        if (cmp > 0) { 
            // |var1| > |var2|，结果符号随 var1
            if (!sub_abs(var1, var2, &tmp)) return false; 
            tmp.sign = var1->sign; 
        } else { 
            // |var2| > |var1|，结果符号随 var2
            if (!sub_abs(var2, var1, &tmp)) return false; 
            tmp.sign = var2->sign; 
        } 
    } 
    free_var(res); 
    *res = tmp;
    return true; 
} 
//var1 有 N 位，var2 有 M 位。结果最多 N+M+1 位。
//这里使用的是 "累加法"，即每次乘积直接加到结果数组的对应位置上，并立即处理进位。
static bool mul_var(const NumericVar *var1, const NumericVar *var2, NumericVar *res) { 
    if (!var1 || !var2 || !res) return false; 
    free_var(res); 
    // 乘以 0 结果为 0
    if (var1->sign == 0 || var2->sign == 0 || var1->ndigits <= 0 || var2->ndigits <= 0) { 
        *res = (NumericVar){0}; 
        return true; 
    } 
    int res_ndigits = var1->ndigits + var2->ndigits; 
    if (!alloc_var_digits(res, res_ndigits)) return false; 
    // 权重法则: (A * 10^X) * (B * 10^Y) = (A*B) * 10^(X+Y)
    // 这里 +1 是为了配合 ndigits 的对齐，后续 strip_var 会修正。
    res->weight = var1->weight + var2->weight + 1; 
    // 符号法则: 同号为正，异号为负
    res->sign = (var1->sign == var2->sign) ? 1 : -1; 
    // 执行长乘法 (两层循环)
    for (int i = var1->ndigits - 1; i >= 0; i--) { 
        uint32_t dig1 = (uint32_t)var1->digits[i]; 
        uint32_t carry = 0U; 
        if (dig1 == 0U) continue;
        for (int j = var2->ndigits - 1; j >= 0; j--) { 
            uint32_t dig2 = (uint32_t)var2->digits[j]; 
            uint32_t idx = (uint32_t)(i + j + 1); 
            // 核心乘法累加: 当前位 = 旧值 + 乘积 + 进位
            uint32_t sum = (uint32_t)res->digits[idx] + dig1 * dig2 + carry;
            res->digits[idx] = (unsigned int)(sum % (uint32_t)BASE); 
            carry = sum / (uint32_t)BASE; 
        } 
        // 处理该轮内层循环的最后一次进位，需加到更高一位
        res->digits[i] += (unsigned int)carry; 
    } 

    strip_var(res); 
    if (res->sign != 0 && res->ndigits > 0) { 
        res->sign = (var1->sign == var2->sign) ? 1 : -1; 
    } 
    return true; 
} 

//主要用于除法中的 "试商 * 除数" 步骤，或者简单的缩放操作。
static bool mul_var_digit(const NumericVar *var, unsigned int digit, NumericVar *res) { 
    if (!var || !res) return false; 
    free_var(res); 
    if (var->sign == 0 || digit == 0U) { 
        *res = (NumericVar){0}; 
        return true; 
    } 
    // 分配内存：结果最多比原数多 1 位 
    if (!alloc_var_digits(res, var->ndigits + 1)) return false; 
    // 权重不变 (除非进位导致 strip_var 调整)，符号为正
    res->weight = var->weight + 1; 
    res->sign = 1; 
    uint32_t carry = 0U; 
    // 从低位到高位遍历
    for (int i = var->ndigits - 1; i >= 0; i--) { 
        // 核心运算: 积 = 当前位 * 单精度数 + 进位
        uint32_t prod = (uint32_t)var->digits[i] * (uint32_t)digit + carry; 
        // 当前位保留 prod % BASE
        res->digits[i + 1] = (unsigned int)(prod % (uint32_t)BASE); 
        // 进位 = prod / BASE
        carry = prod / (uint32_t)BASE; 
    } 
    // 最高位填入最后的进位
    res->digits[0] = (unsigned int)carry; 
    // 去除前导零并规范化
    strip_var(res); 
    if (res->ndigits > 0) res->sign = 1; 
    return true; 
} 

//除法中 "被除数 - 商*除数" 的步骤，原地操作避免频繁分配内存。
static bool sub_abs_inplace(NumericVar *a, const NumericVar *b) { 
    NumericVar tmp = {0}; 
    if (!sub_abs(a, b, &tmp)) return false; 
    free_var(a); 
    *a = tmp; 
    return true; 
} 
/*
 *   1. 归一化 (Normalization): 将除数和被除数左移，使得除数最高位 >= BASE/2。
 *      (虽然这里 BASE=1000，也可以不做严格移位，但如果不归一化，估商会很不准)。
 *      本实现为了简化，暂时没有做 bit-level 的 D1 归一化，而是直接依赖 
 *      BASE 进制下的估算公式。
 * 
 *   2. 遍历每一位 (从高到低):
 *      对被除数的当前窗口 (rem域)，估算商 digit (qhat)。
 *      公式: qhat ≈ (u[j]*BASE + u[j+1]) / v[1]
 * 
 *   3. 修正 qhat:
 *      因为是估算，qhat 可能会偏大 1 或 2。
 *      通过校验 sub-condition 修正 qhat。
 * 
 *   4. 乘减 (Multiply & Subtract):
 *      Check if sub-segment >= qhat * divisor.
 *      Apply: current_segment -= qhat * divisor.
 *      
 *   5. 负值回调 (Add Back):
 *      如果在第4步减多了 (即 qhat 还是大了)，则将 current_segment 加上 divisor，
 *      并将 qhat 减 1。
 */
static bool div_var(const NumericVar *var1, const NumericVar *var2, NumericVar *quot, NumericVar *rem) { 
    if (!var1 || !var2 || !quot || !rem) return false; 
    free_var(quot); 
    free_var(rem); 

    // 除数为 0 -> 错误
    if (var2->sign == 0 || var2->ndigits <= 0 || !var2->digits) return false; 
    
    // 被除数为 0 -> 商0 余0
    if (var1->sign == 0 || var1->ndigits <= 0 || !var1->digits) { 
        *quot = (NumericVar){0}; 
        *rem = (NumericVar){0}; 
        return true; 
    } 

    NumericVar dividend = {0}, divisor = {0}, prod = {0}, probe = {0}; 
    bool success = false; 

    // 使用副本进行计算，因为会修改 dividend (作为余数容器)
    if (!copy_var(var1, &dividend) || !copy_var(var2, &divisor)) goto div_cleanup; 

    // 转化为绝对值进行除法，符号最后处理
    dividend.sign = 1; divisor.sign = 1; 
    strip_var(&dividend); strip_var(&divisor); 

    // 如果 |被除数| < |除数|，商为0，余数为被除数
    if (cmp_abs(&dividend, &divisor) < 0) { 
        *quot = (NumericVar){0}; 
        dividend.sign = var1->sign; // 恢复余数符号
        *rem = dividend; // 转移所有权
        dividend = (NumericVar){0}; 
        success = true; 
        goto div_cleanup; 
    } 

    // 商的位数 = (被除数权重 - 除数权重) + 1
    int qweight = dividend.weight - divisor.weight; 
    int qndigits = qweight + 1; 
    if (qndigits <= 0) qndigits = 1; 

    if (!alloc_var_digits(quot, qndigits)) goto div_cleanup; 
    quot->weight = qweight; 
    quot->sign = 1; 

    // shifted_div 代表对齐后的除数
    NumericVar shifted_div = divisor; 

    // 主循环: 从高位向低位试商
    for (int shift = qweight; shift >= 0; shift--) { 
        // 将除数逻辑上左移 shift 位 (通过 weight 调整)
        shifted_div.weight = divisor.weight + shift; 

        // 如果 当前剩余被除数 < 当前移位后的除数，说明这一位的商是 0
        if (cmp_abs(&dividend, &shifted_div) < 0) continue; 
        // 我们取被除数最高的 2 位 (u0, u1) 和 除数最高的 1 位 (v0)
        // 估算 qhat = (u0 * BASE + u1) / v0
        unsigned int rem_hi1 = digit_at_weight(&dividend, shifted_div.weight); 
        unsigned int rem_hi2 = digit_at_weight(&dividend, shifted_div.weight - 1); 
        unsigned int div_hi1 = digit_at_weight(&shifted_div, shifted_div.weight); 
        unsigned int qhat; 

        if (div_hi1 == 0U) { 
            qhat = BASE - 1; 
        } else { 
            uint32_t numer = (uint32_t)rem_hi1 * BASE + rem_hi2; 
            qhat = numer / div_hi1; 
            if (qhat >= BASE) qhat = BASE - 1; // qhat 不能超过 BASE-1
            if (qhat == 0U) qhat = 1U; 
        } 

        // 此时 qhat 可能偏大。我们需要逐步调小 qhat，直到 qhat * divisor <= dividend
        while (qhat > 0U) { 
            if (!mul_var_digit(&shifted_div, qhat, &prod)) goto div_cleanup; 
            if (cmp_abs(&prod, &dividend) <= 0) break; // 找到了合法的 qhat
            qhat--; // 试商太大，减小重试
        } 
        // 虽然通常估商公式只会偏大，不会偏小。这一步主要是为了精确性兜底
        if (qhat > 0U) { 
            while (qhat + 1U < BASE) { 
                if (!mul_var_digit(&shifted_div, qhat + 1U, &probe)) goto div_cleanup; 
                int cmp_probe = cmp_abs(&probe, &dividend); 
                if (cmp_probe <= 0) { 
                    // 如果 qhat+1 也够减，说明我们可以取更大的商
                    qhat++; 
                } else { 
                    break; 
                } 
            } 
            // 重新计算最终确定的 qhat * divisor
            if (!mul_var_digit(&shifted_div, qhat, &prod)) goto div_cleanup; 
        } 

        // 将商写入结果数组
        if (qhat > 0U) { 
            int qidx = quot->weight - shift; 
            // 安全检查索引范围
            if (qidx >= 0 && qidx < quot->ndigits) quot->digits[qidx] = qhat; 
            if (!sub_abs_inplace(&dividend, &prod)) goto div_cleanup; 
        } 
    } 

    strip_var(quot); 
    // 确定商的符号
    if (quot->sign != 0) quot->sign = (var1->sign == var2->sign) ? 1 : -1; 

    // 余数的符号与被除数一致
    if (dividend.sign != 0) dividend.sign = var1->sign; 
    *rem = dividend; // 剩余的 dividend 即为余数
    dividend = (NumericVar){0};
    success = true; 

div_cleanup: 
    free_var(&dividend); 
    free_var(&divisor); 
    free_var(&prod); 

    free_var(&probe); 
    if (!success) { 
        free_var(quot); 
        free_var(rem); 
    } 
    return success; 
} 
static bool div_var_trunc(const NumericVar *a, const NumericVar *b, NumericVar *q) { 
    NumericVar rem = {0}; 
    free_var(q); 
    if (!div_var(a, b, q, &rem)) { 
        free_var(&rem); 
        return false; 
    } 
    q->scale = 0;
    free_var(&rem); 
    return true; 
} 

//要求 sqrt(n) 精确到 k 位小数，等价于求整数 sqrt(n * 10^(2k))。
//令 N = n * 10^(2k)。我们需要求 X = floor(sqrt(N))。
// 迭代公式: x_{next} = (x_{curr} + N / x_{curr}) / 2。
 
static bool sqrt_scaled_var(const NumericVar *in, int target_scale, NumericVar *out, char *err, size_t err_size) { 
    if (!in || !out || target_scale < 0) { 
        if (err) snprintf(err, err_size, "内部转换错误。"); 
        return false; 
    } 
    // 负数不能开平方
    if (in->sign < 0) { 
        if (err) snprintf(err, err_size, "sqrt() 参数必须是非负数。"); 
        return false; 
    } 
    // 0 的平方根是 0
    if (in->sign == 0) { 
        free_var(out); 
        *out = (NumericVar){0}; 
        return true; 
    } 

    NumericVar n = {0}, x = {0}, q = {0}, sum = {0}, y = {0};
    NumericVar one = {0}, two = {0}, next = {0}, sq = {0}, tmp = {0};
    bool success = false;

    // 复制输入，以免修改原值
    if (!copy_var(in, &n)) { 
        if (err) snprintf(err, err_size, "内存不足。"); 
        free_var(&n);
        return false; 
    } 
    n.sign = 1;

    // 1. 缩放: 若原有 scale，需调整到 new_scale = 2 * target_scale
    // 公式: sqrt(Num / 10^S) = sqrt(Num) / 10^(S/2)
    // 为了让结果直接对应 target_scale，我们需要被开方数有 2*target_scale 的小数位 (或者等效的放大倍数)
    int shift = 2 * target_scale - n.scale;
    if (shift < 0 || shift > 10000) { 
        if (err) snprintf(err, err_size, "精度对于 sqrt 太大。"); 
        goto sqrt_cleanup;
    } 

    // n = n * 10^shift
    if (!mul_var_pow10_inplace(&n, shift)) { 
        if (err) snprintf(err, err_size, "内存不足。"); 
        goto sqrt_cleanup;
    } 
    // 此时 n 被视为一个巨大的整数进行开方
    n.scale = 0;

    // 2. 估算初值 x0
    // 通过十进制位数估算: sqrt(d 位数) 约等于 (d/2) 位数
    int d = decimal_digits_estimate(&n);
    int init_pow10 = (d + 1) / 2;
    int max_iter = 2 * d + 16; // 设置最大迭代次数防止死循环
    if (max_iter < 32) max_iter = 32;

    // 初始化 x = 1 * 10^init_pow10, one = 1, two = 2
    if (!make_u32_var(1U, &x) || !mul_var_pow10_inplace(&x, init_pow10) ||
        !make_u32_var(1U, &one) || !make_u32_var(2U, &two)) {
        if (err) snprintf(err, err_size, "内存不足。");
        goto sqrt_cleanup;
    }

    // 3. 牛顿迭代循环D
    for (int iter = 0; iter < max_iter; iter++) {
        // q = n / x
        if (!div_var_trunc(&n, &x, &q) ||
            // sum = x + q
            !add_var(&x, &q, &sum) ||
            // y = sum / 2
            !div_var_trunc(&sum, &two, &y)) {
            if (err) snprintf(err, err_size, "sqrt() 失败。");
            goto sqrt_cleanup;
        }

        // 收敛条件: 如果 new_val (y) >= old_val (x)，说明已经达到整数精度的极限
        if (cmp_abs(&y, &x) >= 0) break;

        // 更新 x = y
        free_var(&x);
        x = y;
        y = (NumericVar){0};
    }

    // 4. 调整 
    // 牛顿法在整数除法下得到的 x 可能是 floor(sqrt(n)) 或者稍微差一点
    // 我们需要确保 x^2 <= n < (x+1)^2
    while (true) {
        if (!add_var(&x, &one, &next) || !mul_var(&next, &next, &sq)) {
            if (err) snprintf(err, err_size, "sqrt() 失败。");
            goto sqrt_cleanup;
        }
        if (cmp_abs(&sq, &n) <= 0) {
            // (x+1)^2 <= n，说明 x 偏小了，更新 x = x+1
            free_var(&x);
            x = next;
            next = (NumericVar){0};
            continue;
        }
        break; // (x+1)^2 > n，x 可能是正确值，不再增加
    }
    // 检查 x 是否偏大? 
    while (true) {
        if (!mul_var(&x, &x, &sq)) {
            if (err) snprintf(err, err_size, "sqrt() 失败。");
            goto sqrt_cleanup;
        }
        if (cmp_abs(&sq, &n) <= 0) break; // x^2 <= n，正确
        // x^2 > n，说明 x 偏大，x = x - 1
        if (!sub_abs(&x, &one, &tmp)) {
            if (err) snprintf(err, err_size, "sqrt() 失败。");
            goto sqrt_cleanup;
        }
        free_var(&x);
        x = tmp;
        tmp = (NumericVar){0};
    }

    // 成功，输出结果
    free_var(out); 
    *out = x;
    x = (NumericVar){0}; // 防止清理时释放 out 的内容
    out->sign = 1; 
    // 恢复小数位：之前把 n 放大了 10^(2*scale)，开方后结果放大了 10^scale
    out->scale = target_scale; 
    success = true;

sqrt_cleanup:
    free_var(&n); free_var(&x); free_var(&q); free_var(&sum); free_var(&y);
    free_var(&one); free_var(&two); free_var(&next); free_var(&sq); free_var(&tmp);
    return success;
} 

static bool make_u32_var(unsigned int value, NumericVar *v) { 
    if (!v) return false; 

    free_var(v); 
    if (value == 0U) { 
        *v = (NumericVar){0}; 
        return true; 
    } 
    if (value >= (unsigned int)BASE) return false; 
    
    if (!alloc_var_digits(v, 1)) return false; 
    v->digits[0] = value; 
    v->weight = 0; 
    v->sign = 1; 
    v->scale = 0; 
    return true; 
} 

//用于开平方根时估算初值 x0 的位数。
static int decimal_digits_estimate(const NumericVar *v) {
    if (!v || v->sign == 0 || v->ndigits <= 0 || !v->digits) return 1;

    unsigned int hi = digit_at_weight(v, v->weight);
    int hi_digits = 1;
    if (hi >= 100U) {
        hi_digits = 3;
    } else if (hi >= 10U) {
        hi_digits = 2;
    }

    int d = v->weight * BASE_DIGITS + hi_digits;
    return (d > 0) ? d : 1;
}

//原地乘以 10^pow10。
//处理小数点移动、科学计数法调整。
static bool mul_var_pow10_inplace(NumericVar *v, int pow10) { 
    if (!v) return false; 
    if (pow10 < 0) return false; // 不支持除法缩放，那是小数逻辑
    if (pow10 == 0 || v->sign == 0) return true; 

    int group_shift = pow10 / BASE_DIGITS; 
    int rem = pow10 % BASE_DIGITS; 
    if (group_shift > 0) { 
        v->weight += group_shift;
    } 
    if (rem > 0) { 
        unsigned int factor = (rem == 1) ? 10U : 100U; 
        int old_sign = v->sign; 
        NumericVar tmp = {0}; 
        if (!mul_var_digit(v, factor, &tmp)) { 
            free_var(&tmp); 
            return false; 
        } 
        free_var(v); 
        *v = tmp; 
        v->sign = (v->sign == 0) ? 0 : old_sign; 
    } 
    return true; 
} 

static bool pow_var_u32(const NumericVar *base, unsigned int exp, NumericVar *res) { 
    if (!base || !res) return false;

    NumericVar acc = {0}, tmp = {0};
    bool success = false;

    if (!make_u32_var(1U, &acc)) goto pow_cleanup;

    for (unsigned int i = 0U; i < exp; i++) {
        if (!mul_var(&acc, base, &tmp)) goto pow_cleanup;
        free_var(&acc);
        acc = tmp;
        tmp = (NumericVar){0};
    }

    free_var(res);
    *res = acc;
    acc = (NumericVar){0};
    success = true;

pow_cleanup:
    free_var(&acc);
    free_var(&tmp);
    return success;
} 

static bool parse_exponent_part(const char *str, int start, int end, int *exp_out) { 
    int i = start; 
    int sign = 1; 
    long long value = 0; 

    if (i > end) return false; 
    if (str[i] == '+' || str[i] == '-') { 
        sign = (str[i] == '-') ? -1 : 1; 
        i++; 
    } 
    // e 后必须跟数字
    if (i > end || !isdigit((unsigned char)str[i])) return false; 

    while (i <= end) { 
        if (!isdigit((unsigned char)str[i])) return false; 
        value = value * 10 + (str[i] - '0'); 
        // 限制指数大小，防止 int 溢出或内存爆炸
        if (value > 1000000000LL) value = 1000000000LL; 
        i++; 
    } 
    *exp_out = (int)(sign * value); 
    return true; 
} 

/*
 *   1. 清除首尾空格。
 *   2. 提取并记录符号。
 *   3. 扫描字符串，提取纯数字 digits，同时记录 frac_digits 和 sci_exp。
 *   4. 计算 scale = frac_digits - sci_exp。
 *   5. 将纯数字字符串转换为 BASE=1000 的数组。
 *   6. 如果 scale < 0 (即大整数)，调用 mul_var_pow10 进行放大。
 */
static bool parse_to_var(const char *str, NumericVar *out, char *err, size_t err_size) { 
    if (!str || !out) { 
        snprintf(err, err_size, "输入无法解释为数字。"); 
        return false; 
    } 
    free_var(out); 

    int len = (int)strlen(str); 
    int left = 0, right = len - 1; 
    while (left <= right && isspace((unsigned char)str[left])) left++; 
    while (right >= left && isspace((unsigned char)str[right])) right--; 

    if (left > right) { 
        snprintf(err, err_size, "输入无法解释为数字。"); 
        return false; 
    } 

    int sign = 1; 
    if (str[left] == '+' || str[left] == '-') { 
        sign = (str[left] == '-') ? -1 : 1; 
        left++; 
    } 

    if (left > right) { 
        snprintf(err, err_size, "输入无法解释为数字。"); 
        return false; 
    } 

    char *digits = (char *)malloc((right - left + 2) * sizeof(char)); 
    if (!digits) { 
        snprintf(err, err_size, "内存不足。"); 
        return false; 
    } 

    int digits_len = 0, frac_digits = 0, sci_exp = 0; 
    bool seen_dot = false, seen_exp = false, saw_digit = false; 

    for (int i = left; i <= right; i++) { 
        char c = str[i]; 
        if (isdigit((unsigned char)c)) { 
            digits[digits_len++] = c; 
            saw_digit = true; 
            if (seen_dot && !seen_exp) frac_digits++; 
            continue; 
        } 
        if (c == '.' && !seen_dot && !seen_exp) { 
            seen_dot = true; 
            continue; 
        } 
        if ((c == 'e' || c == 'E') && !seen_exp) { 
            seen_exp = true; 
            if (!parse_exponent_part(str, i + 1, right, &sci_exp)) { 
                free(digits); 
                snprintf(err, err_size, "输入无法解释为数字。"); 
                return false; 
            } 
            break; 
        } 
        free(digits); 
        snprintf(err, err_size, "输入无法解释为数字。"); 
        return false; 
    } 

    if (!saw_digit) { 
        free(digits); 
        snprintf(err, err_size, "输入无法解释为数字。"); 
        return false; 
    } 

    int start_idx = 0; 
    while (start_idx < digits_len && digits[start_idx] == '0') start_idx++; 

    if (start_idx == digits_len) { 
        free(digits); 
        *out = (NumericVar){0}; 
        return true; 
    } 

    if (start_idx > 0) { 
        memmove(digits, digits + start_idx, (size_t)(digits_len - start_idx)); 
        digits_len -= start_idx; 
    } 

    int scale = frac_digits - sci_exp; 

    while (digits_len > 1 && digits[digits_len - 1] == '0' && scale > 0) { 
        digits_len--; 
        scale--; 
    } 

    out->ndigits = (digits_len + BASE_DIGITS - 1) / BASE_DIGITS; 
    out->digits = (unsigned int *)calloc((size_t)out->ndigits, sizeof(unsigned int)); 
    if (!out->digits) { 
        free(digits); 
        *out = (NumericVar){0}; 
        snprintf(err, err_size, "内存不足。"); 
        return false; 
    }

    int first_len = digits_len - (out->ndigits - 1) * BASE_DIGITS; 
    int pos = 0; 
    for (int group = 0; group < out->ndigits; group++) { 
        int width = (group == 0) ? first_len : BASE_DIGITS; 
        unsigned int value = 0; 
        for (int k = 0; k < width; k++) { 
            value = value * 10U + (unsigned int)(digits[pos++] - '0'); 
        } 
        out->digits[group] = value; 
    } 

    out->weight = out->ndigits - 1; 
    out->sign = sign; 
    out->scale = scale; 
    strip_var(out); 
    if (out->sign != 0) { 
        out->sign = sign; 
        out->scale = scale; 
    } 

    if (out->scale < 0) { 
        int pow10 = -out->scale; 
        if (pow10 > 10000) { 
            free(digits); 
            free_var(out); 
            snprintf(err, err_size, "指数对于当前计算器过大。"); 
            return false; 
        } 
        if (!mul_var_pow10_inplace(out, pow10)) { 
            free(digits); 
            free_var(out); 
            snprintf(err, err_size, "内存不足。"); 
            return false; 
        } 
        if (out->sign != 0) out->sign = sign; 
        out->scale = 0; 
    } 

    if (out->scale > 10000) { 
        free(digits); 
        free_var(out); 
        snprintf(err, err_size, "精度对于当前计算器过大。"); 
        return false; 
    } 

    free(digits); 
    return true; 
} 

static bool is_supported_operator(char op) { 
    return (op == '+' || op == '-' || op == '*' || op == '/' || op == '^' || op == 'x' || op == 'X'); 
} 

static char *trim_in_place(char *s) { 
    if (!s) return NULL; 
    while (*s != '\0' && isspace((unsigned char)*s)) s++; 
    if (*s == '\0') return s; 

    char *end = s + strlen(s) - 1; 
    while (end > s && isspace((unsigned char)*end)) { 
        *end = '\0'; 
        end--; 
    } 
    return s; 
} 

static bool split_expression(char *input, char **lhs, char *op, char **rhs) { 
    if (!input || !lhs || !op || !rhs) return false; 
    input = trim_in_place(input); 
    if (*input == '\0') return false; 

    int len = (int)strlen(input); 
    int op_index = -1; 

    for (int i = 0; i < len; i++) { 
        char c = input[i]; 
        if (!is_supported_operator(c)) continue; 
        // 忽略行首的符号 (视为一元正负号)
        if (i == 0) continue; 
        // 忽略科学计数法中的 'e+' 'e-'
        if ((c == '+' || c == '-') && (input[i - 1] == 'e' || input[i - 1] == 'E')) continue; 
        // 忽略紧跟在其他符号后的符号 (如 "3 * -5" 中的 -)
        if (c == '+' || c == '-') { 
            int j = i - 1; 
            while (j >= 0 && isspace((unsigned char)input[j])) j--; 
            if (j >= 0) { 
                char prev = input[j]; 
                // 如果前一个有效字符也是符号，那当前这个肯定是正负号，不是二元操作符
                if (is_supported_operator(prev) && prev != 'e' && prev != 'E') continue; 
            } 
        } 

        // 如果之前已经找到过操作符，说明是连算 (如 1+2+3)，不支持
        if (op_index != -1) return false; 
        op_index = i; 
    } 

    if (op_index <= 0 || op_index >= len - 1) return false; 

    *op = input[op_index]; 
    input[op_index] = '\0'; 
    *lhs = trim_in_place(input); 
    // rhd 起始位置是 op 后一位
    *rhs = trim_in_place(input + op_index + 1); 

    if (**lhs == '\0' || **rhs == '\0') return false; 
    return true; 
} 

static bool is_quit_command(const char *s) { 
    return (strcmp(s, "quit") == 0); 
} 
static bool parse_unary_expression(char *input, bool *is_sqrt, char **arg, char *err, size_t err_size) { 
    if (!input || !is_sqrt || !arg) { 
        if (err) snprintf(err, err_size, "函数格式无效。"); 
        return false; 
    } 

    if (strncmp(input, "sqrt(", 5) == 0) { 
        *is_sqrt = true; 
    } else { 
        if (err) snprintf(err, err_size, "函数格式无效。"); 
        return false; 
    }

    char *arg_start = strchr(input, '('); 
    char *arg_end = strrchr(input, ')'); 
    if (!arg_start || !arg_end || arg_end < arg_start + 1 || arg_end[1] != '\0') { 
        if (err) snprintf(err, err_size, "函数格式无效。"); 
        return false; 
    } 

    *arg_end = '\0'; 
    *arg = trim_in_place(arg_start + 1); 
    if (**arg == '\0') { 
        if (err) snprintf(err, err_size, "函数参数不能为空。"); 
        return false; 
    } 
    return true; 
} 

//从 NumericVar 获取指数值。

static bool var_to_u32(const NumericVar *v, unsigned int *out) { 
    if (!v || !out) return false; 
    if (v->sign < 0) return false; 
    if (v->sign == 0 || v->ndigits <= 0 || !v->digits) { 
        *out = 0U; 
        return true; 
    } 

    uint64_t value = 0U; 
    for (int i = 0; i < v->ndigits; i++) { 
        value = value * (uint64_t)BASE + (uint64_t)v->digits[i]; 
        if (value > 100000U) return false; 
    } 
    // 如果 weight 大于 ndigits-1，说明后面还有 weight 个 0 group
    int extra_groups = v->weight - (v->ndigits - 1); 
    while (extra_groups > 0) { 
        value *= (uint64_t)BASE; 
        if (value > 100000U) return false; 
        extra_groups--; 
    } 
    *out = (unsigned int)value; 
    return true; 
} 

/*
 *   1. 先将所有 digits 打印到一个纯数字的临时 buffer (raw string)。
 *      注意补齐中间 group 的 '000' (如 12, 5 -> "12005")。
 *   2. 处理负号。
 *   3. 根据 scale 在 raw string 中插入小数点。
 *      如果 scale > raw_len，需要补前导 '0.00...'。
 *   4. 去除末尾多余的 '0' 和 '.'。
 */
static bool var_to_string_scaled(const NumericVar *v, int scale, char *buf, size_t buf_size) { 
    if (!v || !buf || buf_size == 0) return false; 

    if (v->sign == 0 || v->ndigits <= 0 || !v->digits) { 
        if (snprintf(buf, buf_size, "0") < 0) return false; 
        return true; 
    } 
    // 估算所需空间
    int total_groups = (v->weight + 1 > v->ndigits) ? (v->weight + 1) : v->ndigits; 
    if (total_groups < 1) total_groups = 1; 

    size_t raw_cap = (size_t)total_groups * (size_t)BASE_DIGITS + 4U; 
    char *raw = (char *)malloc(raw_cap); 
    if (!raw) return false; 
    // 生成纯数字字符串
    // 最高位不需要补0
    size_t raw_len = (size_t)snprintf(raw, raw_cap, "%u", v->digits[0]); 
    // 后续位需要补0对齐 (如 5 -> "005")
    for (int i = 1; i < v->ndigits; i++) { 
        raw_len += (size_t)snprintf(raw + raw_len, raw_cap - raw_len, "%03u", v->digits[i]); 
    } 
    // 补末尾的 0 group
    int extra_groups = v->weight - (v->ndigits - 1); 
    for (int i = 0; i < extra_groups; i++) { 
        raw_len += (size_t)snprintf(raw + raw_len, raw_cap - raw_len, "%03u", 0U); 
    } 

    size_t pos = 0U; 
    if (v->sign < 0) { 
        if (pos + 1U >= buf_size) { free(raw); return false; } 
        buf[pos++] = '-'; 
    } 

    if (scale <= 0) { 
        // 整数直接拷贝
        if (pos + raw_len + 1U > buf_size) { free(raw); return false; } 
        memcpy(buf + pos, raw, raw_len); 
        pos += raw_len; 
        buf[pos] = '\0'; 
        free(raw); 
        return true; 
    } 
    // 插入小数点
    if (raw_len <= (size_t)scale) { 
        // 纯小数: 0.00xxxx
        size_t zeros = (size_t)scale - raw_len; 
        if (pos + 2U + zeros + raw_len + 1U > buf_size) { free(raw); return false; } 
        buf[pos++] = '0'; 
        buf[pos++] = '.'; 
        memset(buf + pos, '0', zeros); 
        pos += zeros; 
        memcpy(buf + pos, raw, raw_len); 
        pos += raw_len; 
    } else { 
        // 混合数: xxx.xxxx
        size_t int_len = raw_len - (size_t)scale; 
        if (pos + int_len + 1U + (size_t)scale + 1U > buf_size) { free(raw); return false; } 
        memcpy(buf + pos, raw, int_len); 
        pos += int_len; 
        buf[pos++] = '.'; 
        memcpy(buf + pos, raw + int_len, (size_t)scale); 
        pos += (size_t)scale; 
    } 
    // 去除末尾 0
    while (pos > 0U && buf[pos - 1U] == '0') pos--; 
    if (pos > 0U && buf[pos - 1U] == '.') pos--; // 如果剩下 "123."，去除点
    // -0 变成 0
    if (pos == 1U && buf[0] == '-') { 
        buf[0] = '0'; 
        pos = 1U; 
    } 
    buf[pos] = '\0'; 
    free(raw); 
    return true; 
} 
//对齐两个变量的小数位数 (scale)，取较大的 scale。

static bool align_scales(NumericVar *lhs, NumericVar *rhs) { 
    if (!lhs || !rhs) return false; 
    int target_scale = (lhs->scale > rhs->scale) ? lhs->scale : rhs->scale; 

    int diff = target_scale - lhs->scale; 
    if (diff > 0 && !mul_var_pow10_inplace(lhs, diff)) return false; 

    diff = target_scale - rhs->scale; 
    if (diff > 0 && !mul_var_pow10_inplace(rhs, diff)) return false; 

    lhs->scale = target_scale; 
    rhs->scale = target_scale; 
    return true; 
} 

/*
 *     [+ / -] 加减法: 必须先对齐 scale (alignment)，然后进行整数加减。结果 scale = target_scale。
 *     [*] 乘法: 结果 scale = scale1 + scale2。
 *     [/] 除法: 
 *       设 A = lhs * 10^-Sa, B = rhs * 10^-Sb。
 *       目标结果 Q = (A/B) * 10^-St。
 *       Q = A/B * 10^-(Sa-Sb)
 *       为了得到 Q 的整数部分，我们需要计算:
 *       IntegerQ = (A * 10^(Sb + St)) / (B * 10^Sa)
 *       这样 IntegerQ * 10^-St = (A * 10^Sb) / (B * 10^Sa) = (A/B) * 10^(Sb-Sa)。
 *     [^] 乘方: 仅支持整数指数。结果 scale = base.scale * exp。
 */
static bool evaluate_expression(const char *lhs_str, char op, const char *rhs_str, char **out_buf_ptr, char *err, size_t err_size) { 
    bool ret = true; 
    NumericVar lhs_var = {0}, rhs_var = {0}, result = {0}, tmp = {0}, rem = {0}; 
    char *internal_buf = NULL;

    if (!parse_to_var(lhs_str, &lhs_var, err, err_size) || !parse_to_var(rhs_str, &rhs_var, err, err_size)) { 
        ret = false; 
        goto cleanup; 
    } 

    switch (op) { 
        case '+': 
        case '-': 
            // 加减法需对齐小数点
            if (!align_scales(&lhs_var, &rhs_var)) THROW_ERR("内存不足。"); 
            // 减法转换为: A + (-B)
            if (op == '-') rhs_var.sign = -rhs_var.sign; 
            if (!add_var(&lhs_var, &rhs_var, &result)) THROW_ERR("内存不足。"); 
            result.scale = lhs_var.scale; 
            break; 
        case '*': case 'x': case 'X': 
            // 乘法: 1.2 * 3.4 = 4.08 (scale: 1+1=2)
            if (!mul_var(&lhs_var, &rhs_var, &result)) THROW_ERR("内存不足。"); 
            result.scale = lhs_var.scale + rhs_var.scale; 
            break; 
        case '/': 
            if (rhs_var.sign == 0) THROW_ERR("除数不能为零。"); 
            int target_scale = 18; // 默认保留18位小数精度
            if (rhs_var.scale + target_scale > 10000 || lhs_var.scale > 10000) THROW_ERR("精度对于除法过大。"); 
            if (!copy_var(&lhs_var, &tmp) || !mul_var_pow10_inplace(&tmp, rhs_var.scale + target_scale) || 
                !copy_var(&rhs_var, &result) || !mul_var_pow10_inplace(&result, lhs_var.scale)) { 
                THROW_ERR("内存不足。"); 
            } 
            free_var(&rhs_var); 
            rhs_var = (NumericVar){0}; 
            // 执行带余除法
            if (!div_var(&tmp, &result, &rhs_var, &rem)) THROW_ERR("除法失败。"); 
            free_var(&result); 
            result = rhs_var;  // 商在这里
            rhs_var = (NumericVar){0}; 
            result.scale = target_scale; 
            break; 
        case '^': { 
            unsigned int exp_u32 = 0U; 
            if (rhs_var.scale != 0) THROW_ERR("指数必须是整数。"); 
            if (rhs_var.sign < 0) THROW_ERR("当前核心不支持负指数。"); 
            if (!var_to_u32(&rhs_var, &exp_u32)) THROW_ERR("指数过大。"); 
            
            if (lhs_var.scale > 0 && exp_u32 > 0U && (uint64_t)lhs_var.scale * exp_u32 > 10000U) THROW_ERR("结果精度过大。"); 
            
            if (!pow_var_u32(&lhs_var, exp_u32, &result)) THROW_ERR("内存不足。"); 
            result.scale = (int)((uint64_t)lhs_var.scale * exp_u32); 
            break; 
        } 

        default: 
            THROW_ERR("无效的操作符。仅支持 +, -, *, x, /, ^。"); 
    } 

    size_t total_groups = (size_t)result.ndigits;
    if (result.weight + 1 > result.ndigits) total_groups = (size_t)(result.weight + 1);
    if (total_groups < 1) total_groups = 1;
    size_t needed_size = total_groups * BASE_DIGITS + (size_t)result.scale + 128; 
    internal_buf = (char *)malloc(needed_size);
    if (!internal_buf) THROW_ERR("内存不足，无法分配输出缓冲区。");

    if (!var_to_string_scaled(&result, result.scale, internal_buf, needed_size)) { 
        free(internal_buf);
        THROW_ERR("结果太长，无法打印。"); 
    } 
    *out_buf_ptr = internal_buf;

cleanup: 
    free_var(&lhs_var); 
    free_var(&rhs_var); 
    free_var(&result); 
    free_var(&tmp); 
    free_var(&rem); 
    return ret; 
} 

static bool calculate_unary_expression(const char *arg, bool is_sqrt, char **out_buf_ptr, char *err, size_t err_size) { 
    NumericVar parsed = {0}, unary_result = {0}; 
    bool ret = true; 
    char *internal_buf = NULL;

    if (!parse_to_var(arg, &parsed, err, err_size)) { 
        ret = false; 
        goto unary_cleanup; 
    } 

    if (is_sqrt) { 
        if (!sqrt_scaled_var(&parsed, 18, &unary_result, err, err_size)) { 
            ret = false; 
            goto unary_cleanup; 
        } 
    } 
    size_t total_groups = (size_t)unary_result.ndigits;
    if (unary_result.weight + 1 > unary_result.ndigits) total_groups = (size_t)(unary_result.weight + 1);
    if (total_groups < 1) total_groups = 1;
    size_t needed_size = total_groups * BASE_DIGITS + (size_t)unary_result.scale + 128;
    internal_buf = (char *)malloc(needed_size);
    if (!internal_buf) {
        if (err) snprintf(err, err_size, "内存不足，无法分配输出缓冲区。"); 
        ret = false; 
        goto unary_cleanup; 
    }

    if (!var_to_string_scaled(&unary_result, unary_result.scale, internal_buf, needed_size)) { 
        if (err) snprintf(err, err_size, "结果太长，无法打印。"); 
        free(internal_buf);
        ret = false; 
        goto unary_cleanup; 
    } 
    *out_buf_ptr = internal_buf;

unary_cleanup: 
    free_var(&parsed); 
    free_var(&unary_result); 
    return ret; 
} 

static bool process_input(const char *raw_input, char *err, size_t err_size) { 
    char *buffer = NULL; 
    char *result_buf = NULL; 

    if (!raw_input) { 
        if (err) snprintf(err, err_size, "无效输入：表达式为空。"); 
        return false; 
    } 

    size_t input_len = strlen(raw_input);
    buffer = (char *)malloc(input_len + 1);
    if (!buffer) {
        if (err) snprintf(err, err_size, "内存不足。");
        return false;
    }
    strcpy(buffer, raw_input); 

    char *input = trim_in_place(buffer); 
    if (*input == '\0') { 
        free(buffer);
        if (err) snprintf(err, err_size, "无效输入：表达式为空。"); 
        return false; 
    } 

    if (strncmp(input, "sqrt(", 5) == 0) { 
        bool is_sqrt = false; 
        char *arg = NULL; 

        if (!parse_unary_expression(input, &is_sqrt, &arg, err, err_size)) { 
            free(buffer);
            return false; 
        } 

        if (!calculate_unary_expression(arg, is_sqrt, &result_buf, err, err_size)) { 
            free(buffer);
            return false; 
        } 

        printf("%s\n", result_buf); 
        free(result_buf);
        free(buffer);
        return true; 
    } 
    // 二元表达式
    char *lhs = NULL, *rhs = NULL, op = '\0'; 

    if (!split_expression(input, &lhs, &op, &rhs)) { 
        free(buffer);
        if (err) snprintf(err, err_size, "无效输入：预期格式 '<数字> <操作符> <数字>'。"); 
        return false; 
    } 

    if (!evaluate_expression(lhs, op, rhs, &result_buf, err, err_size)) { 
        free(buffer);
        return false; 
    } 

    printf("%s %c %s = %s\n", lhs, op, rhs, result_buf); 
    free(result_buf);
    free(buffer);
    return true; 
} 

int main(int argc, char *argv[]) { 
    char err[128] = {0}; 
    // 模式 1: ./calculator <a> <op> <b>
    if (argc == 4) { 
        size_t total_len = strlen(argv[1]) + strlen(argv[2]) + strlen(argv[3]) + 3;
        char *buf = malloc(total_len);
        if (!buf) {
            printf("内存不足。\n");
            return 1;
        }
        snprintf(buf, total_len, "%s %s %s", argv[1], argv[2], argv[3]); 
        if (!process_input(buf, err, sizeof(err))) { 
            printf("%s\n", err); 
            free(buf);
            return 1; 
        } 
        free(buf);
        return 0; 
    } 
    // 模式 2: ./calculator "expression"
    if (argc == 2) { 
        if (!process_input(argv[1], err, sizeof(err))) { 
            printf("%s\n", err); 
            return 1; 
        } 
        return 0; 
    } 
    if (argc != 1) { 
        printf("用法: ./calculator <数字1> <操作符> <数字2>\n"); 
        printf("   或: ./calculator \"<表达式>\"\n"); 
        return 1; 
    } 
    // 模式 3: REPL 
    printf("交互模式。输入表达式，或输入 'quit' 退出。\n"); 
    while (true) { 
        char line[4096]; 
        printf("> "); 
        if (fgets(line, sizeof(line), stdin) == NULL) break; 

        char *input = trim_in_place(line); 
        if (*input == '\0') continue; 
        if (is_quit_command(input)) break; 

        if (!process_input(input, err, sizeof(err))) { 
            printf("%s\n", err); 
        } 
    } 

    return 0; 
} 





