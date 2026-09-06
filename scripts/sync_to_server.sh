#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REMOTE="${SERVER_SSH_HOST:-thinkpad440sserver}"
REMOTE_ROOT="${SERVER_PROJECT_DIR:-panda_distributed_pick_place}"

ssh "${REMOTE}" mkdir -p "${REMOTE_ROOT}"
rsync --archive --delete \
  --exclude '.git/' \
  --exclude 'build/' \
  --exclude 'install/' \
  --exclude 'log/' \
  --exclude 'ros2_ws/build/' \
  --exclude 'ros2_ws/install/' \
  --exclude 'ros2_ws/log/' \
  --exclude 'runs/' \
  --exclude 'secrets.txt' \
  "${REPO_ROOT}/" "${REMOTE}:${REMOTE_ROOT}/"

echo "Synchronized repository to ${REMOTE}:${REMOTE_ROOT}"
