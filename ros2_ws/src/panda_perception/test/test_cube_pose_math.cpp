#include <gtest/gtest.h>

#include <opencv2/core.hpp>

#include "panda_perception/cube_pose_math.hpp"

TEST(Median, OddSizedVectorReturnsMiddleValue)
{
  EXPECT_DOUBLE_EQ(panda_perception::median({3.0, 1.0, 2.0}), 2.0);
}

TEST(Median, SingleValueReturnsItself)
{
  EXPECT_DOUBLE_EQ(panda_perception::median({5.0}), 5.0);
}

TEST(PlausibleCubePosition, InsideWorkspaceIsAccepted)
{
  EXPECT_TRUE(panda_perception::is_plausible_cube_position(0.5, 0.0, 0.78));
}

TEST(PlausibleCubePosition, OutsideXBoundsIsRejected)
{
  EXPECT_FALSE(panda_perception::is_plausible_cube_position(1.5, 0.0, 0.78));
}

TEST(PlausibleCubePosition, OutsideZBoundsIsRejected)
{
  EXPECT_FALSE(panda_perception::is_plausible_cube_position(0.5, 0.0, 0.5));
}

TEST(DepthNoise, LowDeviationIsAcceptable)
{
  EXPECT_TRUE(panda_perception::is_depth_noise_acceptable(0.005F));
}

TEST(DepthNoise, HighDeviationIsRejected)
{
  EXPECT_FALSE(panda_perception::is_depth_noise_acceptable(0.02F));
}

TEST(DepthColormap, InvalidDepthProducesTheDarkGraySentinelPixel)
{
  const cv::Mat depth(1, 1, CV_32FC1, cv::Scalar(-1.0F));
  const cv::Mat colormap = panda_perception::make_depth_colormap(depth);
  EXPECT_EQ(colormap.at<cv::Vec3b>(0, 0), cv::Vec3b(20, 20, 20));
}

TEST(DepthColormap, ValidDepthProducesAColorizedPixel)
{
  const cv::Mat depth(1, 1, CV_32FC1, cv::Scalar(1.0F));
  const cv::Mat colormap = panda_perception::make_depth_colormap(depth);
  EXPECT_NE(colormap.at<cv::Vec3b>(0, 0), cv::Vec3b(20, 20, 20));
}

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
