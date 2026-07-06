#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

/* --- 常量 --- */
#define BASE 1000
#define BASE_DIGITS 3

/* --- 结构体定义 --- */
/*
 * NumericVar 是本计算器的核心数据结构，用于表示任意精度的浮点数。
 * 设计思想参考了 Cpython 的长整型实现，但针对浮点运算做了适配。
 * 
 * 内存布局示意图:
 * 假设 BASE = 1000 (千进制)，数值 12345.006 可表示如下:
 * digits = [12, 345, 0, 6]  (数组长度 ndigits = 4)
 * weight = 1                (意味着 digits[0] 对应 1000^1, digits[1] 对应 1000^0 ...)
 *                           12 * 1000^1 + 345 * 1000^0 + 0 * 1000^-1 + 6 * 1000^-2
 *                           = 12000 + 345 + 0 + 0.006 = 12345.006
 * scale = 3                 (仅用于输出格式控制，表示显示时小数点后保留几位)
 */
typedef struct { 
    unsigned int *digits; // 动态数组指针，存储 "大数" 的每一位。
                          // 每一 "位" 实际上是一个 0 到 BASE-1 之间的整数。
                          // 为了方便计算，我们使用小端序或大端序并不重要，正如这里使用的是
                          // "高位在低索引" (Big-Endian-ish for the array)，即 digits[0] 是最高有效位。

    int ndigits;          // digits 数组的长度 (元素个数)。
                          // 如果 ndigits <= 0，通常表示该数为 0。

    int weight;           // 权重新，表示最高有效位 (digits[0]) 相对于小数点的偏移量 (以 BASE 为底)。
                          // weight >= 0 表示整数部分，weight < 0 表示小数部分。
                          // 举例: 对于 12345 (BASE=10), digits=[1,2,3,4,5], digits[0]=1, 它是 10^4 位，所以 weight=4。
                          // 本计算器中，digits[i] 的实际权重是 weight - i。

    int sign;             // 符号位: 
                          //  1 表示正数
                          // -1 表示负数
                          //  0 表示数值为零 (此时 digits 可能为 NULL 或 ndigits=0)

    int scale;            // 目标小数精度 (仅用于最终打印输出)。
                          // 内部运算（加减乘除）主要依赖 weight 和 ndigits 维护精度，
                          // scale 更多是作为“用户期望的输出精度”或“运算结果的截断建议”。
} NumericVar;

// 释放 NumericVar 关联的内存
// 在进行频繁的大数运算时，内存管理至关重要。
// 为避免内存泄漏，每次使用完 NumericVar 必须调用此函数。
static void free_var(NumericVar *var);

// 为 NumericVar 的数字数组分配内存
// 这是内存分配的底层封装，包含了对 malloc/calloc 的错误检查。
// 如果分配失败（如内存耗尽），返回 false。
static bool alloc_var_digits(NumericVar *var, int ndigits);

// 深拷贝 NumericVar (从 src 到 dst)
// 大数运算中经常需要保存中间结果或进行非破坏性操作，
// 因此需要深拷贝函数，完整复制 digits 数组的内容，而不仅仅是复制指针。
static bool copy_var(const NumericVar *src, NumericVar *dst);

// 计算最低有效位的权重
// 这是一个辅助函数，用于快速确定数字的“最右端”在数轴上的位置。
// 公式: low_weight = weight - (ndigits - 1)
// 例如: 12.34 (BASE=10), digits=[1,2,3,4], weight=1 (1对应10^1).
// ndigits=4. low_weight = 1 - (4-1) = -2 (4对应10^-2).
static int var_low_weight(const NumericVar *v);

// 获取指定权重索引处的数字
// 这是一个非常重要的抽象接口，它允许我们将大数视为一个无限长的数字序列。
// 如果请求的权重 w 在 digits 数组范围内，返回对应数字；
// 如果超出范围（例如请求更高的高位或更低的低位，即前导/尾随零区域），直接返回 0。
// 这极大简化了加减法对齐逻辑。
static unsigned int digit_at_weight(const NumericVar *v, int w);

// 移除 NumericVar 的前导零和尾随零
// 大数运算后（如减法或乘法）常会产生无效的零，
// 例如 1.5 - 1.4 = 0.1 (原算式可能产生 0.10000...)
// strip_var 会重新调整 digits 数组的大小和 weight，保持数据的紧凑性，节省内存并提高后续运算速度。
static void strip_var(NumericVar *v);

// 比较两个 NumericVar 的绝对值
// 返回值: 
//  1 表示 |a| > |b|
// -1 表示 |a| < |b|
//  0 表示 |a| == |b|
// 用于决定减法时的操作顺序（大减小）以及除法时的试商逻辑。
static int cmp_abs(const NumericVar *a, const NumericVar *b);

// 绝对值相加: res = |var1| + |var2|
// 这是底层加法核心，不考虑符号。
// 算法思路: 对齐最低位，从低位向高位逐位相加，处理进位 (Carry)。
static bool add_abs(const NumericVar *var1, const NumericVar *var2, NumericVar *res);

