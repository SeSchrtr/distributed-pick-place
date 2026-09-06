#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REMOTE="${JETSON_SSH_HOST:-jetson}"
REMOTE_ROOT="${JETSON_PROJECT_DIR:-panda_distributed_pick_place}"
CONTAINER_NAME="${PANDA_CONTAINER_NAME:-panda-planner}"
READY_TIMEOUT="${PANDA_READY_TIMEOUT:-60}"
SIM_REMOTE="${SERVER_SSH_HOST:-thinkpad440sserver}"
SIM_REMOTE_ROOT="${SERVER_PROJECT_DIR:-panda_distributed_pick_place}"

source "${REPO_ROOT}/infra/thinkpad/env.sh"

wait_until() {
  local description="$1"
  shift
  local deadline=$((SECONDS + READY_TIMEOUT))
  until "$@"; do
    if (( SECONDS >= deadline )); then
      echo "Timeout waiting for ${description} (${READY_TIMEOUT}s)." >&2
      return 1
    fi
    sleep 1
  done
  echo "Ready: ${description}"
}

# All simulation-side checks run against the server over SSH, not locally:
# this dev machine is WiFi-only and is not part of the runtime DDS-critical
# path (see docs/ARCHITECTURE.md, "Three-Host Topology").
on_server() {
  ssh "${SIM_REMOTE}" \
    "source ${SIM_REMOTE_ROOT}/infra/thinkpadt440sserver/env.sh && $1"
}

controller_active() {
  on_server "ros2 control list_controllers 2>/dev/null" |
    grep -Eq "^$1[[:space:]].*[[:space:]]active$"
}

joint_state_available() {
  on_server "timeout 15 ros2 topic echo /joint_states --once >/dev/null 2>&1"
}

arm_action_available() {
  on_server "ros2 action list 2>/dev/null" |
    grep -qx '/panda_arm_controller/follow_joint_trajectory'
}

grasp_services_available() {
  local services
  services="$(on_server "ros2 service list 2>/dev/null")"
  grep -qx '/grasp/attach' <<<"${services}" &&
    grep -qx '/grasp/detach' <<<"${services}" &&
    grep -qx '/grasp/reset_object' <<<"${services}"
}

cube_pose_available() {
  on_server "timeout 15 ros2 topic echo /perception/cube_pose --once >/dev/null 2>&1"
}

move_group_ready() {
  ssh "${REMOTE}" docker exec "${CONTAINER_NAME}" bash -lc \
    "'source /opt/panda_env.sh && ros2 node list 2>/dev/null'" |
    grep -qx '/move_group'
}

wait_until "joint_state_broadcaster" controller_active joint_state_broadcaster
wait_until "panda_arm_controller" controller_active panda_arm_controller
wait_until "panda_hand_controller" controller_active panda_hand_controller
wait_until "/joint_states sample" joint_state_available
wait_until "FollowJointTrajectory action" arm_action_available
wait_until "grasp adapter services" grasp_services_available

"${SCRIPT_DIR}/sync_to_jetson.sh"
if [[ "${PANDA_SKIP_BUILD:-0}" != "1" ]]; then
  ssh "${REMOTE}" "${REMOTE_ROOT}/infra/jetson/build.sh" planner
fi
ssh "${REMOTE}" "${REMOTE_ROOT}/infra/jetson/run.sh"
wait_until "Jetson move_group" move_group_ready
wait_until "RGB-D cube pose" cube_pose_available

echo "Planner is running in ${CONTAINER_NAME} on ${REMOTE}."
