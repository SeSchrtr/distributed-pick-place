from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from moveit_configs_utils import MoveItConfigsBuilder


def generate_launch_description():
    reset_object = LaunchConfiguration("reset_object")
    moveit_config = (
        MoveItConfigsBuilder(
            "moveit_resources_panda",
            package_name="moveit_resources_panda_moveit_config",
        )
        .robot_description(file_path="config/panda.urdf.xacro")
        .robot_description_semantic(file_path="config/panda.srdf")
        .trajectory_execution(file_path="config/gripper_moveit_controllers.yaml")
        .planning_pipelines(pipelines=["ompl"])
        .to_moveit_configs()
    )
    pick_place = Node(
        package="panda_pick_place",
        executable="pick_place",
        output="screen",
        parameters=[
            moveit_config.to_dict(),
            {
                "use_sim_time": True,
                "reset_object": ParameterValue(reset_object, value_type=bool),
            },
        ],
    )
    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "reset_object",
                default_value="true",
                description="Reset the cube before measuring it; disable to test arbitrary poses",
            ),
            pick_place,
        ]
    )