// 绝对值相减: res = |var1| - |var2| (要求 |var1| >= |var2|)
// 这是底层减法核心，不考虑符号。
// 调用前必须保证 |var1| >= |var2|，否则结果无定义（本系统不支持保存负的digits）。
// 算法思路: 对齐最低位，从低位向高位逐位相减，处理借位 (Borrow)。
static bool sub_abs(const NumericVar *var1, const NumericVar *var2, NumericVar *res);

// 通用加法: res = var1 + var2 (处理符号)
// 这是对外暴露的加法接口。
// 它会根据操作数的符号决定是调用底层加法 (同号相加) 还是底层减法 (异号相减)。
// 例如: (+5) + (-3) 实际上是 compute |5| - |3|，结果符号取绝对值较大者的符号。
static bool add_var(const NumericVar *var1, const NumericVar *var2, NumericVar *res);

// 通用乘法: res = var1 * var2
// 实现了经典的长乘法 (Long Multiplication)，时间复杂度 O(N*M)。
// 类似于我们在纸上手算的乘法：逐位乘、移位、累加。
// 虽然 Karatsuba 算法更快 O(N^1.58)，但对于计算器的典型输入规模，经典算法简单且足够快。
static bool mul_var(const NumericVar *var1, const NumericVar *var2, NumericVar *res);

// NumericVar 乘以单个数字: res = var * digit
// 乘法的一个特例优化，用于除法中的试商乘积计算。
// 例如计算 12345 * 7，不需要完整的长乘法开销。
static bool mul_var_digit(const NumericVar *var, unsigned int digit, NumericVar *res);

// 原地绝对值相减: a = |a| - |b|
// 为了减少内存分配，在某些算法（如除法）中需要原地修改操作数。
static bool sub_abs_inplace(NumericVar *a, const NumericVar *b);

// 整数除法 (向下取整): quot = var1 / var2, rem = var1 % var2
// 计算器中最复杂的部分。实现了 Knuth 的 Algorithm D (The Art of Computer Programming, Vol 2)。
// 这是一个能够处理长整数除法的标准算法，包含 归一化 -> 估商 -> 修正 -> 去归一化 等步骤。
static bool div_var(const NumericVar *var1, const NumericVar *var2, NumericVar *quot, NumericVar *rem);

// 仅保留商的整数除法 (截断余数)
// div_var 的简单包装，只关心商。
static bool div_var_trunc(const NumericVar *a, const NumericVar *b, NumericVar *q);

// 使用牛顿法计算高精度平方根
// 算法: x_{n+1} = (x_n + a / x_n) / 2
// 牛顿迭代法具有二次收敛速度，非常适合高精度开方。
// 此函数包含对小数点的处理：通过放大数值转化为整数开方，然后再缩小。
static bool sqrt_scaled_var(const NumericVar *in, int target_scale, NumericVar *out, char *err, size_t err_size);

// 从 32 位无符号整数创建 NumericVar
// 便捷函数，用于将 C 语言原生类型转换为大数对象。
static bool make_u32_var(unsigned int value, NumericVar *v);

// 估算 NumericVar 的十进制位数
// 用于输出格式化前的缓冲区计算。
static int decimal_digits_estimate(const NumericVar *v);

// 原地乘以 10 的幂: v = v * 10^pow10
// 这是一个位移操作。由于 BASE=1000，乘以 10^k 需要精细处理：
// 大部分可以通过调整 digits 内容 (乘以 10, 100) 和调整 weight 来完成，而不是真的做乘法。
static bool mul_var_pow10_inplace(NumericVar *v, int pow10);

// 计算整数幂: res = base ^ exp
// 使用“二进制幂”(Binary Exponentiation / Exponentiation by Squaring) 算法。
// 时间复杂度 O(log exp)，远快于循环相乘。
static bool pow_var_u32(const NumericVar *base, unsigned int exp, NumericVar *res);

// 解析科学计数法字符串的指数部分
// 辅助解析 "1.23E+5" 中的 "+5"。
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
// 修改输入的字符串 buffer，将首尾空格截断。
static char *trim_in_place(char *s);

// 将表达式字符串分割为左操作数、运算符和右操作数
// 简单的词法分析器，寻找主操作符 split 点。
static bool split_expression(char *input, char **lhs, char *op, char **rhs);

// 检查输入字符串是否为 quit/exit 命令
static bool is_quit_command(const char *s);

// 解析一元表达式 (例如 "sqrt(2)")
static bool parse_unary_expression(char *input, bool *is_sqrt, char **arg, char *err, size_t err_size);

// 将 NumericVar 转换为 32 位无符号整数 (如果适合)
// 用于检查指数是否过大，因为 pow 操作的指数通常是基本整数。
static bool var_to_u32(const NumericVar *v, unsigned int *out);

// 将 NumericVar 转换为指定小数位数的字符串
// 这是输出的出口。将内部的 BASE 进制数组转换回人类可读的十进制字符串。
// 需要处理四舍五入(虽然这里暂时是截断或精确输出)、补零、小数点位置等。
static bool var_to_string_scaled(const NumericVar *v, int scale, char *buf, size_t buf_size);

