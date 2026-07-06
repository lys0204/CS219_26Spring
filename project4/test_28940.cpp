// Reproducer for OpenCV issue #28940.
//
// Build this against an OpenCV build compiled with UBSan, then run it:
//   g++ -std=c++11 -fsanitize=undefined -fno-sanitize-recover=undefined \
//       test_28940.cpp -o /tmp/test_28940 \
//       $(pkg-config --cflags --libs opencv4)
//   /tmp/test_28940
//
// The input is valid: a contour in original-image coordinates is drawn into a
// cropped ROI image using a negative offset. The contour is partly outside the
// ROI and should simply be clipped.

#include <climits>
#include <iostream>
#include <vector>

#include <opencv2/imgproc.hpp>

int main()
{
    cv::Mat roi(80, 80, CV_8UC3, cv::Scalar::all(0));

    std::vector<std::vector<cv::Point> > contours(1);
    contours[0].push_back(cv::Point(100, 100));
    contours[0].push_back(cv::Point(160, 100));
    contours[0].push_back(cv::Point(160, 160));
    contours[0].push_back(cv::Point(100, 160));

    const cv::Point offset(-120, -120);

    std::cout << "Calling drawContours with offset " << offset
              << ". The first point becomes "
              << (contours[0][0] + offset) << std::endl;

    cv::drawContours(roi, contours, -1, cv::Scalar(0, 255, 0),
                     2, cv::LINE_8, cv::noArray(), INT_MAX, offset);

    std::cout << "drawContours finished, pixel sum = " << cv::sum(roi) << std::endl;
    return 0;
}
