#!/usr/bin/env bash
set -euo pipefail

CONTAINER_NAME="${PANDA_CONTAINER_NAME:-panda-planner}"
IMAGE_TAG="${PANDA_IMAGE_TAG:-panda-planner:jazzy}"

if docker container inspect "${CONTAINER_NAME}" >/dev/null 2>&1; then
  docker rm --force "${CONTAINER_NAME}" >/dev/null
fi

docker run --detach \
  --name "${CONTAINER_NAME}" \
  --network host \
  --env ROS_DOMAIN_ID=42 \
  --env RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
  --env ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET \
  "${IMAGE_TAG}" \
  bash -lc 'source /opt/panda_env.sh \
    && { ros2 launch panda_perception perception.launch.py & \
         ros2 launch panda_demo_moveit_config planner.launch.py & \
         wait -n; }'

echo "Started ${CONTAINER_NAME} from ${IMAGE_TAG}"
