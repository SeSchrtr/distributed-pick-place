#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cv_bridge/cv_bridge.hpp>
#include <geometry_msgs/msg/point_stamped.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/header.hpp>
#include <tf2/time.hpp>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>
#include <visualization_msgs/msg/marker.hpp>

#include "panda_perception/cube_pose_math.hpp"

using namespace std::chrono_literals;

class CubePoseEstimator : public rclcpp::Node
{
public:
  CubePoseEstimator()
  : Node("cube_pose_estimator"),
    tf_buffer_(get_clock()),
    tf_listener_(tf_buffer_)
  {
    const auto sensor_qos = rclcpp::SensorDataQoS();
    color_sub_ = create_subscription<sensor_msgs::msg::Image>(
      "/overhead_camera/image", sensor_qos,
      [this](sensor_msgs::msg::Image::ConstSharedPtr message) {
        sensor_msgs::msg::Image::ConstSharedPtr depth_message;
        {
          std::lock_guard<std::mutex> lock(data_mutex_);
          latest_color_ = std::move(message);
          depth_message = latest_depth_;
        }
        if (depth_message) {
          process_images(depth_message);
        }
      });
    info_sub_ = create_subscription<sensor_msgs::msg::CameraInfo>(
      "/overhead_camera/camera_info", sensor_qos,
      [this](sensor_msgs::msg::CameraInfo::ConstSharedPtr message) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        latest_info_ = std::move(message);
      });
    depth_sub_ = create_subscription<sensor_msgs::msg::Image>(
      "/overhead_camera/depth_image", sensor_qos,
      [this](sensor_msgs::msg::Image::ConstSharedPtr message) {
        {
          std::lock_guard<std::mutex> lock(data_mutex_);
          latest_depth_ = message;
        }
        process_images(message);
      });

    pose_pub_ = create_publisher<geometry_msgs::msg::PoseStamped>(
      "/perception/cube_pose", rclcpp::QoS(10).reliable());
    marker_pub_ = create_publisher<visualization_msgs::msg::Marker>(
      "/perception/cube_marker", rclcpp::QoS(10).reliable());
    depth_view_pub_ = create_publisher<sensor_msgs::msg::Image>(
      "/perception/depth_colormap", rclcpp::QoS(2).reliable());

    RCLCPP_INFO(
      get_logger(),
      "Estimating /perception/cube_pose from overhead RGB-D images; no Gazebo pose input is used");
  }

