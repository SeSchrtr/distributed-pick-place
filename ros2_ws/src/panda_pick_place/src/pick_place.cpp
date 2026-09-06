#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <control_msgs/action/gripper_command.hpp>
#include <geometry_msgs/msg/pose.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <moveit/move_group_interface/move_group_interface.hpp>
#include <moveit/planning_scene_interface/planning_scene_interface.hpp>
#include <moveit_msgs/msg/attached_collision_object.hpp>
#include <moveit_msgs/msg/collision_object.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <shape_msgs/msg/solid_primitive.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <sys/utsname.h>
#include <tf2/time.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>
#include <unistd.h>

#include "panda_pick_place/geometry_helpers.hpp"

using namespace std::chrono_literals;

namespace
{
constexpr char kWorldFrame[] = "world";
constexpr char kCubeId[] = "pick_cube";
constexpr double kPlaceX = 0.45;
constexpr double kPlaceY = -0.25;
constexpr double kGraspOffset = 0.11;
constexpr double kTravelOffset = 0.27;
constexpr double kPlanningSceneCubeClearance = 0.005;
constexpr double kClosedFingerPosition = 0.024;

enum class State
{
  INITIALIZE,
  HOME_START,
  PRE_GRASP,
  GRASP,
  CLOSE,
  ATTACH,
  LIFT,
  PRE_PLACE,
  PLACE,
  DETACH,
  OPEN,
  RETREAT,
  HOME_END,
  DONE,
  ERROR
};

const char * state_name(State state)
{
  switch (state) {
    case State::INITIALIZE: return "INITIALIZE";
    case State::HOME_START: return "HOME";
    case State::PRE_GRASP: return "PRE_GRASP";
    case State::GRASP: return "GRASP";
    case State::CLOSE: return "CLOSE";
    case State::ATTACH: return "ATTACH";
    case State::LIFT: return "LIFT";
    case State::PRE_PLACE: return "PRE_PLACE";
    case State::PLACE: return "PLACE";
    case State::DETACH: return "DETACH";
    case State::OPEN: return "OPEN";
    case State::RETREAT: return "RETREAT";
    case State::HOME_END: return "HOME";
    case State::DONE: return "DONE";
    case State::ERROR: return "ERROR";
  }
  return "UNKNOWN";
}

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

}  // namespace

class PickPlaceStateMachine
{
public:
  using GripperCommand = control_msgs::action::GripperCommand;
  using GripperGoalHandle = rclcpp_action::ClientGoalHandle<GripperCommand>;

  explicit PickPlaceStateMachine(const rclcpp::Node::SharedPtr & node)
  : node_(node),
    logger_(node->get_logger()),
    arm_(node, "panda_arm"),
    tf_buffer_(node->get_clock()),
    tf_listener_(tf_buffer_)
  {
    arm_.setPoseReferenceFrame(kWorldFrame);
    arm_.setEndEffectorLink("panda_hand");
    arm_.setPlanningTime(10.0);
    arm_.setNumPlanningAttempts(8);
    arm_.setMaxVelocityScalingFactor(0.20);
    arm_.setMaxAccelerationScalingFactor(0.20);
    arm_.setGoalPositionTolerance(0.008);
    arm_.setGoalOrientationTolerance(0.03);

    gripper_client_ = rclcpp_action::create_client<GripperCommand>(
      node_, "/panda_hand_controller/gripper_cmd");
    attach_client_ = node_->create_client<std_srvs::srv::Trigger>("/grasp/attach");
    detach_client_ = node_->create_client<std_srvs::srv::Trigger>("/grasp/detach");
    reset_client_ = node_->create_client<std_srvs::srv::Trigger>("/grasp/reset_object");
    pose_sub_ = node_->create_subscription<geometry_msgs::msg::PoseStamped>(
      "/perception/cube_pose", rclcpp::QoS(10).reliable(),
      std::bind(&PickPlaceStateMachine::on_cube_pose, this, std::placeholders::_1));
    node_->get_parameter_or("reset_object", reset_object_, true);
  }

