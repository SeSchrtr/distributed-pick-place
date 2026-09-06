#include <limits>

#include <gtest/gtest.h>

#include "panda_pick_place/geometry_helpers.hpp"

TEST(DownwardPose, SetsRequestedPositionAndTopDownOrientation)
{
  const auto pose = panda_pick_place::downward_pose(0.4, -0.2, 0.8);
  EXPECT_DOUBLE_EQ(pose.position.x, 0.4);
  EXPECT_DOUBLE_EQ(pose.position.y, -0.2);
  EXPECT_DOUBLE_EQ(pose.position.z, 0.8);
  EXPECT_DOUBLE_EQ(pose.orientation.x, 1.0);
  EXPECT_DOUBLE_EQ(pose.orientation.w, 0.0);
}

TEST(Box, SetsBoxTypeAndDimensions)
{
  const auto primitive = panda_pick_place::box(0.05, 0.06, 0.07);
  EXPECT_EQ(primitive.type, shape_msgs::msg::SolidPrimitive::BOX);
  ASSERT_EQ(primitive.dimensions.size(), 3u);
  EXPECT_DOUBLE_EQ(primitive.dimensions[0], 0.05);
  EXPECT_DOUBLE_EQ(primitive.dimensions[1], 0.06);
  EXPECT_DOUBLE_EQ(primitive.dimensions[2], 0.07);
}

TEST(PlausibleCubePose, InsideWorkspaceIsAccepted)
{
  EXPECT_TRUE(panda_pick_place::is_plausible_cube_pose(0.5, 0.0, 0.78));
}

TEST(PlausibleCubePose, OutsideWorkspaceIsRejected)
{
  EXPECT_FALSE(panda_pick_place::is_plausible_cube_pose(1.5, 0.0, 0.78));
}

TEST(PlausibleCubePose, NonFiniteValueIsRejected)
{
  EXPECT_FALSE(
    panda_pick_place::is_plausible_cube_pose(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.78));
}

int main(int argc, char ** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
