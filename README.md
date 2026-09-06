# Distributed ROS 2 Panda Pick and Place

[![CI](https://github.com/SeSchrtr/distributed-pick-place/actions/workflows/ci.yml/badge.svg)](https://github.com/SeSchrtr/distributed-pick-place/actions/workflows/ci.yml)

This project runs Gazebo Harmonic and `ros2_control` headless on a dedicated
Ubuntu/Linux Mint simulation host while RGB-D perception, MoveIt 2, OMPL, and
the pick-and-place state machine run in an ARM64 ROS 2 Jazzy container on a
Jetson Nano. A separate WiFi-only dev machine only builds, orchestrates over
SSH, and records a rosbag; it runs no simulation nodes of its own. The hosts
communicate only through ROS 2 / Cyclone DDS during planning and execution.

## Architecture

| Simulation host | Jetson Nano container |
| --- | --- |
| Gazebo Sim (headless) | ROS 2 Jazzy |
| Panda physics + overhead RGB-D camera | RGB-D pose estimator (`panda_perception`) |
| `gz_ros2_control` controllers | MoveIt `move_group` |
| `image_transport` compressors | OMPL, IK, collision checking |
| Grasp adapter + DetachableJoint | Pose-driven pick-and-place state machine |

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), "Three-Host Topology",
for the current dev machine / simulation host / Jetson mapping.

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

- Simulation host: reachable as `ssh thinkpadt440sserver` (override with
  `SERVER_SSH_HOST`), with ROS 2 Jazzy and Gazebo Harmonic installed and DDS
  pinned to its wired interface (see `infra/thinkpadt440sserver/env.sh`).
- Jetson Nano: reachable as `ssh jetson`, Docker usable without `sudo`, and
  enough free storage for the Jazzy/MoveIt image.
- All hosts on the same multicast-capable LAN.
- No ROS installation, OS upgrade, JetPack change, or Gazebo installation is
  required on the Jetson host.

Install any missing dev-machine packages:

```bash
./infra/thinkpad/setup.sh
```

The script is idempotent. It invokes `sudo apt-get` only when a package is
missing and may therefore request the user's sudo password.

## Build And Start

Open three dev-machine terminals in the repository.

Terminal 1 syncs the repo to the simulation host, builds the simulation-side
packages there, and starts headless Gazebo, spawning the Panda, activating
all controllers, and starting the `image_transport` camera compressors:

```bash
./scripts/launch_simulation.sh
```

For a previously built, unchanged simulation, skip the remote build:

```bash
PANDA_SKIP_BUILD=1 ./scripts/launch_simulation.sh
```

To stop it later:

```bash
./scripts/stop_simulation.sh
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

The simulation host runs headless (no RViz); `/perception/depth_colormap` and
`/perception/cube_marker` are still published for debugging and can be viewed
by pointing a local RViz instance at the shared `ROS_DOMAIN_ID` from any host
on the LAN.

To test a manually moved cube without resetting it before the run:

```bash
PANDA_RESET_OBJECT=0 ./scripts/run_pick_place.sh
```

## Tests

`panda_perception` and `panda_pick_place` factor their pure logic (median/MAD
filtering, plausibility bounds, depth colormap, pose/collision-shape helpers)
into headers under `include/`, covered by `ament_cmake_gtest` unit tests that
run in CI without Gazebo, MoveIt, or a Jetson:

```bash
colcon test --packages-select panda_perception panda_pick_place
colcon test-result --verbose
```

Build the lightweight DDS image and run bidirectional LAN communication tests:

```bash
./scripts/sync_to_jetson.sh
ssh jetson panda_distributed_pick_place/infra/jetson/build.sh dds
./scripts/test_dds.sh
```

The full validation report, including host versions, controller/action names,
ARM64 proof, and the measured final cube pose, is in
[`docs/SETUP_REPORT.md`](docs/SETUP_REPORT.md).

## Traceability: Per-Run Artifacts

Every `./scripts/run_pick_place.sh` invocation writes a timestamped, git-ignored
directory under `runs/` (not committed; each run can be a few hundred MB) containing:

```text
runs/<UTC timestamp>_pick_place/
  pick_place.log   # full stdout/stderr of the state machine, incl. STATE lines
  rosbag/          # joint_states, TF, cube pose/marker, trajectory and
                   # gripper action feedback, grasp service calls
  gazebo.webm      # screen recording, if PANDA_RECORD_VIDEO=1 (default)
  manifest.txt     # commit hash, result, reset_object, timestamps
```

The log is no longer deleted after the run (it previously was). `gazebo.webm`
is aspirational for now: [`infra/thinkpad/record_screen.sh`](infra/thinkpad/record_screen.sh)
is a stub, since GNOME Shell 46 removed the D-Bus method it was meant to
wrap and the replacements each need either an undocumented private API or a
one-time interactive consent dialog plus a PipeWire capture pipeline (see
[`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md)). `PANDA_RECORD_VIDEO`
defaults to `0` until that's implemented; the rosbag and log always record.
Prune old runs with:

```bash
./scripts/prune_runs.sh 20   # keep the newest 20 runs, delete the rest
```

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
PANDA_RECORD_VIDEO=0
```

Do not source both host environment files manually. ThinkPad scripts source
`infra/thinkpad/env.sh`; container commands source `/opt/panda_env.sh`.

## Troubleshooting

Use [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md) for DDS discovery,
controller readiness, Docker build, Gazebo/VS Code Snap, and known upstream
warning diagnostics.

## License

MIT, see [`LICENSE`](LICENSE).
