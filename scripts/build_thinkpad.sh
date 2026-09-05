#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

source "${REPO_ROOT}/infra/thinkpad/env.sh"

colcon --log-base "${REPO_ROOT}/ros2_ws/log" build \
  --base-paths "${REPO_ROOT}/ros2_ws/src" \
  --build-base "${REPO_ROOT}/ros2_ws/build" \
  --install-base "${REPO_ROOT}/ros2_ws/install" \
  --symlink-install \
  --packages-select panda_demo_description panda_demo_gazebo panda_grasp_adapter \
  "$@"
