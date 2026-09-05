#!/usr/bin/env bash
set -euo pipefail

REMOTE="${JETSON_SSH_HOST:-jetson}"
CONTAINER_NAME="${PANDA_CONTAINER_NAME:-panda-planner}"

if ssh "${REMOTE}" docker container inspect "${CONTAINER_NAME}" >/dev/null 2>&1; then
  ssh "${REMOTE}" docker rm --force "${CONTAINER_NAME}" >/dev/null
  echo "Stopped ${CONTAINER_NAME} on ${REMOTE}."
else
  echo "Planner container ${CONTAINER_NAME} is already stopped."
fi

