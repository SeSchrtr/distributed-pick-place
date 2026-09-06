#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="/tmp/panda_simulation.pid"
LOG_FILE="/tmp/panda_simulation.log"

if [[ -f "${PID_FILE}" ]] && kill -0 "$(cat "${PID_FILE}")" 2>/dev/null; then
  kill -INT "$(cat "${PID_FILE}")" 2>/dev/null || true
  for _ in $(seq 1 10); do
    kill -0 "$(cat "${PID_FILE}")" 2>/dev/null || break
    sleep 1
  done
fi
# gz sim's own process, and the ros_gz/image_transport/grasp_adapter nodes it
# spawns, can outlive the launch script that started it (e.g. after a gzserver
# crash, since this launch file has no on-exit shutdown wired between them).
# The leading '^' anchor on the gz sim pattern stops it from ever matching our
# own process's cmdline (relevant when this text is embedded in an inline ssh
# command elsewhere).
pkill -KILL -f '^gz sim.*panda_table.sdf' 2>/dev/null || true
pkill -KILL -f 'ros_gz_bridge/parameter_bridge|image_transport/republish|panda_grasp_adapter/grasp_adapter|controller_manager/spawner' 2>/dev/null || true
rm -f "${PID_FILE}"

source "${SCRIPT_DIR}/env.sh"

# This host is headless (no display); RViz has nowhere to open and the GUI
# client would compete with the physics server for this weaker CPU's time
# (observed RTF ~0.13x with GUI + RViz both running). Defaults reflect that;
# args passed to this script (e.g. from launch_simulation.sh) still win
# because `ros2 launch` uses the last value given for a repeated argument.
# stdin must be /dev/null, not the ssh session's pipe, or the parent ssh
# command that started this will never return even though the launch is
# backgrounded.
nohup ros2 launch panda_demo_gazebo simulation.launch.py \
  launch_rviz:=false gz_headless_args:="-s --headless-rendering" "$@" \
  </dev/null >"${LOG_FILE}" 2>&1 &
echo $! >"${PID_FILE}"
disown

echo "Simulation started (pid $(cat "${PID_FILE}")); log at ${LOG_FILE} on this host."
