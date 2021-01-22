#pragma once

#include <string>
#include <opencv2/core.hpp>

namespace bm {
namespace core {

typedef cv::Mat1b Image1b;
typedef cv::Mat3b Image3b;

// 32-bit floating point images
typedef cv::Mat1f Image1f;
typedef cv::Mat3f Image3f;

// 64-bit floating point images
typedef cv::Mat1d Image1d;
typedef cv::Mat3d Image3d;

}
}