private:
  void publish_depth_view(
    const std_msgs::msg::Header & header, const cv::Mat & view,
    const std::vector<cv::Point> * contour = nullptr, const cv::Point * centroid = nullptr,
    float depth = 0.0F)
  {
    cv::Mat annotated = view.clone();
    if (contour != nullptr) {
      cv::polylines(annotated, *contour, true, cv::Scalar(40, 255, 40), 2, cv::LINE_AA);
    }
    if (centroid != nullptr) {
      cv::drawMarker(
        annotated, *centroid, cv::Scalar(255, 255, 255), cv::MARKER_CROSS, 18, 2,
        cv::LINE_AA);
      const std::string label = cv::format("cube %.3f m", depth);
      cv::putText(
        annotated, label, *centroid + cv::Point(12, -12), cv::FONT_HERSHEY_SIMPLEX,
        0.55, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
    }
    depth_view_pub_->publish(*cv_bridge::CvImage(header, "bgr8", annotated).toImageMsg());
  }

  void process_images(const sensor_msgs::msg::Image::ConstSharedPtr & depth_message)
  {
    sensor_msgs::msg::Image::ConstSharedPtr color_message;
    sensor_msgs::msg::CameraInfo::ConstSharedPtr info_message;
    {
      std::lock_guard<std::mutex> lock(data_mutex_);
      color_message = latest_color_;
      info_message = latest_info_;
    }
    if (!color_message || !info_message) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000, "Waiting for RGB image and CameraInfo");
      return;
    }

    try {
      const cv::Mat depth = panda_perception::depth_as_meters(depth_message);
      cv::Mat depth_view = panda_perception::make_depth_colormap(depth);
      const double image_age = std::abs(
        (rclcpp::Time(depth_message->header.stamp) -
        rclcpp::Time(color_message->header.stamp)).seconds());
      if (image_age > 0.25) {
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 3000,
          "RGB and depth frames are not synchronized (delta %.3f s)", image_age);
        publish_depth_view(depth_message->header, depth_view);
        return;
      }
      const cv::Mat color = cv_bridge::toCvShare(
        color_message, sensor_msgs::image_encodings::BGR8)->image;
      if (color.size() != depth.size()) {
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000, "RGB and depth image sizes differ");
        publish_depth_view(depth_message->header, depth_view);
        return;
      }

      cv::Mat hsv;
      cv::cvtColor(color, hsv, cv::COLOR_BGR2HSV);
      cv::Mat blue_mask;
      cv::inRange(hsv, cv::Scalar(90, 100, 45), cv::Scalar(135, 255, 255), blue_mask);
      const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
      cv::morphologyEx(blue_mask, blue_mask, cv::MORPH_OPEN, kernel);
      cv::morphologyEx(blue_mask, blue_mask, cv::MORPH_CLOSE, kernel);

      std::vector<std::vector<cv::Point>> contours;
      cv::findContours(blue_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
      const auto largest = std::max_element(
        contours.begin(), contours.end(),
        [](const auto & left, const auto & right) {
          return cv::contourArea(left) < cv::contourArea(right);
        });
      if (largest == contours.end() || cv::contourArea(*largest) < 40.0) {
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 3000, "No blue cube segment found in RGB image");
        publish_depth_view(depth_message->header, depth_view);
        return;
      }

      const cv::Moments moments = cv::moments(*largest);
      if (moments.m00 <= 0.0) {
        publish_depth_view(depth_message->header, depth_view);
        return;
      }
      const cv::Point centroid(
        static_cast<int>(std::lround(moments.m10 / moments.m00)),
        static_cast<int>(std::lround(moments.m01 / moments.m00)));

      cv::Mat object_mask(depth.size(), CV_8UC1, cv::Scalar(0));
      cv::drawContours(
        object_mask, contours, static_cast<int>(std::distance(contours.begin(), largest)),
        cv::Scalar(255), cv::FILLED);
      cv::erode(object_mask, object_mask, kernel);

      std::vector<float> samples;
      samples.reserve(static_cast<size_t>(cv::countNonZero(object_mask)));
      for (int row = 0; row < depth.rows; ++row) {
        const float * depth_row = depth.ptr<float>(row);
        const uint8_t * mask_row = object_mask.ptr<uint8_t>(row);
        for (int column = 0; column < depth.cols; ++column) {
          const float value = depth_row[column];
          if (mask_row[column] && std::isfinite(value) && value > 0.10F && value < 3.0F) {
            samples.push_back(value);
          }
        }
      }
      if (samples.size() < 20) {
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 3000, "Cube segment contains too few valid depth pixels");
        publish_depth_view(depth_message->header, depth_view, &*largest, &centroid);
        return;
      }

      const auto middle = samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2);
      std::nth_element(samples.begin(), middle, samples.end());
      const float median_depth = *middle;
      std::vector<float> deviations;
      deviations.reserve(samples.size());
      std::transform(
        samples.begin(), samples.end(), std::back_inserter(deviations),
        [median_depth](float value) {return std::abs(value - median_depth);});
      const auto deviation_middle =
        deviations.begin() + static_cast<std::ptrdiff_t>(deviations.size() / 2);
      std::nth_element(deviations.begin(), deviation_middle, deviations.end());
      const float median_absolute_deviation = *deviation_middle;
      if (!panda_perception::is_depth_noise_acceptable(median_absolute_deviation)) {
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 3000,
          "Rejected noisy cube depth (median absolute deviation %.3f m)",
          median_absolute_deviation);
        publish_depth_view(
          depth_message->header, depth_view, &*largest, &centroid, median_depth);
        return;
      }

      const double fx = info_message->k[0];
      const double fy = info_message->k[4];
      const double cx = info_message->k[2];
      const double cy = info_message->k[5];
      if (fx <= 0.0 || fy <= 0.0) {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "Invalid camera intrinsics");
        publish_depth_view(
          depth_message->header, depth_view, &*largest, &centroid, median_depth);
        return;
      }

      geometry_msgs::msg::PointStamped camera_point;
      camera_point.header = depth_message->header;
      camera_point.point.x = (static_cast<double>(centroid.x) - cx) * median_depth / fx;
      camera_point.point.y = (static_cast<double>(centroid.y) - cy) * median_depth / fy;
      camera_point.point.z = median_depth;

      const auto world_point = tf_buffer_.transform(
        camera_point, "world", tf2::durationFromSec(0.2));
      geometry_msgs::msg::PoseStamped cube_pose;
      cube_pose.header = world_point.header;
      cube_pose.pose.position = world_point.point;
      cube_pose.pose.position.z -= 0.025;
      cube_pose.pose.orientation.w = 1.0;

      if (!panda_perception::is_plausible_cube_position(
          cube_pose.pose.position.x, cube_pose.pose.position.y, cube_pose.pose.position.z))
      {
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 3000,
          "Rejected implausible cube estimate x=%.3f y=%.3f z=%.3f",
          cube_pose.pose.position.x, cube_pose.pose.position.y, cube_pose.pose.position.z);
        publish_depth_view(
          depth_message->header, depth_view, &*largest, &centroid, median_depth);
        return;
      }

      const cv::Point3d raw_position(
        cube_pose.pose.position.x, cube_pose.pose.position.y, cube_pose.pose.position.z);
      if (!position_history_.empty() &&
        cv::norm(raw_position - position_history_.back()) > 0.08)
      {
        position_history_.clear();
      }
      position_history_.push_back(raw_position);
      if (position_history_.size() > 7) {
        position_history_.pop_front();
      }
      if (position_history_.size() < 3) {
        publish_depth_view(
          depth_message->header, depth_view, &*largest, &centroid, median_depth);
        return;
      }

      std::vector<double> x_values;
      std::vector<double> y_values;
      std::vector<double> z_values;
      x_values.reserve(position_history_.size());
      y_values.reserve(position_history_.size());
      z_values.reserve(position_history_.size());
      for (const auto & position : position_history_) {
        x_values.push_back(position.x);
        y_values.push_back(position.y);
        z_values.push_back(position.z);
      }
      cube_pose.pose.position.x = panda_perception::median(std::move(x_values));
      cube_pose.pose.position.y = panda_perception::median(std::move(y_values));
      cube_pose.pose.position.z = panda_perception::median(std::move(z_values));

      pose_pub_->publish(cube_pose);
      visualization_msgs::msg::Marker marker;
      marker.header = cube_pose.header;
      marker.ns = "perception";
      marker.id = 0;
      marker.type = visualization_msgs::msg::Marker::CUBE;
      marker.action = visualization_msgs::msg::Marker::ADD;
      marker.pose = cube_pose.pose;
      marker.scale.x = 0.055;
      marker.scale.y = 0.055;
      marker.scale.z = 0.055;
      marker.color.r = 0.1F;
      marker.color.g = 1.0F;
      marker.color.b = 0.2F;
      marker.color.a = 0.65F;
      marker.lifetime = rclcpp::Duration::from_seconds(0.5);
      marker_pub_->publish(marker);

      publish_depth_view(
        depth_message->header, depth_view, &*largest, &centroid, median_depth);
      RCLCPP_INFO_THROTTLE(
        get_logger(), *get_clock(), 2000,
        "Cube estimate world x=%.3f y=%.3f z=%.3f from median depth %.3f m",
        cube_pose.pose.position.x, cube_pose.pose.position.y, cube_pose.pose.position.z,
        median_depth);
    } catch (const tf2::TransformException & error) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 3000, "Camera-to-world transform unavailable: %s", error.what());
    } catch (const cv_bridge::Exception & error) {
      RCLCPP_ERROR_THROTTLE(
        get_logger(), *get_clock(), 3000, "Image conversion failed: %s", error.what());
    }
  }

  std::mutex data_mutex_;
  sensor_msgs::msg::Image::ConstSharedPtr latest_color_;
  sensor_msgs::msg::Image::ConstSharedPtr latest_depth_;
  sensor_msgs::msg::CameraInfo::ConstSharedPtr latest_info_;
  tf2_ros::Buffer tf_buffer_;
  tf2_ros::TransformListener tf_listener_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr color_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr depth_sub_;
  rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr info_sub_;
  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr pose_pub_;
  rclcpp::Publisher<visualization_msgs::msg::Marker>::SharedPtr marker_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr depth_view_pub_;
  std::deque<cv::Point3d> position_history_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CubePoseEstimator>());
  rclcpp::shutdown();
  return 0;
}
