# SUSTech CS219(Advanced Programming 

> 🌐 中文（默认）

##  简介

本仓库收录了南方科技大学 CS219 春季学期的全部四个 Project 的源码与报告。

- **学期**：2026 Spring
- **授课教师**：Prof. Shiqi Yu
- **作者**：李语尚12412308

### Projects

| #    | 名称                                     | 简介                                                         | 大约用时 |
| ---- | ---------------------------------------- | ------------------------------------------------------------ | -------- |
| 1    | [A Simple Calculator](./project1)        | 用 C 语言实现高精度计算器：千进制压位存储 + Knuth Algorithm D + 牛顿开方 | ~40 h    |
| 2    | [Dot Product of Two Vectors](./project2) | C 与 Java 向量点积性能横评，剖析 JIT / SIMD / 内存墙         | ~20 h    |
| 3    | [Matrix Multiplication](./project3)      | 手写 AVX2 + FMA + OpenMP 分块矩阵乘，与 OpenBLAS 对标        | ~15 h    |
| 4    | [Contributing to OpenCV](./project4)     | 向 OpenCV 提交真实 bugfix：修复绘图模块 signed left-shift UB | ~15 h    |

---

##  四个 Project 详解

### Project 1 · A Simple Calculator（高精度计算器）

用纯 C 实现的**任意精度**计算器。核心数据结构 `NumericVar` 借鉴 CPython 长整型与 PostgreSQL `numeric.c`，采用 **BASE=1000 千进制压位**存储，理论上精度仅受内存限制。

- **输入解析**：支持整数、小数、科学计数法（`1e100`）、冗余空格容错。
- **加减乘除**：对齐位权 → 进/借位；除法实现 **Knuth Algorithm D**（归一化 + 估商 + 修正）。
- **开方**：将数字放大 $10^{2k}$ 转为整数后用**牛顿迭代法**，二次收敛。
- **乘方**：`^` 运算符（整数指数）。
- **鲁棒性**：除零、非法输入均有友好报错，500 组随机用例以 Python `decimal` 为标准答案交叉验证。

---

### Project 2 · Dot Product of Two Vectors（C vs Java 性能剖析）

表面是比较 C 与 Java 计算向量点积 $\sum a_i b_i$ 的速度，实则是**对现代计算机体系结构、编译器优化与软硬件协同的一场深度剖析**。

**5 组核心对比：**
1. C 无优化 vs JDK 17 无优化
2. C 无优化 vs C `-O3`
3. JDK 17 vs JDK 26
4. JDK 26 无优化 vs 优化方案（`parallel()` + `Math.fma`）
5. C `-O3` vs JDK 26

**关键工程细节：**
- C 端弃用 `rand()`，改用 **PCG32** 伪随机数生成器，避免低位周期性干扰 CPU 缓存与分支预测。
- 数据生成与内存分配全部在计时器外完成，避免 PRNG 耗时污染点乘数据。
- 选用 `clock_gettime(CLOCK_MONOTONIC)` 而非 `clock()`——后者在多线程下会把真实时间放大 $N_{threads}$ 倍。

**核心结论：**
- **编译机制主导性能曲线**：Java 小数据量低效 + 非线性跃升 = JVM 从解释执行到 JIT 的跨越期。
- **现代 CPU 为劣质代码兜底**：L1 缓存 + 超标量乱序执行，让未优化 C 没有被 JIT 机器码甩开几百倍。
- **终极物理底线是内存墙**：`-O3` 与高版本 JDK 用 AVX-512 飙升算力后，最终都堵在内存总线带宽上"众生平等"。
- **数据类型是精度/算力/带宽的三方博弈**：`float`/`double` 因缺乏结合律被迫放弃 SIMD；`short`/`char` 虽有符号扩展开销却降低了带宽压力，特定规模下反超 `int`。

---

### Project 3 · Matrix Multiplication（手写 SIMD 矩阵乘）

📁 `project3/`：`Matrix.h`、`matmul.c`、`main.c`

在 $16\times16$ 到 $32768\times32768$ 规模上，实现并对比三种单精度（`float`）矩阵乘法：

