cmake_minimum_required(VERSION 3.8)
project(panda_grasp_adapter)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_srvs REQUIRED)
find_package(gz-msgs10 REQUIRED)
find_package(gz-transport13 REQUIRED)

add_executable(grasp_adapter src/grasp_adapter.cpp)
ament_target_dependencies(grasp_adapter rclcpp std_srvs)
target_link_libraries(grasp_adapter gz-msgs10::core gz-transport13::core)

install(TARGETS grasp_adapter DESTINATION lib/${PROJECT_NAME})

ament_package()