// 对齐两个 NumericVar 的小数位 (用于加/减法)
// 浮点数加减法必须对齐小数点。
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

/*
 * 函数: free_var
 * 功能: 释放 NumericVar 结构体中动态分配的 digits 数组内存。
 * 参数: var - 指向需要释放的 NumericVar 的指针。
 * 细节: 会将指针置为 NULL 并将相关属性清零，防止悬空指针（Double Free）或误用。
 */
static void free_var(NumericVar *var) { 
    if (!var) return; 
    free(var->digits); 
    var->digits = NULL; 
    var->ndigits = 0; 
    var->weight = 0; 
    var->sign = 0; 
    var->scale = 0; 
} 

/*
 * 函数: alloc_var_digits
 * 功能: 为 NumericVar 分配指定长度的数字数组。
 * 参数: 
 *   var     - 目标对象
 *   ndigits - 需要分配的 "位" 数 (注意是 BASE 进制的位，不是十进制位)
 * 返回: 成功返回 true，失败返回 false。
 * 技巧: 使用 calloc 而不是 malloc，确保分配的内存初始化为 0，这对于大数运算的逻辑正确性很重要。
 */
static bool alloc_var_digits(NumericVar *var, int ndigits) { 
    if (!var || ndigits <= 0) return false; 
    // 安全检查，防止整数溢出攻击 (虽然 ndigits 也是 int)
    var->digits = (unsigned int *)calloc((size_t)ndigits, sizeof(unsigned int)); 
    if (!var->digits) return false; 
    var->ndigits = ndigits; 
    return true; 
} 

/*
 * 函数: copy_var
 * 功能: 实现 NumericVar 的深拷贝。
 * 原理: 
 *   C 语言的结构体默认赋值是浅拷贝 (Bitwise copy)。
 *   对于包含指针的结构体，浅拷贝会导致两个对象指向同一块堆内存。
 *   当其中一个对象被 free 后，另一个对象就变成了悬垂指针 (Dangling Pointer)。
 *   因此必须手动分配新内存，并使用 memcpy 复制内容。
 */
static bool copy_var(const NumericVar *src, NumericVar *dst) { 
    if (!src || !dst) return false; 
    // 防止自身赋值，也先清理目标对象
    free_var(dst); 
    
    // 如果源对象是零或空，直接返回零对象
    if (src->ndigits <= 0 || !src->digits || src->sign == 0) { 
        *dst = (NumericVar){0}; 
        return true; 
    } 

    if (!alloc_var_digits(dst, src->ndigits)) return false; 
    
    memcpy(dst->digits, src->digits, (size_t)src->ndigits * sizeof(unsigned int)); 
    
    dst->weight = src->weight; 
    dst->sign = src->sign; 
    dst->scale = src->scale; 
    return true; 
} 

static int var_low_weight(const NumericVar *v) { 
    return v->weight - (v->ndigits - 1); 
} 

/*
 * 函数: digit_at_weight
 * 功能: 获取大数在特定权重位置的数值。
 * 核心思想: 抽象层。
 *   无需关心数组越界问题。在数字处理中，任何数字如果补上前导零和尾随零，
 *   在数学上是不改变值的。
 *   输入 weight，自动计算对应的数组下标。如果下标越界，说明该位是虚拟的 0。
 * 公式推导:
 *   digits[0] 的权重是 v->weight.
 *   digits[i] 的权重是 v->weight - i.
 *   我们要求权重 w 对应的 digits[idx], 即: v->weight - idx = w  =>  idx = v->weight - w.
 */
static unsigned int digit_at_weight(const NumericVar *v, int w) { 
    if (!v || !v->digits || v->ndigits <= 0 || v->sign == 0) return 0U; 
    int idx = v->weight - w; 
    if (idx < 0 || idx >= v->ndigits) return 0U; 
    return v->digits[idx]; 
} 

/*
 * 函数: strip_var
 * 功能: 规范化 (Normalization)。移除无意义的零，保持数值表示的唯一性和紧凑性。
 * 用例:
 *   加法: 99 + 1 = 100 (增长，strip 不会移除，因为非零)
 *   减法: 123 - 120 = 003 -> 3 (ndigits 从 3 缩减为 1)
 *   乘法: 10 * 0.1 = 01.0 -> 1
 * 步骤:
 *   1. 扫描头部 (digits[0] 方向) 的零，记录个数 first。
 *   2. 扫描尾部 (digits[N-1] 方向) 的零，记录个数，计算有效长度。
 *   3. 如果有头部零，需要调整 digits 指针或移动内存，并减少 weight。
 *      (例如 00123, 1的权重变了，或者可以说如果原weight对应第一个0，现在对应1，weight应该减小)
 *      Wait, digits[0] 是最高位。如果 digits[0] 是 0，比如 012，weight 是 2 (100位)。
 *      去掉0后变成 12，digits'[0]=1, weight 应该是 1 (10位)。所以 weight -= first 是正确的。
 */
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

    // 重新分配（或移动）内存
    // 注意：这里分配新内存比 realloc 更安全，因为如果是为了去除头部零，
    // realloc 不支持“原地头部收缩”。
    unsigned int *new_digits = (unsigned int *)malloc((size_t)new_ndigits * sizeof(unsigned int)); 
    if (!new_digits) return; // 极端情况：分配失败则保持原样，不算致命错误

    // 复制有效数据段
    memcpy(new_digits, v->digits + first, (size_t)new_ndigits * sizeof(unsigned int)); 
    free(v->digits); 
    v->digits = new_digits; 
    
    // 关键：权重的调整
    // 去掉前导零意味着最高有效位变低了。
    v->weight -= first; 
    v->ndigits = new_ndigits; 
    
    // 如果结果是 0 (虽然上面处理了全零，但防止副作用)，符号置 0
    if (v->ndigits == 1 && v->digits[0] == 0U) v->sign = 0; 
} 

