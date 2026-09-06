cmake_minimum_required(VERSION 3.8)
project(panda_perception)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(visualization_msgs REQUIRED)

add_executable(cube_pose_estimator src/cube_pose_estimator.cpp)
target_include_directories(cube_pose_estimator PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
ament_target_dependencies(
  cube_pose_estimator
  cv_bridge
  geometry_msgs
  rclcpp
  sensor_msgs
  std_msgs
  tf2
  tf2_geometry_msgs
  tf2_ros
  visualization_msgs
)
target_link_libraries(cube_pose_estimator ${OpenCV_LIBS})
target_compile_features(cube_pose_estimator PUBLIC cxx_std_17)

install(TARGETS cube_pose_estimator DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_cube_pose_math test/test_cube_pose_math.cpp)
  target_include_directories(test_cube_pose_math PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
  ament_target_dependencies(test_cube_pose_math cv_bridge sensor_msgs)
  target_link_libraries(test_cube_pose_math ${OpenCV_LIBS})
endif()

ament_package()
