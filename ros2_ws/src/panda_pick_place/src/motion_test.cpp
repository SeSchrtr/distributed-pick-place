#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <moveit/move_group_interface/move_group_interface.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sys/utsname.h>
#include <unistd.h>

namespace
{
void log_deployment(const rclcpp::Logger & logger)
{
  std::array<char, 256> hostname{};
  struct utsname system_info {};
  gethostname(hostname.data(), hostname.size() - 1);
  uname(&system_info);
  const auto env = [](const char * name) {
      const char * value = std::getenv(name);
      return value == nullptr ? std::string("<unset>") : std::string(value);
    };

  RCLCPP_INFO(logger, "hostname=%s architecture=%s", hostname.data(), system_info.machine);
  RCLCPP_INFO(logger, "ROS_DISTRO=%s ROS_DOMAIN_ID=%s RMW_IMPLEMENTATION=%s",
    env("ROS_DISTRO").c_str(), env("ROS_DOMAIN_ID").c_str(),
    env("RMW_IMPLEMENTATION").c_str());
}

bool plan_and_execute(
  moveit::planning_interface::MoveGroupInterface & arm,
  const rclcpp::Logger & logger)
{
  moveit::planning_interface::MoveGroupInterface::Plan plan;
  const auto planned = arm.plan(plan);
  if (planned != moveit::core::MoveItErrorCode::SUCCESS) {
    RCLCPP_ERROR(logger, "MoveIt failed to plan the Stage I motion");
    return false;
  }
  RCLCPP_INFO(logger, "MoveIt planned %zu trajectory points on this host",
    plan.trajectory.joint_trajectory.points.size());
  const auto executed = arm.execute(plan);
  if (executed != moveit::core::MoveItErrorCode::SUCCESS) {
    RCLCPP_ERROR(logger, "Trajectory execution through ros2_control failed");
    return false;
  }
  return true;
}
}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>(
    "panda_motion_test",
    rclcpp::NodeOptions().automatically_declare_parameters_from_overrides(true));
  log_deployment(node->get_logger());

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  std::thread spin_thread([&executor]() {executor.spin();});

  bool success = false;
  try {
    moveit::planning_interface::MoveGroupInterface arm(node, "panda_arm");
    arm.setPlanningTime(10.0);
    arm.setNumPlanningAttempts(5);
    arm.setMaxVelocityScalingFactor(0.25);
    arm.setMaxAccelerationScalingFactor(0.25);

    if (!arm.getCurrentState(10.0)) {
      RCLCPP_ERROR(node->get_logger(), "No current Panda state received from ThinkPad");
    } else {
      const std::array<double, 7> target = {0.25, -0.70, 0.0, -2.20, 0.0, 1.65, 0.90};
      arm.setJointValueTarget(std::vector<double>(target.begin(), target.end()));
      success = plan_and_execute(arm, node->get_logger());
    }
  } catch (const std::exception & error) {
    RCLCPP_ERROR(node->get_logger(), "Motion test exception: %s", error.what());
  }

  executor.cancel();
  spin_thread.join();
  rclcpp::shutdown();
  return success ? 0 : 1;
}