/*
 * 函数: cmp_abs
 * 功能: 比较绝对值大小。
 * 逻辑:
 *   1. 先比较最高位的权重 (weight)。权重大的数一定大 (比如 100 > 99)。
 *   2. 如果权重相同，说明数量级相同，需要从高位到低位逐位比较 (Lexicographical Compare)。
 *   注意：这里比较区间是从 max(weight_a, weight_b) 到 min(low_weight_a, low_weight_b)。
 *   借助 digit_at_weight 自动补零的特性，我们可以像比较对齐后的字符串一样比较它们。
 */
static int cmp_abs(const NumericVar *a, const NumericVar *b) { 
    if (!a || a->sign == 0 || a->ndigits <= 0 || !a->digits) { 
        if (!b || b->sign == 0 || b->ndigits <= 0 || !b->digits) return 0; 
        return -1; // 0 < 非0
    } 
    if (!b || b->sign == 0 || b->ndigits <= 0 || !b->digits) return 1; // 非0 > 0

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

/*
 * 函数: add_abs
 * 功能: 核心加法算法 (高精度 + 高精度)。
 * 算法详解:
 *   该函数模拟了手工加法的过程。
 *   输入的两个数可能有不同的小数点位置 (即不同的 weight)。
 *   
 *   1. 对齐范围计算:
 *      结果的最高位权重至多是 max(weight1, weight2) + 1 (因为可能进位)。
 *      结果的最低位权重是 min(low1, low2)。
 *   
 *   2. 内存分配:
 *      根据计算出的范围分配足够的 digits 数组。
 * 
 *   3. 逐位相加:
 *      从最低位 (low_w) 到 最高位 (top_w) 进行循环。
 *      sum = digit1 + digit2 + carry
 *      digit_res = sum % BASE
 *      carry = sum / BASE
 * 
 *   4. 后处理:
 *      strip_var 去除可能不需要的前导零 (如果预估的 +1 进位没发生)。
 */
static bool add_abs(const NumericVar *var1, const NumericVar *var2, NumericVar *res) { 
    if (!var1 || !var2 || !res) return false; 
    free_var(res); 

    // 优化：处理零值加法
    if (var1->sign == 0) { 
        if (!copy_var(var2, res)) return false; 
        if (res->sign != 0) res->sign = 1; // 结果总是正的 (绝对值加法)
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
    // +2 是为了保险：+1 是因为最高位可能进位，再 +1 是为了包含 low_w 到 top_w 的所有位
    int res_ndigits = top_w - low_w + 2; 

    if (!alloc_var_digits(res, res_ndigits)) return false; 
    res->weight = top_w + 1; // 假设发生了进位
    res->sign = 1; 

    int carry = 0; 
    // 这里使用 i 作为 res->digits 的索引，i=0 对应最高位 (res->weight)
    // 所以这里的便利方向是从数组末尾 (最低权重) 到 头部 (最高权重)
    for (int i = res_ndigits - 1; i >= 0; i--) { 
        int target_w = res->weight - i; 
        
        // 核心加法: 获取两个数在当前权重位的数值 (若无则为0) 并加上进位
        int sum = carry + (int)digit_at_weight(var1, target_w) + (int)digit_at_weight(var2, target_w); 
        
        if (sum >= BASE) { 
            sum -= BASE; 
            carry = 1; 
        } else { 
            carry = 0; 
        } 
        res->digits[i] = (unsigned int)sum; 
    } 

    // 规范化：去除可能多分配的高位零
    strip_var(res); 
    if (res->ndigits > 0) res->sign = 1; 
    return true; 
} 

/*
 * 函数: sub_abs
 * 功能: 核心减法算法 (|var1| - |var2|)。
 * 前置条件: 必须保证 |var1| >= |var2|，由调用者 (add_var) 保证。
 * 算法详解:
 *   模拟手工借位减法。
 *   digit_res = digit1 - digit2 - borrow
 *   如果 digit_res < 0，则向高位借 1 (即 + BASE)，borrow = 1。
 *   否则 borrow = 0。
 */
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

/*
 * 函数: add_var
 * 功能: 通用的带符号加法接口。
 * 逻辑:
 *   处理符号规则，将任务分发给 add_abs (同号) 或 sub_abs (异号)。
 *   规则表:
 *    (+A) + (+B) = +( |A| + |B| )
 *    (-A) + (-B) = -( |A| + |B| )
 *    (+A) + (-B) = (+A) - (+B) = sign(|A|-|B|) * ||A|-|B||
 *    (-A) + (+B) = (+B) - (+A) = sign(|B|-|A|) * ||B|-|A||
 */
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
    *res = tmp; // 转移所有权，无需再 copy，因为 tmp 是局部变量
    return true; 
} 

/*
 * 函数: mul_var
 * 功能: 通用乘法 (O(N*M))。
 * 算法详解:
 *   模拟竖式乘法。
 *   var1 有 N 位，var2 有 M 位。结果最多 N+M+1 位。
 *   weight = weight1 + weight2 + 1。
 * 
 *   核心循环:
 *   for i in var1_digits:
 *     for j in var2_digits:
 *        res[i+j+1] += digits1[i] * digits2[j]
 *        处理进位...
 * 
 *   这里使用的是 "累加法"，即每次乘积直接加到结果数组的对应位置上，并立即处理进位。
 *   这比先计算所有中间乘积再相加更省内存。
 */
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
        
        if (dig1 == 0U) continue; // 优化：如果当前位是0，跳过内层循环，这对稀疏数字非常有效

        for (int j = var2->ndigits - 1; j >= 0; j--) { 
            uint32_t dig2 = (uint32_t)var2->digits[j]; 
            // 计算结果在 digits 数组中的位置
            // 索引计算是整个长乘法中最容易出错的地方。
            // 我们的 digits 是 big-endian 顺序的。
            uint32_t idx = (uint32_t)(i + j + 1); 
            
            // 核心乘法累加: 当前位 = 旧值 + 乘积 + 进位
            // 注意这里 sum 可能很大，但不会超过 uint32 范围
            // BASE 是 1000，BASE-1 = 999
            // max sum = 999 + 999*999 + carry ≈ 1000000，在 uint32 (40亿) 范围内很安全
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

/*
 * 函数: mul_var_digit
 * 功能: 大数乘单精度数 (NumericVar * uint)。
 * 场景: 这是一个基础操作，经常用于：
 *   1. 减法借位时的临时计算
 *   2. 除法中 "商 * 除数" 的试乘步骤
 * 性能: O(N)，比通用 mul_var 快得多。
 */
/*
 * 函数: mul_var_digit
 * 功能: 高精度数 * 单精度整数 (Vector * Scalar)。
 * 核心逻辑:
 *   这比通用的 mul_var (Vector * Vector) 快得多，复杂度仅为 O(N)。
 *   主要用于除法中的 "试商 * 除数" 步骤，或者简单的缩放操作。
 */
static bool mul_var_digit(const NumericVar *var, unsigned int digit, NumericVar *res) { 
    if (!var || !res) return false; 
    free_var(res); 

    // 0 乘任何数得 0
    if (var->sign == 0 || digit == 0U) { 
        *res = (NumericVar){0}; 
        return true; 
    } 

    // 分配内存：结果最多比原数多 1 位 (例如 99 * 9 = 891)
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

/*
 * 函数: sub_abs_inplace
 * 功能: 原地绝对值减法: a = |a| - |b|。
 * 前提: 调用前必须保证 |a| >= |b|，否则结果无意义。
 * 用途: 除法中 "被除数 - 商*除数" 的步骤，原地操作避免频繁 malloc。
 */
static bool sub_abs_inplace(NumericVar *a, const NumericVar *b) { 
    NumericVar tmp = {0}; 
    // 目前复用了非原地的 sub_abs，虽然不是最优解(多了一次 alloc/free)，
    // 但保证了正确性。真正的原地减法需要重写 sub_abs 逻辑以支持重叠内存。
    if (!sub_abs(a, b, &tmp)) return false; 
    free_var(a); 
    *a = tmp; 
    return true; 
} 

/*
 * 函数: div_var
 * 功能: 高精度除法 (带余数)。计算 quot = var1 / var2, rem = var1 % var2。
 * 算法: Knuth's Algorithm D (The Art of Computer Programming, Vol 2, 4.3.1)。
 * 
 * 核心逻辑详解:
 *   类似于我们手算的 "长除法"，但基数是 BASE (1000) 而不是 10。
 *   步骤:
 *   1. 归一化 (Normalization): 将除数和被除数左移，使得除数最高位 >= BASE/2。
 *      (虽然这里 BASE=1000，也可以不做严格移位，但如果不归一化，估商 (qhat) 会很不准)。
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

    // shifted_div 代表 "对齐" 后的除数
    NumericVar shifted_div = divisor; 

    // 主循环: 从高位向低位试商
    for (int shift = qweight; shift >= 0; shift--) { 
        // 将除数逻辑上左移 shift 位 (通过 weight 调整)
        shifted_div.weight = divisor.weight + shift; 

        // 如果 当前剩余被除数 < 当前移位后的除数，说明这一位的商是 0
        if (cmp_abs(&dividend, &shifted_div) < 0) continue; 

        /* --- Knuth Algorithm D Step D3: Calculate qhat (估商) --- */
        // 我们取被除数最高的 2 位 (u0, u1) 和 除数最高的 1 位 (v0)
        // 估算 qhat = (u0 * BASE + u1) / v0
        unsigned int rem_hi1 = digit_at_weight(&dividend, shifted_div.weight); 
        unsigned int rem_hi2 = digit_at_weight(&dividend, shifted_div.weight - 1); 
        unsigned int div_hi1 = digit_at_weight(&shifted_div, shifted_div.weight); 
        unsigned int qhat; 

        if (div_hi1 == 0U) { 
            // 理论上不可能，除非 divisor 为 0 (已检查)
            qhat = BASE - 1; 
        } else { 
            uint32_t numer = (uint32_t)rem_hi1 * BASE + rem_hi2; 
            qhat = numer / div_hi1; 
            if (qhat >= BASE) qhat = BASE - 1; // qhat 不能超过 BASE-1
            if (qhat == 0U) qhat = 1U; 
        } 

        // 此时 qhat 可能偏大。我们需要逐步调小 qhat，直到 qhat * divisor <= dividend
        // 这一步是 Algorithm D 中的 "while (qhat * v > ...)" 检查的简化版
        // 直接用乘法验证比复杂的高精度条件判断更直观
        while (qhat > 0U) { 
            if (!mul_var_digit(&shifted_div, qhat, &prod)) goto div_cleanup; 
            if (cmp_abs(&prod, &dividend) <= 0) break; // 找到了合法的 qhat
            qhat--; // 试商太大，减小重试
        } 

        // 优化: 二次试探 (Lookahead correction)
        // 有时候 qhat 可能是正确的，或者可以更大? (通常上面的 loop 已经保证了 <=)
        // 下面的逻辑是为了确保尽可能 "贪婪" 地匹配，防止 qhat 偏小
        // (虽然通常估商公式只会偏大，不会偏小。这一步主要是为了精确性兜底)
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
            
            // Step D4: Multiply & Subtract 
            // dividend -= qhat * shifted_divisor
            if (!sub_abs_inplace(&dividend, &prod)) goto div_cleanup; 
        } 
    } 

    strip_var(quot); 
    // 确定商的符号
    if (quot->sign != 0) quot->sign = (var1->sign == var2->sign) ? 1 : -1; 

    // 余数的符号与被除数一致
    if (dividend.sign != 0) dividend.sign = var1->sign; 
    *rem = dividend; // 剩余的 dividend 即为余数
    dividend = (NumericVar){0}; // 所有权转移
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

/*
 * 函数: div_var_trunc
 * 功能: 仅计算商 (截断除法)，忽略余数。
 */
static bool div_var_trunc(const NumericVar *a, const NumericVar *b, NumericVar *q) { 
    NumericVar rem = {0}; 
    free_var(q); 
    if (!div_var(a, b, q, &rem)) { 
        free_var(&rem); 
        return false; 
    } 
    q->scale = 0; // 截断除法通常不保留小数部分逻辑，这里假定是整数除法
    free_var(&rem); 
    return true; 
} 

/*
 * 函数: sqrt_scaled_var
 * 功能: 计算 sqrt(in)，保留 target_scale 位小数。
 * 算法: 牛顿迭代法 (Newton's Method)。
 * 原理:
 *   要求 sqrt(n) 精确到 k 位小数，等价于求整数 sqrt(n * 10^(2k))。
 *   令 N = n * 10^(2k)。我们需要求 X = floor(sqrt(N))。
 *   迭代公式: x_{next} = (x_{curr} + N / x_{curr}) / 2。
 */
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
        // (在整数算术中，牛顿迭代最终会在两个相邻整数间震荡或停滞)
        if (cmp_abs(&y, &x) >= 0) break;

        // 更新 x = y
        free_var(&x);
        x = y;
        y = (NumericVar){0}; // y 的内容已经移给 x
    }

    // 4. 调整 (Adjustment)
    // 牛顿法在整数除法下得到的 x 可能是 floor(sqrt(n)) 或者稍微差一点
    // 我们需要确保 x^2 <= n < (x+1)^2
    
    // 检查 x+1 是否更好? (虽然一般牛顿法是从上方逼近，通常不需要加)
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

    // 检查 x 是否偏大? (减法调整)
    // 确保 x^2 <= n
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

