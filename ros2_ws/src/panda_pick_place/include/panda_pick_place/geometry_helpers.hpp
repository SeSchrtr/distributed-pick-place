#ifndef PANDA_PICK_PLACE__GEOMETRY_HELPERS_HPP_
#define PANDA_PICK_PLACE__GEOMETRY_HELPERS_HPP_

#include <cmath>

#include <geometry_msgs/msg/pose.hpp>
#include <shape_msgs/msg/solid_primitive.hpp>

// Pure, ROS-node-independent helpers factored out of pick_place.cpp so they can be unit
// tested (see test/test_geometry_helpers.cpp) without a running ROS graph or Gazebo.
namespace panda_pick_place
{

inline geometry_msgs::msg::Pose downward_pose(double x, double y, double z)
{
  geometry_msgs::msg::Pose pose;
  pose.position.x = x;
  pose.position.y = y;
  pose.position.z = z;
  pose.orientation.x = 1.0;
  pose.orientation.y = 0.0;
  pose.orientation.z = 0.0;
  pose.orientation.w = 0.0;
  return pose;
}

inline shape_msgs::msg::SolidPrimitive box(double x, double y, double z)
{
  shape_msgs::msg::SolidPrimitive primitive;
  primitive.type = shape_msgs::msg::SolidPrimitive::BOX;
  primitive.dimensions = {x, y, z};
  return primitive;
}

// Workspace bounds the state machine trusts for an incoming /perception/cube_pose sample.
inline bool is_plausible_cube_pose(double x, double y, double z)
{
  return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
         x >= 0.15 && x <= 0.90 && y >= -0.45 && y <= 0.45 && z >= 0.73 && z <= 0.84;
}

}  // namespace panda_pick_place

#endif  // PANDA_PICK_PLACE__GEOMETRY_HELPERS_HPP_
