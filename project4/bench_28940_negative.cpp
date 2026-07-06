// Local benchmark for the #28940 negative-offset drawContours path.
// This is not intended for the final PR.

#include <algorithm>
#include <chrono>
#include <climits>
#include <iostream>
#include <numeric>
#include <vector>

#include <opencv2/imgproc.hpp>

static std::vector<std::vector<cv::Point> > makeContours()
{
    std::vector<std::vector<cv::Point> > contours;
    contours.reserve(128);

    for (int y = 100; y < 540; y += 40)
    {
        for (int x = 100; x < 700; x += 40)
        {
            std::vector<cv::Point> contour;
            contour.push_back(cv::Point(x, y));
            contour.push_back(cv::Point(x + 24, y));
            contour.push_back(cv::Point(x + 24, y + 24));
            contour.push_back(cv::Point(x, y + 24));
            contours.push_back(contour);
        }
    }

    return contours;
}

static double runOnce(int iterations, const std::vector<std::vector<cv::Point> >& contours, double& checksum)
{
    cv::Mat image(480, 640, CV_8UC3, cv::Scalar::all(0));
    const cv::Point offset(-120, -120);

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i)
    {
        image.setTo(cv::Scalar::all(0));
        cv::drawContours(image, contours, -1, cv::Scalar(0, 255, 0),
                         2, cv::LINE_8, cv::noArray(), INT_MAX, offset);
    }
    const auto stop = std::chrono::steady_clock::now();

    const cv::Scalar s = cv::sum(image);
    checksum = s[0] + s[1] + s[2] + s[3];

    return std::chrono::duration<double, std::milli>(stop - start).count();
}

int main()
{
    const std::vector<std::vector<cv::Point> > contours = makeContours();
    const int counts[] = {100, 1000, 10000};
    const int repeats = 5;

    double warmupChecksum = 0.0;
    (void)runOnce(20, contours, warmupChecksum);

    for (int iterations : counts)
    {
        std::vector<double> times;
        times.reserve(repeats);

        double checksum = 0.0;
        for (int r = 0; r < repeats; ++r)
            times.push_back(runOnce(iterations, contours, checksum));

        const double best = *std::min_element(times.begin(), times.end());
        const double avg = std::accumulate(times.begin(), times.end(), 0.0) / times.size();

        std::cout << "iterations=" << iterations
                  << " best_ms=" << best
                  << " avg_ms=" << avg
                  << " checksum=" << checksum
                  << std::endl;
    }

    return 0;
}
