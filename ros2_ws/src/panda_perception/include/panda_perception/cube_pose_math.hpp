#ifndef PANDA_PERCEPTION__CUBE_POSE_MATH_HPP_
#define PANDA_PERCEPTION__CUBE_POSE_MATH_HPP_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <cv_bridge/cv_bridge.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sensor_msgs/msg/image.hpp>

// Pure, ROS-node-independent helpers factored out of cube_pose_estimator.cpp so they can be
// unit tested (see test/test_cube_pose_math.cpp) without a running ROS graph or Gazebo.
namespace panda_perception
{

// Reorders `values`; returns the middle element (matches the estimator's odd-sized sample use).
inline double median(std::vector<double> values)
{
  const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
  std::nth_element(values.begin(), middle, values.end());
  return *middle;
}

inline cv::Mat depth_as_meters(const sensor_msgs::msg::Image::ConstSharedPtr & message)
{
  const auto image = cv_bridge::toCvShare(message);
  if (message->encoding == sensor_msgs::image_encodings::TYPE_32FC1) {
    return image->image;
  }
  if (message->encoding == sensor_msgs::image_encodings::TYPE_16UC1) {
    cv::Mat meters;
    image->image.convertTo(meters, CV_32FC1, 0.001);
    return meters;
  }
  throw cv_bridge::Exception("Unsupported depth encoding: " + message->encoding);
}

inline cv::Mat make_depth_colormap(const cv::Mat & depth)
{
  cv::Mat normalized(depth.size(), CV_8UC1, cv::Scalar(0));
  cv::Mat valid(depth.size(), CV_8UC1, cv::Scalar(0));
  for (int row = 0; row < depth.rows; ++row) {
    const float * source = depth.ptr<float>(row);
    uint8_t * target = normalized.ptr<uint8_t>(row);
    uint8_t * valid_row = valid.ptr<uint8_t>(row);
    for (int column = 0; column < depth.cols; ++column) {
      const float value = source[column];
      if (std::isfinite(value) && value >= 0.10F && value <= 3.0F) {
        const float scaled = std::clamp((2.0F - value) / 1.7F, 0.0F, 1.0F);
        target[column] = static_cast<uint8_t>(scaled * 255.0F);
        valid_row[column] = 255;
      }
    }
  }

  cv::Mat colorized;
  cv::applyColorMap(normalized, colorized, cv::COLORMAP_TURBO);
  colorized.setTo(cv::Scalar(20, 20, 20), valid == 0);
  return colorized;
}

// Workspace bounds the estimator trusts; matches the table/pick-place envelope in world frame.
inline bool is_plausible_cube_position(double x, double y, double z)
{
  return x >= 0.05 && x <= 1.05 && y >= -0.50 && y <= 0.50 && z >= 0.73 && z <= 0.84;
}

inline bool is_depth_noise_acceptable(float median_absolute_deviation)
{
  return median_absolute_deviation <= 0.015F;
}

}  // namespace panda_perception

#endif  // PANDA_PERCEPTION__CUBE_POSE_MATH_HPP_
