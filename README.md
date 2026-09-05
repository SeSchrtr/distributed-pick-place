# Distributed ROS 2 Panda Pick and Place

[![CI](https://github.com/SeSchrtr/dummyROS2Sandbox/actions/workflows/ci.yml/badge.svg)](https://github.com/SeSchrtr/dummyROS2Sandbox/actions/workflows/ci.yml)

This project runs Gazebo Harmonic and `ros2_control` on an Ubuntu 24.04
ThinkPad while RGB-D perception, MoveIt 2, OMPL, and the pick-and-place state
machine run in an ARM64 ROS 2 Jazzy container on a Jetson Nano. The two sides
communicate only through ROS 2 / Cyclone DDS during planning and execution.

## Architecture

| ThinkPad | Jetson Nano container |
| --- | --- |
| Gazebo Sim + GUI | ROS 2 Jazzy |
| Panda physics + overhead RGB-D camera | RGB-D pose estimator (`panda_perception`) |
| `gz_ros2_control` controllers | MoveIt `move_group` |
| `image_transport` compressors + RViz depth view | OMPL, IK, collision checking |
| Grasp adapter + DetachableJoint | Pose-driven pick-and-place state machine |

Perception runs on the Jetson, co-located with planning, because the target
deployment wires the real camera directly into the Jetson; see
[`docs/adr/0001-perception-placement.md`](docs/adr/0001-perception-placement.md).
In simulation this means the camera feed, not the pose, crosses the network,
so it is compressed with `image_transport` first.

Both hosts use `ROS_DOMAIN_ID=42`, `rmw_cyclonedds_cpp`, subnet discovery, and
the container uses Docker host networking. See
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for interfaces and process
placement.

## Prerequisites

- ThinkPad: Ubuntu 24.04 with the official ROS 2 Jazzy apt repository enabled.
- Jetson Nano: reachable as `ssh jetson`, Docker usable without `sudo`, and
  enough free storage for the Jazzy/MoveIt image.
- Both machines on the same multicast-capable LAN.
- No ROS installation, OS upgrade, JetPack change, or Gazebo installation is
  required on the Jetson host.

Install any missing ThinkPad packages:

```bash
./infra/thinkpad/setup.sh
```

The script is idempotent. It invokes `sudo apt-get` only when a package is
missing and may therefore request the user's sudo password.

## Build And Start

Open three ThinkPad terminals in the repository.

Terminal 1 starts Gazebo and RViz, builds the ThinkPad packages, spawns the
Panda, activates all controllers, and starts the `image_transport` camera
compressors:

```bash
./scripts/launch_simulation.sh
```

Terminal 2 validates the simulation interfaces, synchronizes this repository,
builds the ARM64 image, starts `move_group` and the RGB-D perception
pipeline, and waits until both are visible:

```bash
./scripts/launch_planner.sh
```

For a previously built, unchanged image, skip the Docker build:

```bash
PANDA_SKIP_BUILD=1 ./scripts/launch_planner.sh
```

Terminal 3 executes the state machine on the Jetson:

```bash
./scripts/run_pick_place.sh
```

The node resets the detached cube to PICK at the beginning, so the command can
be repeated without restarting Gazebo. Success is reported only after the
remote log contains `STATE DONE`. It moves HOME, waits for a fresh
`/perception/cube_pose`, and uses that measured position for all pick targets;
the Gazebo model pose is not an input to the estimator.

RViz opens with `/perception/depth_colormap` visible. The image is a stable,
colorized depth view with the accepted cube contour, centroid, and median depth
overlaid. The 3D view also shows `/perception/cube_marker`. To run without RViz:

```bash
./scripts/launch_simulation.sh launch_rviz:=false
```

To test a manually moved cube without resetting it before the run:

```bash
PANDA_RESET_OBJECT=0 ./scripts/run_pick_place.sh
```

## Tests

Build the lightweight DDS image and run bidirectional LAN communication tests:

```bash
./scripts/sync_to_jetson.sh
ssh jetson panda_distributed_pick_place/infra/jetson/build.sh dds
./scripts/test_dds.sh
```

The full validation report, including host versions, controller/action names,
ARM64 proof, and the measured final cube pose, is in
[`docs/SETUP_REPORT.md`](docs/SETUP_REPORT.md).

## Stop

Stop Gazebo with `Ctrl+C` in Terminal 1. Stop the persistent Jetson planner:

```bash
./scripts/stop_planner.sh
```

## Configuration

The defaults can be overridden without editing files:

```text
JETSON_SSH_HOST=jetson
JETSON_PROJECT_DIR=panda_distributed_pick_place
PANDA_CONTAINER_NAME=panda-planner
PANDA_IMAGE_TAG=panda-planner:jazzy
PANDA_READY_TIMEOUT=60
PANDA_RESET_OBJECT=true
```

Do not source both host environment files manually. ThinkPad scripts source
`infra/thinkpad/env.sh`; container commands source `/opt/panda_env.sh`.

## Troubleshooting

Use [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md) for DDS discovery,
controller readiness, Docker build, Gazebo/VS Code Snap, and known upstream
warning diagnostics.
