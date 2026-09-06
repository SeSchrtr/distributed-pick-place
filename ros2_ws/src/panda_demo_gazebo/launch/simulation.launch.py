from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    IncludeLaunchDescription,
    RegisterEventHandler,
    TimerAction,
)
from launch.conditions import IfCondition
from launch.event_handlers import OnProcessExit
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import Command, FindExecutable, LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    launch_rviz = LaunchConfiguration("launch_rviz")
    gz_headless_args = LaunchConfiguration("gz_headless_args")
    description = Command(
        [
            FindExecutable(name="xacro"),
            " ",
            PathJoinSubstitution(
                [FindPackageShare("panda_demo_description"), "urdf", "panda.urdf.xacro"]
            ),
        ]
    )
    robot_description = {"robot_description": description, "use_sim_time": True}
    world = PathJoinSubstitution(
        [FindPackageShare("panda_demo_gazebo"), "worlds", "panda_table.sdf"]
    )

    gazebo = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([FindPackageShare("ros_gz_sim"), "launch", "gz_sim.launch.py"])
        ),
        # gz_headless_args lets callers append e.g. "-s --headless-rendering" to run
        # Gazebo without its own GUI client, which otherwise competes for CPU with the
        # physics server and can drop the simulation's real-time factor far below 1.0
        # (observed as ~0.13x with GUI + RViz both running), causing sim-time-scaled
        # waits elsewhere in the stack to appear to hang or time out.
        launch_arguments={"gz_args": ["-r -v 2 ", gz_headless_args, " ", world]}.items(),
    )
    state_publisher = Node(
        package="robot_state_publisher",
        executable="robot_state_publisher",
        output="screen",
        parameters=[robot_description],
    )
    spawn_robot = Node(
        package="ros_gz_sim",
        executable="create",
        output="screen",
        arguments=["-topic", "robot_description", "-name", "panda", "-allow_renaming", "false"],
    )
    clock_bridge = Node(
        package="ros_gz_bridge",
        executable="parameter_bridge",
        output="screen",
        arguments=["/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock"],
    )
    camera_bridge = Node(
        package="ros_gz_bridge",
        executable="parameter_bridge",
        output="screen",
        arguments=[
            "/overhead_camera/image@sensor_msgs/msg/Image[gz.msgs.Image",
            "/overhead_camera/depth_image@sensor_msgs/msg/Image[gz.msgs.Image",
            "/overhead_camera/camera_info@sensor_msgs/msg/CameraInfo[gz.msgs.CameraInfo",
        ],
    )
    camera_mount_tf = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        output="screen",
        arguments=[
            "--x", "0.55", "--y", "0.0", "--z", "1.85",
            "--roll", "0.0", "--pitch", "1.57079632679", "--yaw", "0.0",
            "--frame-id", "world", "--child-frame-id", "overhead_camera_link",
        ],
    )
    camera_optical_tf = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        output="screen",
        arguments=[
            "--x", "0.0", "--y", "0.0", "--z", "0.0",
            "--roll", "-1.57079632679", "--pitch", "0.0",
            "--yaw", "-1.57079632679",
            "--frame-id", "overhead_camera_link",
            "--child-frame-id", "overhead_camera_optical_frame",
        ],
    )
    # Cross-host link now carries the camera feed instead of the pose estimate,
    # so it is compressed to bound WiFi bandwidth (see docs/ARCHITECTURE.md).
    color_compressor = Node(
        package="image_transport",
        executable="republish",
        name="overhead_camera_color_compressor",
        output="screen",
        arguments=["raw", "compressed"],
        remappings=[
            ("in", "/overhead_camera/image"),
            ("out/compressed", "/overhead_camera/image/compressed"),
        ],
    )
    depth_compressor = Node(
        package="image_transport",
        executable="republish",
        name="overhead_camera_depth_compressor",
        output="screen",
        arguments=["raw", "compressedDepth"],
        remappings=[
            ("in", "/overhead_camera/depth_image"),
            ("out/compressedDepth", "/overhead_camera/depth_image/compressedDepth"),
        ],
    )
    rviz = Node(
        package="rviz2",
        executable="rviz2",
        output="screen",
        condition=IfCondition(launch_rviz),
        arguments=[
            "-d",
            PathJoinSubstitution(
                [FindPackageShare("panda_demo_gazebo"), "config", "perception.rviz"]
            ),
        ],
        parameters=[robot_description],
    )

    joint_state_spawner = Node(
        package="controller_manager",
        executable="spawner",
        output="screen",
        arguments=["joint_state_broadcaster", "--controller-manager", "/controller_manager"],
    )
    arm_spawner = Node(
        package="controller_manager",
        executable="spawner",
        output="screen",
        arguments=["panda_arm_controller", "--controller-manager", "/controller_manager"],
    )
    hand_spawner = Node(
        package="controller_manager",
        executable="spawner",
        output="screen",
        arguments=["panda_hand_controller", "--controller-manager", "/controller_manager"],
    )
    grasp_adapter = Node(
        package="panda_grasp_adapter",
        executable="grasp_adapter",
        output="screen",
        parameters=[{"use_sim_time": True}],
    )

    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "launch_rviz",
                default_value="true",
                description="Start RViz with the filtered depth image and cube estimate",
            ),
            DeclareLaunchArgument(
                "gz_headless_args",
                default_value="",
                description=(
                    "Extra gz sim args, e.g. '-s --headless-rendering' to run without a "
                    "GUI client (recommended for automated/recorded runs)."
                ),
            ),
            gazebo,
            clock_bridge,
            camera_bridge,
            camera_mount_tf,
            camera_optical_tf,
            state_publisher,
            spawn_robot,
            color_compressor,
            depth_compressor,
            rviz,
            RegisterEventHandler(
                OnProcessExit(
                    target_action=spawn_robot,
                    on_exit=[TimerAction(period=1.0, actions=[joint_state_spawner, grasp_adapter])],
                )
            ),
            RegisterEventHandler(OnProcessExit(target_action=joint_state_spawner, on_exit=[arm_spawner])),
            RegisterEventHandler(OnProcessExit(target_action=arm_spawner, on_exit=[hand_spawner])),
        ]
    )
