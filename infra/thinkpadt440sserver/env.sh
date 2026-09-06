#!/usr/bin/env bash

_PANDA_ENV_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_PANDA_REPO_ROOT="$(cd "${_PANDA_ENV_DIR}/../.." && pwd)"
_PANDA_RESTORE_NOUNSET=false
if [[ $- == *u* ]]; then
  _PANDA_RESTORE_NOUNSET=true
  set +u
fi

source /opt/ros/jazzy/setup.bash
if [[ -f "${_PANDA_REPO_ROOT}/ros2_ws/install/setup.bash" ]]; then
  source "${_PANDA_REPO_ROOT}/ros2_ws/install/setup.bash"
fi

if [[ "${_PANDA_RESTORE_NOUNSET}" == true ]]; then
  set -u
fi

export ROS_DOMAIN_ID=42
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
# This host keeps WiFi enabled for desktop use; pin DDS traffic to the wired NIC
# so dual-homing (WiFi + Ethernet on the same subnet) cannot stall discovery.
export CYCLONEDDS_URI=file:///etc/panda-demo/cyclonedds.xml

unset _PANDA_ENV_DIR
unset _PANDA_REPO_ROOT
unset _PANDA_RESTORE_NOUNSET