  bool run()
  {
    const std::vector<std::pair<State, std::function<bool()>>> steps = {
      {State::INITIALIZE, [this]() {return initialize();}},
      {State::HOME_START, [this]() {return prepare_pick();}},
      {State::PRE_GRASP, [this]() {return move_pose(pick_target(kTravelOffset));}},
      {State::GRASP, [this]() {
          planning_scene_.removeCollisionObjects({kCubeId});
          return move_pose(pick_target(kGraspOffset));
        }},
      {State::CLOSE, [this]() {return command_gripper(kClosedFingerPosition);}},
      {State::ATTACH, [this]() {return attach();}},
      {State::LIFT, [this]() {return move_pose(pick_target(kTravelOffset));}},
      {State::PRE_PLACE, [this]() {return move_pose(place_target(kTravelOffset));}},
      {State::PLACE, [this]() {return move_pose(place_target(kGraspOffset));}},
      {State::DETACH, [this]() {return detach();}},
      {State::OPEN, [this]() {return command_gripper(0.035);}},
      {State::RETREAT, [this]() {return move_pose(place_target(kTravelOffset));}},
      {State::HOME_END, [this]() {return move_named("ready");}},
    };

    for (const auto & [state, operation] : steps) {
      RCLCPP_INFO(logger_, "STATE %s", state_name(state));
      if (!operation()) {
        RCLCPP_ERROR(logger_, "STATE %s failed; sequence aborted", state_name(state));
        RCLCPP_ERROR(logger_, "STATE %s", state_name(State::ERROR));
        return false;
      }
    }
    RCLCPP_INFO(logger_, "STATE %s", state_name(State::DONE));
    return true;
  }

private:
  bool initialize()
  {
    log_deployment(logger_);
    if (!gripper_client_->wait_for_action_server(15s)) {
      RCLCPP_ERROR(logger_, "Gripper action on ThinkPad is unavailable");
      return false;
    }
    if (!attach_client_->wait_for_service(15s) ||
      !detach_client_->wait_for_service(15s) ||
      !reset_client_->wait_for_service(15s))
    {
      RCLCPP_ERROR(logger_, "Grasp adapter services on ThinkPad are unavailable");
      return false;
    }
    if (!arm_.getCurrentState(15.0)) {
      RCLCPP_ERROR(logger_, "No /joint_states received from ThinkPad");
      return false;
    }
    if (!tf_buffer_.canTransform(
        kWorldFrame, "panda_link0", tf2::TimePointZero, tf2::durationFromSec(15.0)))
    {
      RCLCPP_ERROR(logger_, "TF world -> panda_link0 was not received from ThinkPad");
      return false;
    }
    if (!call_service(detach_client_, "initial detach")) {
      return false;
    }
    if (!command_gripper(0.035)) {
      RCLCPP_ERROR(logger_, "Failed to open gripper during recovery initialization");
      return false;
    }
    if (reset_object_) {
      if (!call_service(reset_client_, "reset object")) {
        return false;
      }
    } else {
      RCLCPP_INFO(logger_, "Object reset disabled; using the cube at its current measured pose");
    }
    return initialize_planning_scene();
  }

  bool prepare_pick()
  {
    if (!move_named("ready")) {
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(pose_mutex_);
      latest_cube_pose_.reset();
      detection_not_before_ = node_->now();
    }
    return wait_for_cube_pose() && add_pick_cube();
  }

  void on_cube_pose(const geometry_msgs::msg::PoseStamped::ConstSharedPtr & message)
  {
    if (message->header.frame_id != kWorldFrame) {
      RCLCPP_WARN_THROTTLE(
        logger_, *node_->get_clock(), 3000,
        "Ignoring cube pose in frame '%s'; expected '%s'",
        message->header.frame_id.c_str(), kWorldFrame);
      return;
    }
    const auto & position = message->pose.position;
    if (!panda_pick_place::is_plausible_cube_pose(position.x, position.y, position.z)) {
      RCLCPP_WARN_THROTTLE(
        logger_, *node_->get_clock(), 3000,
        "Ignoring implausible cube pose x=%.3f y=%.3f z=%.3f",
        position.x, position.y, position.z);
      return;
    }

    std::lock_guard<std::mutex> lock(pose_mutex_);
    if (rclcpp::Time(message->header.stamp) <= detection_not_before_) {
      return;
    }
    latest_cube_pose_ = *message;
    pose_condition_.notify_one();
  }

  bool wait_for_cube_pose()
  {
    std::unique_lock<std::mutex> lock(pose_mutex_);
    if (!pose_condition_.wait_for(lock, 30s, [this]() {return latest_cube_pose_.has_value();})) {
      RCLCPP_ERROR(
        logger_, "No fresh RGB-D cube estimate received on /perception/cube_pose");
      return false;
    }
    pick_pose_ = latest_cube_pose_->pose;
    RCLCPP_INFO(
      logger_, "Using measured cube pose x=%.3f y=%.3f z=%.3f (stamp %.3f)",
      pick_pose_.position.x, pick_pose_.position.y, pick_pose_.position.z,
      rclcpp::Time(latest_cube_pose_->header.stamp).seconds());
    return true;
  }

