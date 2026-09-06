#!/usr/bin/env bash
set -euo pipefail

# Gazebo now runs on a separate simulation host (thinkpad440sserver), not on
# this dev machine, so this script only orchestrates it over SSH: sync repo,
# build the simulation-side packages there, and start it headless.
# Local-run fallback (this machine hosting Gazebo directly) still exists as
# infra/thinkpad/setup.sh + build_thinkpad.sh, but is no longer the default path.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REMOTE="${SERVER_SSH_HOST:-thinkpad440sserver}"
REMOTE_ROOT="${SERVER_PROJECT_DIR:-panda_distributed_pick_place}"

"${SCRIPT_DIR}/sync_to_server.sh"
if [[ "${PANDA_SKIP_BUILD:-0}" != "1" ]]; then
  ssh "${REMOTE}" "${REMOTE_ROOT}/infra/thinkpadt440sserver/build.sh"
fi
ssh "${REMOTE}" "${REMOTE_ROOT}/infra/thinkpadt440sserver/run.sh" "$@"

echo "Simulation launched on ${REMOTE}. Tail /tmp/panda_simulation.log there for details."
