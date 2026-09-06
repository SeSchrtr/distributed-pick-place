#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REMOTE="${JETSON_SSH_HOST:-jetson}"
CONTAINER_NAME="${PANDA_CONTAINER_NAME:-panda-planner}"
READY_TIMEOUT="${PANDA_READY_TIMEOUT:-60}"
# Off by default: see infra/thinkpad/record_screen.sh and docs/TROUBLESHOOTING.md
# for why GNOME's screencast D-Bus API can't currently be driven unattended.
RECORD_VIDEO="${PANDA_RECORD_VIDEO:-0}"

# Every run gets its own directory for traceability: full log, a rosbag2 of
# joint states/TF/cube pose/trajectory action feedback and grasp service
# calls, an optional screen recording, and a manifest tying it all together.
RUN_DIR="${REPO_ROOT}/runs/$(date -u +%Y%m%dT%H%M%SZ)_pick_place"
mkdir -p "${RUN_DIR}"
LOG_FILE="${RUN_DIR}/pick_place.log"
BAG_DIR="${RUN_DIR}/rosbag"
VIDEO_FILE="${RUN_DIR}/gazebo.webm"

source "${REPO_ROOT}/infra/thinkpad/env.sh"

BAG_PID=""
VIDEO_STARTED=0

cleanup() {
  if [[ -n "${BAG_PID}" ]] && kill -0 "${BAG_PID}" 2>/dev/null; then
    kill -INT "${BAG_PID}" 2>/dev/null || true
    wait "${BAG_PID}" 2>/dev/null || true
  fi
  if [[ "${VIDEO_STARTED}" == "1" ]]; then
    "${SCRIPT_DIR}/../infra/thinkpad/record_screen.sh" stop \
      >>"${RUN_DIR}/record_screen.log" 2>&1 || true
  fi
}
trap cleanup EXIT

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

ros2 bag record -o "${BAG_DIR}" \
  --topics /joint_states /tf /tf_static /perception/cube_pose /perception/cube_marker \
    /panda_arm_controller/follow_joint_trajectory/_action/feedback \
    /panda_arm_controller/follow_joint_trajectory/_action/status \
    /panda_hand_controller/gripper_cmd/_action/feedback \
    /panda_hand_controller/gripper_cmd/_action/status \
  --services /grasp/attach /grasp/detach /grasp/reset_object \
  >"${RUN_DIR}/rosbag_record.log" 2>&1 &
BAG_PID=$!

if [[ "${RECORD_VIDEO}" == "1" ]]; then
  if "${SCRIPT_DIR}/../infra/thinkpad/record_screen.sh" start "${VIDEO_FILE}" \
      >"${RUN_DIR}/record_screen.log" 2>&1
  then
    VIDEO_STARTED=1
  else
    echo "Screen recording could not be started; continuing without video (see ${RUN_DIR}/record_screen.log)." >&2
  fi
fi

set +e
ssh "${REMOTE}" docker exec "${CONTAINER_NAME}" bash -lc \
  "'source /opt/panda_env.sh && ros2 launch panda_pick_place pick_place.launch.py reset_object:=${PANDA_RESET_OBJECT:-true}'" \
  2>&1 | tee "${LOG_FILE}"
RUN_STATUS=${PIPESTATUS[0]}
set -e

if grep -q 'STATE DONE' "${LOG_FILE}"; then
  RESULT="DONE"
else
  RESULT="FAILED"
fi

cat > "${RUN_DIR}/manifest.txt" <<EOF
started_utc=$(basename "${RUN_DIR}" | cut -d_ -f1)
repo_commit=$(git -C "${REPO_ROOT}" rev-parse HEAD 2>/dev/null || echo unknown)
remote_host=${REMOTE}
container_name=${CONTAINER_NAME}
reset_object=${PANDA_RESET_OBJECT:-true}
ssh_exit_code=${RUN_STATUS}
result=${RESULT}
log_file=pick_place.log
rosbag_dir=rosbag
video_file=$([[ "${VIDEO_STARTED}" == "1" ]] && echo "gazebo.webm" || echo "none")
EOF

echo "Run artifacts saved to ${RUN_DIR}"

if [[ "${RESULT}" != "DONE" ]]; then
  echo "Pick-and-place did not reach STATE DONE." >&2
  exit 1
fi

echo "Pick-and-place completed successfully on ${REMOTE}."
