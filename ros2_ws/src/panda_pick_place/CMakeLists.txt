cmake_minimum_required(VERSION 3.8)
project(panda_pick_place)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(control_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(moveit_msgs REQUIRED)
find_package(moveit_ros_planning_interface REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(shape_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)

add_executable(motion_test src/motion_test.cpp)
ament_target_dependencies(motion_test moveit_ros_planning_interface rclcpp)
target_compile_features(motion_test PUBLIC cxx_std_17)

add_executable(pick_place src/pick_place.cpp)
target_include_directories(pick_place PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
ament_target_dependencies(
  pick_place
  control_msgs
  geometry_msgs
  moveit_msgs
  moveit_ros_planning_interface
  rclcpp
  rclcpp_action
  shape_msgs
  std_srvs
  tf2
  tf2_ros
)
target_compile_features(pick_place PUBLIC cxx_std_17)

install(TARGETS motion_test pick_place DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_geometry_helpers test/test_geometry_helpers.cpp)
  target_include_directories(test_geometry_helpers PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  ament_target_dependencies(test_geometry_helpers geometry_msgs shape_msgs)
endif()

ament_package()
