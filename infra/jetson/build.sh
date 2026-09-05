#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
TARGET="${1:-planner}"

case "${TARGET}" in
  dds)
    IMAGE_TAG="panda-planner:dds"
    ;;
  planner)
    IMAGE_TAG="panda-planner:jazzy"
    ;;
  *)
    echo "Unknown Docker target: ${TARGET} (expected dds or planner)" >&2
    exit 2
    ;;
esac

docker build \
  --target "${TARGET}" \
  --tag "${IMAGE_TAG}" \
  --file "${SCRIPT_DIR}/Dockerfile" \
  "${REPO_ROOT}"

