// Minimal standalone reproducer for the undefined behavior fixed by OpenCV
// issue #28940. This does not link OpenCV; it mirrors the relevant data flow:
// contour point + negative offset -> ThickLine fixed-point conversion.
//
// Build and run:
//   g++ -std=c++11 -fsanitize=undefined -fno-sanitize-recover=undefined \
//       test_28940_minimal.cpp -o /tmp/test_28940_minimal
//   /tmp/test_28940_minimal

#include <iostream>

struct Point
{
    int x;
    int y;
};

struct Point2l
{
    long long x;
    long long y;
};

static Point add(Point a, Point b)
{
    return Point{a.x + b.x, a.y + b.y};
}

static void ThickLineLike(Point2l p0, Point2l p1, int shift)
{
    const int XY_SHIFT = 16;

    std::cerr << "ThickLineLike input p0=(" << p0.x << ", " << p0.y
              << "), p1=(" << p1.x << ", " << p1.y
              << "), shift=" << shift << std::endl;

    // Same pattern as modules/imgproc/src/drawing.cpp::ThickLine().
    p0.x <<= XY_SHIFT - shift;
    p0.y <<= XY_SHIFT - shift;
    p1.x <<= XY_SHIFT - shift;
    p1.y <<= XY_SHIFT - shift;
}

int main()
{
    const Point contour0{100, 100};
    const Point contour1{160, 100};
    const Point offset{-120, -120};

    const Point shifted0 = add(contour0, offset);
    const Point shifted1 = add(contour1, offset);

    std::cerr << "contour0 + offset = (" << shifted0.x << ", " << shifted0.y << ")\n";
    std::cerr << "contour1 + offset = (" << shifted1.x << ", " << shifted1.y << ")\n";

    ThickLineLike(Point2l{shifted0.x, shifted0.y},
                  Point2l{shifted1.x, shifted1.y},
                  0);

    std::cerr << "finished\n";
    return 0;
}