  geometry_msgs::msg::Pose pick_target(double height_offset) const
  {
    return panda_pick_place::downward_pose(
      pick_pose_.position.x, pick_pose_.position.y, pick_pose_.position.z + height_offset);
  }

  geometry_msgs::msg::Pose place_target(double height_offset) const
  {
    return panda_pick_place::downward_pose(kPlaceX, kPlaceY, pick_pose_.position.z + height_offset);
  }

  bool initialize_planning_scene()
  {
    moveit_msgs::msg::CollisionObject table;
    table.header.frame_id = kWorldFrame;
    table.id = "table";
    table.primitives.push_back(panda_pick_place::box(0.9, 0.9, 0.75));
    geometry_msgs::msg::Pose table_pose;
    table_pose.position.x = 0.55;
    table_pose.position.z = 0.375;
    table_pose.orientation.w = 1.0;
    table.primitive_poses.push_back(table_pose);
    table.operation = moveit_msgs::msg::CollisionObject::ADD;

    moveit_msgs::msg::AttachedCollisionObject old_attachment;
    old_attachment.link_name = "panda_hand";
    old_attachment.object.id = kCubeId;
    old_attachment.object.operation = moveit_msgs::msg::CollisionObject::REMOVE;
    if (!planning_scene_.applyAttachedCollisionObject(old_attachment)) {
      RCLCPP_ERROR(logger_, "Failed to clear old cube attachment from Planning Scene");
      return false;
    }

    moveit_msgs::msg::CollisionObject old_cube;
    old_cube.header.frame_id = kWorldFrame;
    old_cube.id = kCubeId;
    old_cube.operation = moveit_msgs::msg::CollisionObject::REMOVE;
    planning_scene_.applyCollisionObject(old_cube);

    if (!planning_scene_.applyCollisionObject(table)) {
      RCLCPP_ERROR(logger_, "Failed to initialize table in Planning Scene");
      return false;
    }
    RCLCPP_INFO(logger_, "Planning Scene reset with table and no stale cube attachment");
    return true;
  }

  bool add_pick_cube()
  {
    if (!planning_scene_.applyCollisionObject(cube_collision(
        pick_pose_.position.x, pick_pose_.position.y, pick_pose_.position.z)))
    {
      RCLCPP_ERROR(logger_, "Failed to add pick cube to Planning Scene");
      return false;
    }
    RCLCPP_INFO(logger_, "Pick cube added to Planning Scene after recovery move");
    return true;
  }

  moveit_msgs::msg::CollisionObject cube_collision(double x, double y, double z) const
  {
    moveit_msgs::msg::CollisionObject cube;
    cube.header.frame_id = kWorldFrame;
    cube.id = kCubeId;
    cube.primitives.push_back(panda_pick_place::box(0.05, 0.05, 0.05));
    geometry_msgs::msg::Pose pose;
    pose.position.x = x;
    pose.position.y = y;
    // Contact with the table is valid physically, but MoveIt treats exact contact as collision.
    pose.position.z = z + kPlanningSceneCubeClearance;
    pose.orientation.w = 1.0;
    cube.primitive_poses.push_back(pose);
    cube.operation = moveit_msgs::msg::CollisionObject::ADD;
    return cube;
  }

  bool move_named(const std::string & target)
  {
    arm_.setStartStateToCurrentState();
    arm_.clearPoseTargets();
    if (!arm_.setNamedTarget(target)) {
      RCLCPP_ERROR(logger_, "Unknown MoveIt named target: %s", target.c_str());
      return false;
    }
    return plan_and_execute();
  }

  bool move_pose(const geometry_msgs::msg::Pose & pose)
  {
    arm_.setStartStateToCurrentState();
    arm_.clearPoseTargets();
    if (!arm_.setPoseTarget(pose, "panda_hand")) {
      RCLCPP_ERROR(logger_, "IK rejected pose x=%.3f y=%.3f z=%.3f",
        pose.position.x, pose.position.y, pose.position.z);
      return false;
    }
    return plan_and_execute();
  }

  bool plan_and_execute()
  {
    moveit::planning_interface::MoveGroupInterface::Plan plan;
    if (arm_.plan(plan) != moveit::core::MoveItErrorCode::SUCCESS) {
      RCLCPP_ERROR(logger_, "MoveIt/OMPL planning failed");
      return false;
    }
    RCLCPP_INFO(logger_, "Planned %zu trajectory points",
      plan.trajectory.joint_trajectory.points.size());
    if (arm_.execute(plan) != moveit::core::MoveItErrorCode::SUCCESS) {
      RCLCPP_ERROR(logger_, "Trajectory execution on ThinkPad failed");
      return false;
    }
    return true;
  }