/*
 * 函数: make_u32_var
 * 功能: 将一个 uint32 创建为 NumericVar 对象。
 * 用途: 常数生成 (如 1, 2, 0 等) 或小测试用例。
 */
static bool make_u32_var(unsigned int value, NumericVar *v) { 
    if (!v) return false; 

    free_var(v); 
    // 特判 0
    if (value == 0U) { 
        *v = (NumericVar){0}; 
        return true; 
    } 
    // 限制 value < BASE 以保证它只占 1 个 digit 位
    // 如果需要更大的值，应使用 parse_string 或者更通用的 make_from_int
    if (value >= (unsigned int)BASE) return false; 
    
    if (!alloc_var_digits(v, 1)) return false; 
    v->digits[0] = value; 
    v->weight = 0; // 权重为0，表示个位
    v->sign = 1; 
    v->scale = 0; 
    return true; 
} 

/*
 * 函数: decimal_digits_estimate
 * 功能: 估算 NumericVar 的十进制位数。
 * 用途: 用于开平方根时估算初值 x0 的位数。
 * 逻辑:
 *   位数 ≈ weight * 3 + 最高位 digits 的十进制位数。
 *   BASE=1000，每个 weight 贡献 3 位。
 */
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

/*
 * 函数: mul_var_pow10_inplace
 * 功能: 原地乘以 10^pow10。
 * 用途: 处理小数点移动、科学计数法调整。
 * 优化:
 *   将 shift 分解为:
 *   1. 组位移 (group_shift = pow10 / 3): 直接移动内存，极快。
 *   2. 余数位移 (rem = pow10 % 3): 乘以 10 或 100。
 */
