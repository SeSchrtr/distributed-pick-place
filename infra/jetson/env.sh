#!/usr/bin/env bash

_PANDA_RESTORE_NOUNSET=false
if [[ $- == *u* ]]; then
  _PANDA_RESTORE_NOUNSET=true
  set +u
fi

source /opt/ros/jazzy/setup.bash
if [[ -f /opt/panda_ws/install/setup.bash ]]; then
  source /opt/panda_ws/install/setup.bash
fi

if [[ "${_PANDA_RESTORE_NOUNSET}" == true ]]; then
  set -u
fi

export ROS_DOMAIN_ID=42
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET

unset _PANDA_RESTORE_NOUNSET
