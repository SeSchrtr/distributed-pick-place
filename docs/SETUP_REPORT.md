# Setup And Validation Report

> This report documents the topology in place at the time of the run below,
> with perception (`panda_perception`) on the ThinkPad. Perception has since
> moved to the Jetson (see
> [ADR-0001](adr/0001-perception-placement.md)); the numbers and log evidence
> here are still accurate for the components they describe (Gazebo, control,
> MoveIt, grasping), but a full rerun with the new placement is a recommended
> follow-up and not yet reflected in this file.

## Observed Hosts

Measurements were taken on 2026-08-29 and rechecked on 2026-09-06.

| Property | ThinkPad | Jetson Nano host |
| --- | --- | --- |
| OS | Ubuntu 24.04.4 LTS | Ubuntu 18.04.6 LTS |
| Architecture | x86_64 | aarch64 |
| Kernel | 6.17 series | 4.9.337-tegra |
| LAN | `wlp4s0`, `192.168.188.37/24` | `wlan0`, `192.168.188.52/24` |
| Docker | not used for runtime | 20.10.21 |
| ROS on host | Jazzy | none (`0` installed `ros-*` packages) |

The addresses are recorded observations, not runtime configuration. Discovery
uses the active subnet and does not hard-code either address.

## Installed Runtime

ThinkPad:

- ROS 2 Jazzy
- Gazebo Sim 8.11.0 (Harmonic)
- `ros-jazzy-ros-gz` 1.0.22
- `ros-jazzy-gz-ros2-control` 1.2.19
- `ros-jazzy-ros2-control` 4.45.2
- `ros-jazzy-ros2-controllers` 4.40.1
- `ros-jazzy-rmw-cyclonedds-cpp` 2.2.3
- `cv_bridge`, OpenCV, TF2, and RViz 2 from ROS 2 Jazzy
- official `moveit_resources_panda_description` 3.1.0

Jetson container:

- Base image `ros:jazzy-ros-base-noble`
- Ubuntu 24.04 userspace, ROS 2 Jazzy, Cyclone DDS
- MoveIt 2.12.4 binary packages, OMPL planner 2.12.4, and official Panda
  MoveIt resources 3.1.0
- project-built `panda_demo_moveit_config` and `panda_pick_place`
- host networking, no privileged mode, no NVIDIA runtime, no GUI process

## Incremental Validation

| Stage | Result | Evidence |
| --- | --- | --- |
| A | PASS | ThinkPad 24.04/x86_64; Jetson 18.04/aarch64; Docker works without sudo |
| B | PASS | ROS 2 Jazzy native and in ARM64 container; `ros2 --help` succeeds |
| C | PASS | Cyclone DDS domain 42 talker/listener passed in both LAN directions |
| D | PASS | Gazebo Harmonic server and GUI started on ThinkPad |
| E | PASS | Panda, table, ground, pick/place markers, and cube spawned |
| F | PASS | joint-state, arm trajectory, and gripper controllers active |
| G | PASS | Jetson received all nine Panda joints from `/joint_states` |
| H | PASS | `move_group` loaded Panda/KDL/OMPL and remote controllers on Jetson |
| I | PASS | Jetson planned 12 points and executed them through ThinkPad controller |
| J | PASS | Full state machine reached `STATE DONE`; cube visibly changed location |
| K | PASS | RGB-D pose drove pick from both nominal and deliberately shifted starts |

DDS sample evidence included `Hello World` received by the Jetson listener and
the reverse sample received by the ThinkPad listener. Stage I reported
`hostname=user-desktop architecture=aarch64` and the ThinkPad joint state
converged to the requested seven-joint target.

The Stage J run logged every required state: `INITIALIZE`, `HOME`,
`PRE_GRASP`, `GRASP`, `CLOSE`, `ATTACH`, `LIFT`, `PRE_PLACE`, `PLACE`,
`DETACH`, `OPEN`, `RETREAT`, `HOME`, and `DONE`. Gazebo independently reported
the final cube center at approximately:

```text
x = 0.4492 m
y = -0.2564 m
z = 0.7750 m
```

The initial cube center was approximately `(0.4500, 0.2000, 0.7750)`, proving a
physical displacement from PICK to PLACE rather than only a Planning Scene
update.

The final operating script was then executed twice without restarting Gazebo.
Both runs reached `STATE DONE`; after the second run Gazebo measured the cube at
approximately `(0.4421, -0.2502, 0.7750)`. This also validates the reset and
stale-attachment recovery path.

## RGB-D Validation

The overhead RGB-D sensor publishes aligned 320 x 240 RGB, `32FC1` depth, and
CameraInfo streams at 5 Hz simulation rate. The ThinkPad estimator publishes a
reliable `PoseStamped` in `world`, a marker, and an annotated `bgr8` depth
colormap. The nominal estimate was:

```text
estimated = (0.4515, 0.1970, 0.7750) m
modeled   = (0.4500, 0.2000, 0.7750) m
```

For an independent behavior check, Gazebo's test-only `set_pose` service moved
the cube to `(0.5200, 0.1200, 0.7800)` before execution. Perception reported
`(0.5197, 0.1175, 0.7750)`, and the Jetson log confirmed
`Using measured cube pose x=0.520 y=0.117 z=0.775`. With
`PANDA_RESET_OBJECT=0`, the complete state machine still reached `STATE DONE`.
Gazebo then measured the physically placed cube at approximately:

```text
x = 0.4456 m
y = -0.2482 m
z = 0.7750 m
```

This proves that pick planning uses the camera estimate rather than the old
fixed pick coordinates or Gazebo ground truth.

## Verified Process Separation

Jetson Docker process list contained both:

```text
ros2 launch panda_demo_moveit_config planner.launch.py
move_group
```

The pick-and-place log inside the same container reported `architecture=aarch64`.
The ThinkPad process tree contained Gazebo Sim/GUI, `gz_ros2_control`,
`controller_manager`, `robot_state_publisher`, bridge, and grasp adapter. No
Gazebo, perception, RViz, or GUI process ran on the Jetson.

## Implementation Notes

- Binary MoveIt packages were available; no MoveIt source build was needed.
- The grasp adapter uses Gazebo Transport because DetachableJoint's StringMsg
  status and Empty command topics are not a useful direct ROS service API.
- DetachableJoint is parented to `panda_link7`; Gazebo collapses the fixed
  `panda_hand` link during model conversion.
- `/grasp/reset_object` uses Gazebo's `set_pose` service after detaching. This
  makes repeated runs deterministic while leaving all motion planning genuine.
- A 5 mm planning-only clearance keeps the cube collision primitive from being
  classified as penetrating the table when measured contact is exactly at the
  tabletop. It does not alter the measured grasp target or physical cube pose.
- The Jetson host OS, L4T, kernel, Docker installation, drivers, and package set
  were not upgraded or modified.
