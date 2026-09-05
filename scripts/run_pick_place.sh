#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REMOTE="${JETSON_SSH_HOST:-jetson}"
CONTAINER_NAME="${PANDA_CONTAINER_NAME:-panda-planner}"
READY_TIMEOUT="${PANDA_READY_TIMEOUT:-60}"
LOG_FILE="$(mktemp /tmp/panda_pick_place.XXXXXX.log)"
trap 'rm -f "${LOG_FILE}"' EXIT

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

joint_state_available() {
  timeout 15 ros2 topic echo /joint_states --once >/dev/null 2>&1
}

interfaces_available() {
  local actions services
  actions="$(ros2 action list 2>/dev/null)"
  services="$(ros2 service list 2>/dev/null)"
  grep -qx '/panda_arm_controller/follow_joint_trajectory' <<<"${actions}" &&
    grep -qx '/panda_hand_controller/gripper_cmd' <<<"${actions}" &&
    grep -qx '/grasp/attach' <<<"${services}" &&
    grep -qx '/grasp/detach' <<<"${services}" &&
    grep -qx '/grasp/reset_object' <<<"${services}"
}

move_group_ready() {
  ssh "${REMOTE}" docker exec "${CONTAINER_NAME}" bash -lc \
    "'source /opt/panda_env.sh && ros2 node list 2>/dev/null'" |
    grep -qx '/move_group'
}

cube_pose_available() {
  timeout 15 ros2 topic echo /perception/cube_pose --once >/dev/null 2>&1
}

wait_until "/joint_states sample" joint_state_available
wait_until "simulation actions and grasp services" interfaces_available
wait_until "RGB-D cube pose" cube_pose_available
wait_until "Jetson move_group" move_group_ready

ssh "${REMOTE}" docker exec "${CONTAINER_NAME}" bash -lc \
  "'source /opt/panda_env.sh && ros2 launch panda_pick_place pick_place.launch.py reset_object:=${PANDA_RESET_OBJECT:-true}'" \
  2>&1 | tee "${LOG_FILE}"

if ! grep -q 'STATE DONE' "${LOG_FILE}"; then
  echo "Pick-and-place did not reach STATE DONE." >&2
  exit 1
fi

echo "Pick-and-place completed successfully on ${REMOTE}."