  bool command_gripper(double position)
  {
    GripperCommand::Goal goal;
    goal.command.position = position;
    goal.command.max_effort = 20.0;

    auto goal_future = gripper_client_->async_send_goal(goal);
    if (goal_future.wait_for(5s) != std::future_status::ready) {
      RCLCPP_ERROR(logger_, "Timed out sending gripper command");
      return false;
    }
    const auto goal_handle = goal_future.get();
    if (!goal_handle) {
      RCLCPP_ERROR(logger_, "ThinkPad gripper controller rejected command");
      return false;
    }
    auto result_future = gripper_client_->async_get_result(goal_handle);
    if (result_future.wait_for(8s) != std::future_status::ready) {
      RCLCPP_ERROR(logger_, "Timed out waiting for gripper result");
      return false;
    }
    return result_future.get().code == rclcpp_action::ResultCode::SUCCEEDED;
  }

  bool attach()
  {
    if (!call_service(attach_client_, "attach")) {
      return false;
    }

    moveit_msgs::msg::AttachedCollisionObject attached;
    attached.link_name = "panda_hand";
    attached.touch_links = {"panda_hand", "panda_leftfinger", "panda_rightfinger"};
    attached.object = cube_collision(
      pick_pose_.position.x, pick_pose_.position.y, pick_pose_.position.z);
    attached.object.operation = moveit_msgs::msg::CollisionObject::ADD;
    if (!planning_scene_.applyAttachedCollisionObject(attached)) {
      RCLCPP_ERROR(logger_, "Failed to attach cube in MoveIt Planning Scene");
      return false;
    }
    return true;
  }

  bool detach()
  {
    if (!call_service(detach_client_, "detach")) {
      return false;
    }

    moveit_msgs::msg::AttachedCollisionObject attached;
    attached.link_name = "panda_hand";
    attached.object.id = kCubeId;
    attached.object.operation = moveit_msgs::msg::CollisionObject::REMOVE;
    if (!planning_scene_.applyAttachedCollisionObject(attached)) {
      RCLCPP_ERROR(logger_, "Failed to detach cube in MoveIt Planning Scene");
      return false;
    }
    if (!planning_scene_.applyCollisionObject(
        cube_collision(kPlaceX, kPlaceY, pick_pose_.position.z)))
    {
      RCLCPP_ERROR(logger_, "Failed to add placed cube back to Planning Scene");
      return false;
    }
    return true;
  }

  bool call_service(
    const rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr & client,
    const char * operation)
  {
    auto future = client->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
    if (future.wait_for(5s) != std::future_status::ready) {
      RCLCPP_ERROR(logger_, "Timed out waiting for grasp service: %s", operation);
      return false;
    }
    const auto response = future.get();
    if (!response->success) {
      RCLCPP_ERROR(logger_, "Grasp service %s failed: %s", operation, response->message.c_str());
      return false;
    }
    return true;
  }

  rclcpp::Node::SharedPtr node_;
  rclcpp::Logger logger_;
  moveit::planning_interface::MoveGroupInterface arm_;
  moveit::planning_interface::PlanningSceneInterface planning_scene_;
  tf2_ros::Buffer tf_buffer_;
  tf2_ros::TransformListener tf_listener_;
  rclcpp_action::Client<GripperCommand>::SharedPtr gripper_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr attach_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr detach_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr reset_client_;
  rclcpp::Subscription<geometry_msgs::msg::PoseStamped>::SharedPtr pose_sub_;
  std::mutex pose_mutex_;
  std::condition_variable pose_condition_;
  std::optional<geometry_msgs::msg::PoseStamped> latest_cube_pose_;
  rclcpp::Time detection_not_before_{0, 0, RCL_ROS_TIME};
  geometry_msgs::msg::Pose pick_pose_;
  bool reset_object_{true};
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>(
    "panda_pick_place",
    rclcpp::NodeOptions().automatically_declare_parameters_from_overrides(true));

  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);
  std::thread spin_thread([&executor]() {executor.spin();});

  bool success = false;
  try {
    PickPlaceStateMachine state_machine(node);
    success = state_machine.run();
  } catch (const std::exception & error) {
    RCLCPP_ERROR(node->get_logger(), "Unhandled pick-and-place exception: %s", error.what());
  }

  executor.cancel();
  spin_thread.join();
  rclcpp::shutdown();
  return success ? 0 : 1;
}
