#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REMOTE="${JETSON_SSH_HOST:-jetson}"
REMOTE_ROOT="${JETSON_PROJECT_DIR:-panda_distributed_pick_place}"
IMAGE="panda-planner:dds"
TEST_CONTAINER="panda-dds-test"
TIMEOUT_SECONDS="${DDS_TEST_TIMEOUT:-25}"
LOCAL_PID=""

source "${REPO_ROOT}/infra/thinkpad/env.sh"

cleanup() {
  ssh "${REMOTE}" docker rm --force "${TEST_CONTAINER}" >/dev/null 2>&1 || true
  if [[ -n "${LOCAL_PID}" ]]; then
    kill "${LOCAL_PID}" >/dev/null 2>&1 || true
    wait "${LOCAL_PID}" >/dev/null 2>&1 || true
    LOCAL_PID=""
  fi
}
trap cleanup EXIT

echo "Test 1/2: ThinkPad talker -> Jetson listener"
ssh "${REMOTE}" docker run --detach --name "${TEST_CONTAINER}" --network host \
  --env ROS_DOMAIN_ID=42 \
  --env RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
  --env ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET \
  "${IMAGE}" ros2 run demo_nodes_cpp listener >/dev/null
ros2 run demo_nodes_cpp talker >/tmp/panda_dds_talker.log 2>&1 &
LOCAL_PID=$!

deadline=$((SECONDS + TIMEOUT_SECONDS))
until ssh "${REMOTE}" docker logs "${TEST_CONTAINER}" 2>&1 | grep -q 'I heard:'; do
  if (( SECONDS >= deadline )); then
    echo "Jetson listener received no ThinkPad samples within ${TIMEOUT_SECONDS}s" >&2
    exit 1
  fi
  sleep 1
done
ssh "${REMOTE}" docker logs "${TEST_CONTAINER}" 2>&1 | grep -m 1 'I heard:'
cleanup

echo "Test 2/2: Jetson talker -> ThinkPad listener"
ros2 run demo_nodes_cpp listener >/tmp/panda_dds_listener.log 2>&1 &
LOCAL_PID=$!
ssh "${REMOTE}" docker run --detach --name "${TEST_CONTAINER}" --network host \
  --env ROS_DOMAIN_ID=42 \
  --env RMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
  --env ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET \
  "${IMAGE}" ros2 run demo_nodes_cpp talker >/dev/null

deadline=$((SECONDS + TIMEOUT_SECONDS))
until grep -q 'I heard:' /tmp/panda_dds_listener.log 2>/dev/null; do
  if (( SECONDS >= deadline )); then
    echo "ThinkPad listener received no Jetson samples within ${TIMEOUT_SECONDS}s" >&2
    exit 1
  fi
  sleep 1
done
grep -m 1 'I heard:' /tmp/panda_dds_listener.log

echo "Bidirectional Cyclone DDS test passed on domain ${ROS_DOMAIN_ID}."
