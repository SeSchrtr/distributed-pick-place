#!/usr/bin/env bash
set -euo pipefail

REMOTE="${SERVER_SSH_HOST:-thinkpad440sserver}"

# The remote script must be passed as a single argument: ssh joins multiple
# unquoted argv entries with spaces before handing them to the remote shell,
# which would otherwise re-split this on the embedded ';' and newlines.
ssh "${REMOTE}" '
  PID_FILE="/tmp/panda_simulation.pid"
  if [[ -f "${PID_FILE}" ]] && kill -0 "$(cat "${PID_FILE}")" 2>/dev/null; then
    kill -INT "$(cat "${PID_FILE}")"
    for _ in $(seq 1 10); do
      kill -0 "$(cat "${PID_FILE}")" 2>/dev/null || break
      sleep 1
    done
    pkill -KILL -f "^gz sim.*panda_table.sdf" 2>/dev/null || true
    rm -f "${PID_FILE}"
    echo "Stopped simulation."
  else
    echo "Simulation is already stopped."
  fi
'
