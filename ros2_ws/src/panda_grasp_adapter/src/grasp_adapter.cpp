#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gz/msgs/boolean.pb.h>
#include <gz/msgs/empty.pb.h>
#include <gz/msgs/pose.pb.h>
#include <gz/msgs/stringmsg.pb.h>
#include <gz/transport/Node.hh>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/trigger.hpp>

using namespace std::chrono_literals;

class GraspAdapter : public rclcpp::Node
{
public:
  GraspAdapter()
  : Node("panda_grasp_adapter")
  {
    attach_pub_ = gz_node_.Advertise<gz::msgs::Empty>("/panda/grasp/attach");
    detach_pub_ = gz_node_.Advertise<gz::msgs::Empty>("/panda/grasp/detach");
    gz_node_.Subscribe("/panda/grasp/state", &GraspAdapter::on_state, this);

    attach_service_ = create_service<std_srvs::srv::Trigger>(
      "/grasp/attach",
      [this](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
      std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
        response->success = publish_command(attach_pub_, true);
        response->message = response->success ? "Gazebo attach confirmed" : "Gazebo attach failed";
      });
    detach_service_ = create_service<std_srvs::srv::Trigger>(
      "/grasp/detach",
      [this](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
      std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
        response->success = publish_command(detach_pub_, false);
        response->message = response->success ? "Gazebo detach confirmed" : "Gazebo detach failed";
      });
    reset_service_ = create_service<std_srvs::srv::Trigger>(
      "/grasp/reset_object",
      [this](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
      std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
        response->success = reset_object();
        response->message = response->success ?
          "Gazebo cube reset to pick pose" : "Gazebo cube reset failed";
      });

    // DetachableJoint starts attached. Repeat the initial detach while Gazebo discovery settles.
    initial_detach_timer_ = create_wall_timer(500ms, [this]() {
      gz::msgs::Empty message;
      detach_pub_.Publish(message);
      if (++initial_detach_attempts_ >= 20 || (state_seen_ && !attached_)) {
        initial_detach_timer_->cancel();
        RCLCPP_INFO(get_logger(), "Initial cube state is detached");
      }
    });

    RCLCPP_INFO(
      get_logger(),
      "Grasp services /grasp/attach, /grasp/detach and /grasp/reset_object are ready");
  }

private:
  void on_state(const gz::msgs::StringMsg & message)
  {
    if (message.data() != "attached" && message.data() != "detached") {
      RCLCPP_WARN(get_logger(), "Ignoring unknown Gazebo grasp state: %s", message.data().c_str());
      return;
    }
    attached_ = message.data() == "attached";
    state_seen_ = true;
  }

  bool publish_command(gz::transport::Node::Publisher & publisher, bool expected_attached)
  {
    if (state_seen_ && attached_ == expected_attached) {
      return true;
    }

    gz::msgs::Empty message;
    if (!publisher.Publish(message)) {
      RCLCPP_ERROR(get_logger(), "Gazebo Transport rejected grasp command");
      return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (state_seen_ && attached_ == expected_attached) {
        return true;
      }
      std::this_thread::sleep_for(20ms);
    }
    RCLCPP_ERROR(get_logger(), "Timed out waiting for Gazebo grasp state=%s",
      expected_attached ? "attached" : "detached");
    return false;
  }

  bool reset_object()
  {
    gz::msgs::Pose request;
    request.set_name("pick_cube");
    request.mutable_position()->set_x(0.45);
    request.mutable_position()->set_y(0.20);
    request.mutable_position()->set_z(0.78);
    request.mutable_orientation()->set_w(1.0);

    gz::msgs::Boolean response;
    bool service_result = false;
    const bool request_sent = gz_node_.Request(
      "/world/panda_table/set_pose", request, 2000, response, service_result);
    if (!request_sent || !service_result || !response.data()) {
      RCLCPP_ERROR(get_logger(), "Gazebo /world/panda_table/set_pose request failed");
      return false;
    }
    return true;
  }

  gz::transport::Node gz_node_;
  gz::transport::Node::Publisher attach_pub_;
  gz::transport::Node::Publisher detach_pub_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr attach_service_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr detach_service_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr reset_service_;
  rclcpp::TimerBase::SharedPtr initial_detach_timer_;
  std::atomic_bool attached_{true};
  std::atomic_bool state_seen_{false};
  unsigned int initial_detach_attempts_{0};
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<GraspAdapter>());
  rclcpp::shutdown();
  return 0;
}
