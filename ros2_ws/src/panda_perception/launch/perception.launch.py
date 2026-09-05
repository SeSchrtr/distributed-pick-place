from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    # Decompress the WiFi-bandwidth-friendly streams from the ThinkPad camera
    # bridge back into the plain topics the estimator below already expects.
    color_decompressor = Node(
        package="image_transport",
        executable="republish",
        name="overhead_camera_color_decompressor",
        output="screen",
        arguments=["compressed", "raw"],
        remappings=[
            ("in/compressed", "/overhead_camera/image/compressed"),
            ("out", "/overhead_camera/image"),
        ],
    )
    depth_decompressor = Node(
        package="image_transport",
        executable="republish",
        name="overhead_camera_depth_decompressor",
        output="screen",
        arguments=["compressedDepth", "raw"],
        remappings=[
            ("in/compressedDepth", "/overhead_camera/depth_image/compressedDepth"),
            ("out", "/overhead_camera/depth_image"),
        ],
    )
    cube_pose_estimator = Node(
        package="panda_perception",
        executable="cube_pose_estimator",
        output="screen",
        parameters=[{"use_sim_time": True}],
    )
    return LaunchDescription([color_decompressor, depth_decompressor, cube_pose_estimator])
