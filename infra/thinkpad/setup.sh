#!/usr/bin/env bash
set -euo pipefail

PACKAGES=(
  ros-jazzy-desktop
  ros-dev-tools
  ros-jazzy-ros-gz
  ros-jazzy-gz-ros2-control
  ros-jazzy-ros2-control
  ros-jazzy-ros2-controllers
  ros-jazzy-rmw-cyclonedds-cpp
  ros-jazzy-tf2-geometry-msgs
  ros-jazzy-xacro
  ros-jazzy-image-transport-plugins
  ros-jazzy-moveit-resources-panda-description
)

missing=()
for package in "${PACKAGES[@]}"; do
  if ! dpkg-query --show --showformat='${db:Status-Status}\n' "${package}" 2>/dev/null |
    grep -qx 'installed'; then
    missing+=("${package}")
  fi
done

if (( ${#missing[@]} == 0 )); then
  echo "All required ThinkPad packages are installed."
  exit 0
fi

echo "Installing missing ThinkPad packages: ${missing[*]}"
sudo apt-get update
sudo apt-get install -y "${missing[@]}"