static bool mul_var_pow10_inplace(NumericVar *v, int pow10) { 
    if (!v) return false; 
    if (pow10 < 0) return false; // 不支持除法缩放，那是小数逻辑
    if (pow10 == 0 || v->sign == 0) return true; 

    int group_shift = pow10 / BASE_DIGITS; 
    int rem = pow10 % BASE_DIGITS; 

    // 第一步: 粗粒度位移 (移动 digits 数组)
    // 实际上对于 BASE 进制的大数，乘以 BASE^k 只需要增加 weight 即可，
    // 不需要移动 digits 数组的内容。
    // 原代码试图通过移动数组和增加 weight 来实现，但逻辑上有误（值未变）。
    if (group_shift > 0) { 
        v->weight += group_shift;
    } 

    // 第二步: 细粒度调整 (乘以 10 或 100)
    if (rem > 0) { 
        unsigned int factor = (rem == 1) ? 10U : 100U; 
        int old_sign = v->sign; 
        NumericVar tmp = {0}; 
        // 这一步是 O(N)，因为只是单精度乘法
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

/*
 * 函数: pow_var_u32
 * 功能: 计算 base ^ exp (整数指数)。
 * 实现: 简单的线性循环乘法 O(exp * M * N)。
 * 改进点: 如果 exp 很大，应该使用快速幂 (Binary Exponentiation) O(log exp)。
 *        但目前仅用于小指数场景，简单实现足够稳定。
 */
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

/*
 * 函数: parse_exponent_part
 * 功能: 解析科学计数法的指数部分 (如 "e+12" 或 "E-5")。
 */
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
 * 函数: parse_to_var
 * 功能: 将字符串解析为 NumericVar。
 * 支持格式: 整数 (123), 小数 (1.23), 科学计数法 (1.23e-5)。
 * 
 * 解析流程:
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
    // 1. Trim space
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

    // 计算实际的小数位权重调整值
    // frac_digits 是小数点后的有效数字个数
    // sci_exp 是 E 后面的指数值
    // 这里的 scale 实际上是 "小数点需要向左移动的位数" (正数表示向左移，即小数; 负数表示向右移，即整数末尾补零)
    int scale = frac_digits - sci_exp; 
    
    // 如果末尾有0且 scale > 0 (说明是小数末尾的无意义0), 去除之以简化
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


/*
 * 函数: is_supported_operator
 * 功能: 检查字符是否为合法操作符。
 * 支持: +, -, *, /, ^ (乘方), x/X (乘法别名)。
 */
static bool is_supported_operator(char op) { 
    return (op == '+' || op == '-' || op == '*' || op == '/' || op == '^' || op == 'x' || op == 'X'); 
} 

/*
 * 函数: trim_in_place
 * 功能: 原地去除字符串首尾空格。
 * 返回: 处理后的字符串起始指针。
 */
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

/*
 * 函数: split_expression
 * 功能: 将输入字符串 "A op B" 分割为 LHS, op, RHS。
 * 限制: 仅支持二元运算，不支持复杂的连算 (如 1+2*3)。
 * 核心逻辑:
 *   1. 扫描字符串，寻找操作符。
 *   2. 必须处理负号的歧义:
 *      - "1 - 2" 中的 - 是操作符。
 *      - "1e-5" 中的 - 是指数符号 (忽略)。
 *      - "-5 + 2" 中的 - 是 LHS 的一部分 (一元负号)。
 *   3. 如果找到多个操作符 (如 1+2+3)，则报错返回 false。
 */
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
    return (strcmp(s, "quit") == 0 || strcmp(s, "exit") == 0); 
} 

/*
 * 函数: parse_unary_expression
 * 功能: 解析 "sqrt(x)" 或 "square(x)" 这样的一元函数调用。
 */
static bool parse_unary_expression(char *input, bool *is_sqrt, char **arg, char *err, size_t err_size) { 
    if (!input || !is_sqrt || !arg) { 
        if (err) snprintf(err, err_size, "函数格式无效。"); 
        return false; 
    } 

    if (strncmp(input, "sqrt(", 5) == 0) { 
        *is_sqrt = true; 
    } else { 
        // 移除 square 支持
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

/*
 * 函数: var_to_u32
 * 功能: 将 NumericVar 转换为 uint32。
 * 用途: 从 NumericVar 获取指数值 (因为指数通常较小)。
 */
static bool var_to_u32(const NumericVar *v, unsigned int *out) { 
    if (!v || !out) return false; 
    if (v->sign < 0) return false; 
    if (v->sign == 0 || v->ndigits <= 0 || !v->digits) { 
        *out = 0U; 
        return true; 
    } 

    uint64_t value = 0U; 
    // 从高位到低位累加 (digits[0] 是最高位 group)
    // 但 digits[ndigits-1] 是最低位 group ??
    // 等等，parse_to_var 里 digits[0] 是最高位 group.
    // mul_var 里 digits 大端序.
    // 这里实现:
    for (int i = 0; i < v->ndigits; i++) { 
        value = value * (uint64_t)BASE + (uint64_t)v->digits[i]; 
        if (value > 100000U) return false; // 为了安全，限制在 100000 以内
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
 * 函数: var_to_string_scaled
 * 功能: 将 NumericVar 转换为字符串，并插入小数点。
 * 参数: scale - 要显示的小数位数 (通常等于 v->scale, 必须 >= 0)。
 * 逻辑:
 *   1. 先将所有 digits 打印到一个纯数字的临时 buffer (raw string)。
 *      注意补齐中间 group 的 '000' (如 12, 5 -> "12005")。
 *   2. 处理负号。
 *   3. 根据 scale 在 raw string 中插入小数点。
 *      如果 scale > raw_len，需要补前导 '0.00...'。
 *   4. 去除末尾多余的 '0' 和 '.'。
 */
static bool var_to_string_scaled(const NumericVar *v, int scale, char *buf, size_t buf_size) { 
    if (!v || !buf || buf_size == 0) return false; 

    // 特判 0
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

    // 美化: 去除末尾 0
    while (pos > 0U && buf[pos - 1U] == '0') pos--; 
    if (pos > 0U && buf[pos - 1U] == '.') pos--; // 如果剩下 "123."，去除点

    // 边界: -0 变成 0
    if (pos == 1U && buf[0] == '-') { 
        buf[0] = '0'; 
        pos = 1U; 
    } 
    buf[pos] = '\0'; 
    free(raw); 
    return true; 
} 

/*
 * 函数: align_scales
 * 功能: 对齐两个变量的小数位数 (scale)，取较大的 scale。
 * 实现: 较小 scale 的变量乘以 10^diff 进行放大。
 */
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
 * 函数: evaluate_expression
 * 功能: 执行二元运算。
 * 核心逻辑:
 *   1. 解析左右操作数。
 *   2.根据操作符分发:
 *     [+ / -] 加减法: 必须先对齐 scale (alignment)，然后进行整数加减。结果 scale = target_scale。
 *     [*] 乘法: 结果 scale = scale1 + scale2。
 *     [/] 除法: 
 *       设 A = lhs * 10^-Sa, B = rhs * 10^-Sb。
 *       目标结果 Q = (A/B) * 10^-St (St = target_scale = 18)。
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

            // 预缩放被除数和除数，以保证整数除法后的商具有 target_scale 位小数
            // 详见函数头注释推导
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

 


/*
 * 函数: calculate_unary_expression
 * 功能: 执行一元运算 (sqrt, square)。
 * 逻辑:
 *   1. 解析参数字符串为 NumericVar。
 *   2. 分发:
 *      - sqrt: 调用 sqrt_scaled_var (Newton迭代)。
 *      - square: 调用 mul_var(x, x)。标度变为 2*scale。
 *   3. 将结果转换为字符串输出。
 */
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
    // 移除 square 分支
 

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

/*
 * 函数: process_input
 * 功能: 处理单行输入。
 * 逻辑:
 *   1. 清理输入 (trim)。
 *   2. 检查是一元操作 (sqrt/square) 还是二元操作 (op)。
 *      如果是 unary，调用 calculate_unary_expression。
 *      如果是 binary，调用 split_expression 分割后调 evaluate_expression。
 *   3. 打印结果。
 */
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

    // 检测一元前缀: 仅支持 sqrt
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

/*
 * 函数: main
 * 功能: 程序入口。
 * 模式:
 *   1. 命令行参数模式: ./calculator 1 + 2
 *   2. 单参数模式: ./calculator "1 + 2"
 *   3. 交互 REPL 模式: ./calculator (进入循环)
 */
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

    // 帮助信息
    if (argc != 1) { 
        printf("用法: ./calculator <数字1> <操作符> <数字2>\n"); 
        printf("   或: ./calculator \"<表达式>\"\n"); 
        return 1; 
    } 

    // 模式 3: REPL 交互模式
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





