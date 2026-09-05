#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

"${SCRIPT_DIR}/build_thinkpad.sh" --packages-up-to \
  panda_demo_description panda_grasp_adapter panda_demo_gazebo

source "${REPO_ROOT}/infra/thinkpad/env.sh"

# VS Code installed as a Snap exports GTK paths that make native Gazebo load
# incompatible core20 libraries. Gazebo must use the host's Noble libraries.
unset SNAP SNAP_ARCH SNAP_COMMON SNAP_CONTEXT SNAP_COOKIE SNAP_DATA SNAP_EUID
unset SNAP_INSTANCE_NAME SNAP_LIBRARY_PATH SNAP_NAME SNAP_REAL_HOME SNAP_REVISION
unset SNAP_UID SNAP_USER_COMMON SNAP_USER_DATA SNAP_VERSION
unset GTK_EXE_PREFIX GTK_PATH GDK_PIXBUF_MODULEDIR GDK_PIXBUF_MODULE_FILE
unset GIO_MODULE_DIR GTK_IM_MODULE_FILE

exec ros2 launch panda_demo_gazebo simulation.launch.py "$@"
