# SUSTech CS219 (Advanced Programming) Projects

> 🌐 [中文（default）](./README.md) · English

## 📚 Introduction

This repository contains the source code and reports for all four Projects of **CS219: Advanced Programming** at Southern University of Science and Technology (SUSTech), Spring 2026.

- **Semester**: 2026 Spring
- **Lecturer**: Prof. Shiqi Yu
- **Author**: Yushang Li (Student ID: 12412308)
- **Contents**: Full source code, LaTeX reports (`.tex`), compiled PDF reports, tests and performance plots for each Project.

> 📌 Starting from Spring 2025, the course code **CS205** was officially renamed to **CS219** with continuous course content. This repo records the four projects of the first CS219 cohort.

### Projects

| #   | Name                                       | Brief                                                                  | Report | Hours |
| --- | ------------------------------------------ | ---------------------------------------------------------------------- | ------ | ----- |
| 1   | [A Simple Calculator](./project1)          | Arbitrary-precision calculator in C: BASE-1000 digits + Knuth Alg. D + Newton sqrt | CN     | ~40 h |
| 2   | [Dot Product of Two Vectors](./project2)   | C vs Java dot-product benchmark; deep dive into JIT / SIMD / memory wall | CN     | ~20 h |
| 3   | [Matrix Multiplication](./project3)        | Hand-written AVX2 + FMA + OpenMP tiled matmul, benchmarked vs OpenBLAS | Code   | ~15 h |
| 4   | [Contributing to OpenCV](./project4)       | Real OpenCV bugfix: eliminate signed left-shift UB in drawing code    | CN     | ~15 h |

<img src="https://api.visitorbadge.io/api/visitors?path=https%3A%2F%2Fgithub.com%2Flys0204%2FCS219_26Spring&label=visitors&countColor=%2337d67a" alt="visitors"/>

---

## 💻 Project Details

### Project 1 · A Simple Calculator

📁 `project1/`: `calculator.c` (core library), `main.c` (REPL frontend), `report.tex`, `run_500_tests.py`, `test_calculator_comprehensive.py`

An **arbitrary-precision** calculator in pure C. The core `NumericVar` struct is inspired by CPython longs and PostgreSQL `numeric.c`, storing numbers in **BASE=1000** packed digits — precision is limited only by memory.

- **Parsing**: integers, decimals, scientific notation (`1e100`), whitespace tolerance.
- **Arithmetic**: align weights → carry/borrow; division implements **Knuth Algorithm D** (normalize + estimate + correct).
- **Square root**: scale by $10^{2k}$ to an integer, then **Newton-Raphson** iteration (quadratic convergence).
- **Power**: `^` operator (integer exponent).
- **Robustness**: friendly errors for div-by-zero / bad input; 500 random cases cross-validated against Python `decimal`.

### Project 2 · Dot Product of Two Vectors

📁 `project2/`: `dotproduct.c`, ` Dotproduct.java`, `report.tex`/`report.pdf`, 5 performance plots

On the surface, a speed comparison of C vs Java for $\sum a_i b_i$; in depth, **a dissection of modern computer architecture, compiler optimization, and hardware-software co-design**.

Five core comparisons: C vs JDK17, C no-opt vs `-O3`, JDK17 vs JDK26, JDK26 no-opt vs optimized (`parallel()` + `Math.fma`), C `-O3` vs JDK26.

Key engineering: PCG32 RNG (not `rand()`), data generated outside the timer, `clock_gettime(CLOCK_MONOTONIC)` instead of `clock()` (which inflates wall time by $N_{threads}$).

Core conclusions: compilation drives the performance curve; modern CPUs backstop bad code via L1 + out-of-order execution; the ultimate physical limit is the **memory wall**; data-type choice is a three-way tradeoff of precision vs compute vs bandwidth.

### Project 3 · Matrix Multiplication

📁 `project3/`: `Matrix.h`, `matmul.c`, `main.c`

Three single-precision (`float`) matrix multiplications benchmarked from $16^2$ to $32768^2$:

