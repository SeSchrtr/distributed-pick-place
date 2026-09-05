FROM ros:jazzy-ros-base-noble AS dds

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-c"]

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
      ros-jazzy-demo-nodes-cpp \
      ros-jazzy-rmw-cyclonedds-cpp \
    && rm -rf /var/lib/apt/lists/*

COPY infra/jetson/env.sh /opt/panda_env.sh

FROM dds AS planner

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      python3-colcon-common-extensions \
      ros-jazzy-cv-bridge \
      ros-jazzy-image-transport \
      ros-jazzy-image-transport-plugins \
      ros-jazzy-moveit \
      ros-jazzy-moveit-resources-panda-description \
      ros-jazzy-moveit-resources-panda-moveit-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/panda_ws
COPY ros2_ws/src/ src/
RUN source /opt/ros/jazzy/setup.bash \
    && colcon build --merge-install \
      --packages-select panda_demo_moveit_config panda_perception panda_pick_place \
      --cmake-args -DCMAKE_BUILD_TYPE=Release

ENTRYPOINT ["/ros_entrypoint.sh"]
CMD ["bash"]
