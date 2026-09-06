#!/usr/bin/env bash
set -euo pipefail

# Keeps only the newest N run directories under runs/ (default 20); deletes older ones.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
RUNS_DIR="${REPO_ROOT}/runs"
KEEP="${1:-20}"

if [[ ! -d "${RUNS_DIR}" ]]; then
  echo "No ${RUNS_DIR} yet; nothing to prune."
  exit 0
fi

mapfile -t runs < <(ls -1 "${RUNS_DIR}" | sort)
total=${#runs[@]}
if (( total <= KEEP )); then
  echo "${total} run(s) present, nothing to prune (keeping ${KEEP})."
  exit 0
fi

to_remove=$((total - KEEP))
for ((i = 0; i < to_remove; i++)); do
  echo "Removing runs/${runs[i]}"
  rm -rf "${RUNS_DIR:?}/${runs[i]}"
done
