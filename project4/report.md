# OpenCV #28940 修复报告：绘图负坐标左移导致 Undefined Behavior

## 1. 问题来源

本次修改针对 OpenCV issue [#28940](https://github.com/opencv/opencv/issues/28940)：

> `[UB] Left Shift of Negative Value in cv::ThickLine When Drawing Contours with Negative Offset`

问题发生在 `imgproc` 模块的绘图实现中，相关文件为：

- `modules/imgproc/src/drawing.cpp`
- `modules/imgproc/test/test_drawing.cpp`（后续正式 PR 应加入回归测试）

该问题的核心是：当用户调用 `cv::drawContours()` 或旧 C API `cvDrawContours()` 时，如果传入负的 `offset`，轮廓点可能被移动到图像左上边界外，形成负坐标。负坐标本身是合理输入，OpenCV 应该对图形进行裁剪并正常绘制。但当前实现会在内部 fixed-point 坐标转换时对负的有符号整数执行左移，触发 C++ undefined behavior。

## 2. 触发场景

一个现实场景是：用户从原图中裁剪出 ROI，然后希望把原图坐标系中的轮廓绘制到 ROI 图像中。

例如：

```cpp
cv::Mat roi(80, 80, CV_8UC3, cv::Scalar::all(0));

std::vector<std::vector<cv::Point>> contours = {
    {
        cv::Point(100, 100),
        cv::Point(160, 100),
        cv::Point(160, 160),
        cv::Point(100, 160)
    }
};

cv::Point offset(-120, -120);

cv::drawContours(roi, contours, -1, cv::Scalar(0, 255, 0),
                 2, cv::LINE_8, cv::noArray(), INT_MAX, offset);
```

其中第一个轮廓点：

```text
(100, 100) + (-120, -120) = (-20, -20)
```

这是合法且常见的输入，含义是轮廓有一部分落在 ROI 外面，OpenCV 应该裁剪后绘制可见部分。

## 3. 本地复现结果

我在 WSL 中构建了一个只包含 `opencv_core` 和 `opencv_imgproc` 的 UBSan 版本：

```bash
cmake -S . -B build_ubsan_28940 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_LIST=core,imgproc \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_TESTS=OFF \
  -DBUILD_PERF_TESTS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_opencv_apps=OFF \
  -DBUILD_JAVA=OFF \
  -DWITH_IPP=OFF \
  -DWITH_OPENCL=OFF \
  -DWITH_TBB=OFF \
  -DWITH_OPENMP=OFF \
  -DCMAKE_C_FLAGS="-fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"

cmake --build build_ubsan_28940 --target opencv_imgproc --parallel 8
```

然后用同一个复现程序 `test_28940.cpp` 调用 `cv::drawContours()`。

原始版本运行结果：

```text
Calling drawContours with offset [-120, -120]. The first point becomes [-20, -20]
/tmp/opencv_28940_orig/modules/imgproc/src/drawing.cpp:1662:10:
runtime error: left shift of negative value -20

#0 ThickLine
   /tmp/opencv_28940_orig/modules/imgproc/src/drawing.cpp:1662

#1 cv::drawContours(...)
   /tmp/opencv_28940_orig/modules/imgproc/src/drawing.cpp:2571

#2 main
   /tmp/test_28940_compare
```

说明原始 OpenCV 在合法负 offset 输入下，会在 `ThickLine()` 中触发 UBSan 错误。

## 4. 根因分析

`drawing.cpp` 中定义：

```cpp
enum { XY_SHIFT = 16, XY_ONE = 1 << XY_SHIFT };
```

OpenCV 绘图模块内部使用 fixed-point 坐标，因此会把普通坐标放大 `2^16` 倍。原代码在 `ThickLine()` 中使用了 signed 左移：

```cpp
p0.x <<= XY_SHIFT - shift;
p0.y <<= XY_SHIFT - shift;
p1.x <<= XY_SHIFT - shift;
p1.y <<= XY_SHIFT - shift;
```

当 `p0.x == -20` 时，该语句等价于：

```cpp
-20 << 16
```

根据 C++ 标准，对负的 signed integer 进行左移是 undefined behavior。普通 Release 构建下，这种代码在 x86 平台上可能“看起来可以运行”，因为机器使用补码表示；但 C++ 标准并不保证这种行为。UBSan 会在运行时检测到该问题并终止程序。

此外，第一次只修复 `ThickLine()` 后，同一个复现输入继续触发了 `FillConvexPoly()` 中的同类问题。原因是粗线绘制会将线段扩展成四边形，然后调用 `FillConvexPoly()` 填充。因此需要修复同一绘图路径上的所有同类 fixed-point 转换。

## 5. 修复方案

修复原则：

1. 不禁止负坐标。
2. 不把负坐标 clamp 到 0，因为这会改变图形形状。
3. 保持原有 fixed-point 缩放语义。
4. 避免对负 signed integer 执行左移。

新增局部 helper：

```cpp
static inline int64
ScaleToFixedPoint( int64 v, int shift )
{
    return v * (static_cast<int64>(1) << (XY_SHIFT - shift));
}
```

然后将原先的 signed 左移：

```cpp
p0.x <<= XY_SHIFT - shift;
```

替换为：

```cpp
p0.x = ScaleToFixedPoint(p0.x, shift);
```

该修改保持数学含义不变：

```text
x << 16 约等于 x * 65536
```

对于非负数，结果与原逻辑一致；对于负数，乘法是 C++ 定义良好的行为，不会触发负数左移 UB。

修复范围包括：

- `ThickLine()`
- `FillConvexPoly()`
- `CollectPolyEdges()`

这些函数都属于 `drawing.cpp` 内部绘图路径，均可能接收负坐标或由负 offset 产生的坐标。

## 6. 修复后验证

使用相同的 `test_28940.cpp`，分别加载原始 UBSan 库和修复后的 UBSan 库。

原始版本：

```text
ORIG_UBSAN_NEGATIVE
Calling drawContours with offset [-120, -120]. The first point becomes [-20, -20]
drawing.cpp:1662:10: runtime error: left shift of negative value -20
```

修复版本：

```text
FIXED_UBSAN_NEGATIVE
Calling drawContours with offset [-120, -120]. The first point becomes [-20, -20]
drawContours finished, pixel sum = [0, 61710, 0, 0]
```

说明修复后：

1. `drawContours()` 能完成绘制。
2. UBSan 不再报告 `left shift of negative value`。
3. 输出图像非空，像素和为 `[0, 61710, 0, 0]`。

## 7. 性能影响评估

为了确认修复不会明显影响正常使用性能，我分别构建了原始 Release 库和修复后 Release 库，并用同一个 benchmark 程序测试。

测试说明：

- 构建类型：Release
- 不启用 UBSan
- 模块：`core,imgproc`
- 测试 API：`cv::drawContours()`
- 每组重复 5 次，记录 best 和 avg
- 输出 checksum 保持一致，用于确认绘制结果相同

### 7.1 正常正坐标数据

测试文件：`bench_28940.cpp`

该测试使用正常正坐标轮廓，`offset=(0,0)`。

| 次数 | 原始 best ms | 修复 best ms | 差异 |
|---:|---:|---:|---:|
| 100 | 32.5576 | 31.8391 | -2.21% |
| 1000 | 327.375 | 321.646 | -1.75% |
| 10000 | 3228.03 | 3252.81 | +0.77% |

平均耗时：

| 次数 | 原始 avg ms | 修复 avg ms | 差异 |
|---:|---:|---:|---:|
| 100 | 32.8929 | 32.2869 | -1.84% |
| 1000 | 328.55 | 326.96 | -0.48% |
| 10000 | 3274.25 | 3268.45 | -0.18% |

输出校验值一致：

```text
checksum=1.19493e+07
```

### 7.2 负 offset 数据

测试文件：`bench_28940_negative.cpp`

该测试使用负 offset：

```cpp
cv::Point offset(-120, -120);
```

该路径对应原 bug 的触发场景。Release 原始库虽然能运行，但内部仍然含有 undefined behavior，因此这里只用于表面对比。

第二轮顺序测试结果：

| 次数 | 原始 best ms | 修复 best ms | 差异 |
|---:|---:|---:|---:|
| 100 | 30.3292 | 30.7858 | +1.51% |
| 1000 | 304.723 | 305.115 | +0.13% |
| 10000 | 2997.3 | 3033.59 | +1.21% |

平均耗时：

| 次数 | 原始 avg ms | 修复 avg ms | 差异 |
|---:|---:|---:|---:|
| 100 | 30.6651 | 31.3353 | +2.19% |
| 1000 | 307.935 | 309.081 | +0.37% |
| 10000 | 3023.27 | 3056.36 | +1.09% |

输出校验值一致：

```text
checksum=1.07391e+07
```

### 7.3 性能结论

修复后的性能与原始版本基本一致，差异大多在 `0%~2%` 范围内，属于正常运行噪声范围。

原因：

1. 修改只发生在少量几何点的 fixed-point 坐标转换处。
2. 绘图函数主要耗时在裁剪、扫描线填充和像素写入。
3. `v * (1 << k)` 是乘以 2 的幂，Release 编译器通常会优化为等价的低成本指令。

因此，本修复基本不影响性能。

## 8. 其他修复方案比较

### 方案 A：使用乘法缩放

```cpp
return v * (static_cast<int64>(1) << (XY_SHIFT - shift));
```

优点：

- 语义清楚：fixed-point 缩放。
- 对负数定义良好。
- 易于在 PR 中解释。
- 性能影响可以忽略。

### 方案 B：转为 unsigned 后左移

```cpp
return static_cast<int64>(static_cast<uint64_t>(v) << shift);
```

优点：

- 可以避免 signed negative left shift UB。
- 与底层二进制补码行为更接近。

缺点：

- unsigned 转回 signed 的语义在极端值上更容易引发维护者讨论。
- 可读性不如乘法。

### 方案 C：负坐标 clamp 到 0

不推荐。

原因是负坐标是合法输入，表示图形部分位于图像外。clamp 会改变几何形状，导致绘制结果不正确。

综上，当前采用方案 A。

## 9. OpenCV PR 要求与后续工作

根据 OpenCV 贡献指南：

1. 一个 PR 应该只解决一个问题。
2. 修复 bug 需要添加回归测试。
3. 不应提交临时复现文件或构建目录。
4. 代码应遵守 OpenCV 现有风格。
5. PR 描述中应说明复现、根因、修复和验证方式。

因此正式 PR 中不应包含：

- `test_28940.cpp`
- `test_28940_minimal.cpp`
- `bench_28940.cpp`
- `bench_28940_negative.cpp`
- `build_ubsan_28940/`
- `build_release_28940/`

正式 PR 应包含：

1. `modules/imgproc/src/drawing.cpp` 的修复。
2. `modules/imgproc/test/test_drawing.cpp` 中新增的 regression test。

建议 PR 标题：

```text
imgproc: avoid signed left-shift UB in drawing code
```

建议 PR 描述中包含：

```text
Fixes #28940
```

并说明：

- `drawContours()` 使用负 offset 时会将合法轮廓点移动到负坐标。
- 原绘图代码使用 signed left shift 进行 fixed-point 转换。
- 对负 signed integer 左移是 C++ undefined behavior。
- 本 PR 改为使用乘法进行 fixed-point 缩放，保持语义不变并避免 UB。

## 10. 总结

本次修改修复了 OpenCV `imgproc` 绘图模块在负坐标场景下的 C++ undefined behavior。

修复前：

```text
drawContours(..., offset=(-120,-120))
=> ThickLine()
=> p0.x <<= 16
=> left shift of negative value
=> UBSan 报错
```

修复后：

```text
drawContours(..., offset=(-120,-120))
=> fixed-point 坐标转换使用乘法缩放
=> 无 UBSan 错误
=> 图像正常绘制
```

该修复保持绘制结果一致，性能影响可以忽略，适合作为一个独立、真实、可测试的 OpenCV bug fix PR。