| Version          | Technique                                                                              |
| ---------------- | -------------------------------------------------------------------------------------- |
| `matmul_plain`   | Naive $O(N^3)$ baseline                                                                |
| `matmul_improved`| **AVX2 (`__m256`) + FMA (`_mm256_fmadd_ps`) + OpenMP tiling (BLOCK=64) + NUMA first-touch** |
| `matmul_openblas`| OpenBLAS `cblas_sgemm` as theoretical upper bound                                      |

Optimizations: 32-byte aligned allocation, 32-element inner unrolling (4× `__m256`), a small-matrix single-core SIMD fast path (≤64), and `collapse(2)+dynamic` three-level tiling for large sizes. Implementation notes are inline in `matmul.c`.

### Project 4 · Contributing to OpenCV

📁 `project4/`: `report_project4.tex`/`.pdf`, `test_28940.cpp`, `test_28940_minimal.cpp`, `bench_28940.cpp`, `bench_28940_negative.cpp`, `report.md`

Chose **Level C (real OpenCV issue)**, fixing [opencv/opencv#28940](https://github.com/opencv/opencv/issues/28940): a **C++ undefined behavior** in the drawing module under negative coordinates.

`cv::drawContours()` with a negative `offset` produces negative coordinates (legal input — shape partly outside the image, to be clipped). But `drawing.cpp` performed a signed left shift during 16.16 fixed-point conversion: `p0.x <<= XY_SHIFT - shift` (i.e. `-20 << 16` → UB). Works "by accident" on x86 release builds, but UBSan aborts.

**Fix**: a `ScaleToFixedPoint()` helper using multiplication instead of signed left shift — well-defined for negatives, and `v * (1<<k)` optimizes to equivalent cheap instructions in Release. Covers the whole drawing call chain (`ThickLine` → `FillConvexPoly` → `CollectPolyEdges`, 17 sites, +28/-18 lines).

Verified with a UBSan OpenCV build (before: `left shift of negative value -20`; after: clean, image drawn correctly), a regression test, and Release benchmarks showing 0%–2% noise with identical checksums.

> **Fork**: https://github.com/lys0204/opencv · **Branch**: `project4_qdB3ar` · **Commit**: `434bb82a8878`

---

## 🛠️ On the Use of AI

The four projects span one semester and chart a gradual shift in how I collaborate with AI:

- **P1**: Heavy reliance on GitHub Copilot (read source, write code, debug, generate tests).
- **P2**: AI as "explainer" — conclusions came from AI, then I verified, understood, and wrote them up.
- **P3**: Hand-written SIMD intrinsics; AI helped look up intrinsics and tiling strategies, core decisions were mine.
- **P4**: AI (CodeWhale / DeepSeek V4) acted as a "pair-programming senior engineer" — codebase analysis, fix design, benchmark scaffolding. Final decisions were mine.

From "someone who writes code" toward "software engineer."

---

## 📝 On the Reports

Reports are written in LaTeX (`ctexart` / `article` + `ctex`); compile on [Overleaf](https://www.overleaf.com) or locally with `xelatex`. Each project folder provides both `.tex` source and compiled `.pdf`.

## 🔗 References

- [Shiqi Yu / CPP — official course repo](https://github.com/ShiqiYu/CPP)
- [BrightonXX / SUSTech-CPP-Project — 2025 Spring (style reference for this README)](https://github.com/BrightonXX/SUSTech-CPP-Project)
- [HaibinLai / CS205-CPP-Programing-Project — 2024 Spring](https://github.com/HaibinLai/CS205-CPP-Programing-Project)
- [Maystern / SUSTech_CS205_Cpp_Projects](https://github.com/Maystern/SUSTech_CS205_Cpp_Projects)
- [YanWQ-monad / SUSTech_CS205_Projects — 2022 Fall](https://github.com/YanWQ-monad/SUSTech_CS205_Projects)

---

> If this repo helps you, a ⭐ Star is appreciated!