| 版本             | 技术                                                                                   |
| ---------------- | -------------------------------------------------------------------------------------- |
| `matmul_plain`   | 朴素三重循环 $O(N^3)$ 基线                                                             |
| `matmul_improved`| **AVX2（`__m256`）+ FMA（`_mm256_fmadd_ps`）+ OpenMP 分块（BLOCK=64）+ NUMA first-touch** |
| `matmul_openblas`| 调用 OpenBLAS `cblas_sgemm` 作为理论参考上限                                           |

**优化要点：**
- 32 字节对齐分配（`_mm_malloc`），保证 `_mm256_load_ps` 可用对齐加载。
- 内层循环 32 元素展开（4 个 `__m256`），最大化寄存器复用、减少循环开销。
- 极小规模（$\le 64$）走单核 SIMD 快速通道，规避 OpenMP 启动与分块开销。
- 大规模走 `collapse(2) + dynamic` 调度 + 三层分块（i/j/k），提升缓存命中。
- 用 PCG32 生成随机矩阵数据，`CLOCK_MONOTONIC` 精确计时。

---

### Project 4 · Contributing to OpenCV（真实开源贡献）

📁 `project4/`：`report_project4.tex` / `report_project4.pdf`、`test_28940.cpp`、`test_28940_minimal.cpp`、`bench_28940.cpp`、`bench_28940_negative.cpp`、`report.md`

选择 **Level C（解决真实 OpenCV Issue）**，修复了 [opencv/opencv#28940](https://github.com/opencv/opencv/issues/28940)：绘图模块在负坐标下的 **C++ undefined behavior**。

**问题根因：**
`cv::drawContours()` 传入负 `offset` 时，轮廓点会产生负坐标（合法输入，表示图形部分在图像外需裁剪）。但 `drawing.cpp` 在 16.16 定点坐标转换时对负的有符号整数执行左移：

```cpp
p0.x <<= XY_SHIFT - shift;   // -20 << 16  →  undefined behavior
```

Release 下 x86 补码"恰好能跑"，但 C++ 标准不保证，UBSan 会终止程序。

**修复方案：**
新增 helper 函数 `ScaleToFixedPoint()`，用**乘法缩放**替代 signed left shift——对负数定义良好，且 `v * (1<<k)` 在 Release 下被编译器优化为等价低成本指令。

```cpp
static inline int64 ScaleToFixedPoint(int64 v, int shift) {
    const int64 factor = static_cast<int64>(1) << (XY_SHIFT - shift);
    return v * factor;
}
```

修复覆盖整条绘图调用链：`ThickLine()` → `FillConvexPoly()` → `CollectPolyEdges()`（共 17 处 left shift，+28/-18 行）。

**验证：**
- 在 WSL 构建 UBSan 版 OpenCV（仅 `core`+`imgproc`），修复前 UBSan 报 `left shift of negative value -20`，修复后无报错且图像正常绘制。
- 新增回归测试 `drawContours_negative_offset_regression_28940`，覆盖可见区域与越界区域。
- Release 性能对比：正常坐标与负 offset 下 best/avg 差异均在 0%–2%，属于运行噪声，checksum 一致。

> **Fork**：https://github.com/lys0204/opencv · **Branch**：`project4_qdB3ar` · **Commit**：`434bb82a8878`





---

## 🔗 References

- [Shiqi Yu / CPP — 官方课程仓库](https://github.com/ShiqiYu/CPP)
- [BrightonXX / SUSTech-CPP-Project](https://github.com/BrightonXX/SUSTech-CPP-Project)
- [HaibinLai / CS205-CPP-Programing-Project — 2024 Spring](https://github.com/HaibinLai/CS205-CPP-Programing-Project)
- [Maystern / SUSTech_CS205_Cpp_Projects](https://github.com/Maystern/SUSTech_CS205_Cpp_Projects)
- [YanWQ-monad / SUSTech_CS205_Projects — 2022 Fall](https://github.com/YanWQ-monad/SUSTech_CS205_Projects)

---

> 如果这个仓库对你有帮助，欢迎点一个 ⭐ Star！ · If this repo helps you, a ⭐ Star is appreciated!
