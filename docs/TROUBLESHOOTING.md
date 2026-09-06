# Troubleshooting

## A Readiness Script Times Out

Run the same checks manually after sourcing the ThinkPad environment:

```bash
source infra/thinkpad/env.sh
ros2 control list_controllers
ros2 action list
ros2 service list | grep grasp
ros2 topic echo /joint_states --once
ros2 topic echo /perception/cube_pose --once
```

Expected controllers are `joint_state_broadcaster`, `panda_arm_controller`, and
`panda_hand_controller`, all `active`. Restart `launch_simulation.sh` if the
world or controller manager was stopped. `/perception/cube_pose` is produced
on the Jetson (see below) and only appears once `launch_planner.sh` has
started the perception pipeline there; it is normal for it to be absent right
after `launch_simulation.sh` alone.

## DDS Discovery Fails

Confirm on both machines/container:

```text
ROS_DOMAIN_ID=42
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
```

Then run `./scripts/test_dds.sh`. Confirm Wi-Fi client isolation is disabled
and both machines are on the same LAN. Do not add fixed peers or firewall rules
until normal multicast failure has been demonstrated. The Docker container
must use `--network host`.

## Planner Container Does Not Start

Inspect it without changing the Jetson host:

```bash
ssh jetson 'docker ps -a --filter name=panda-planner'
ssh jetson 'docker logs --tail 200 panda-planner'
ssh jetson 'df -h; free -h'
```

Rebuild from repository source with `./scripts/launch_planner.sh`. The first
MoveIt ARM64 build downloads a large binary dependency set; later builds use
Docker cache.

## Gazebo Fails From The VS Code Terminal

VS Code installed through Snap may export GTK/library paths for its base image.
Those paths can make native Gazebo load incompatible libraries.
`launch_simulation.sh` removes the relevant Snap and GTK variables before
launching Gazebo. Start Gazebo through that script, not by copying its final
`ros2 launch` command into an unclean shell.

## Pick And Place Stops At ERROR

The node logs the failed state and aborts without issuing later commands.
Inspect both ends:

```bash
ssh jetson 'docker logs --tail 200 panda-planner'
source infra/thinkpad/env.sh
ros2 control list_controllers
ros2 service call /grasp/detach std_srvs/srv/Trigger '{}'
ros2 service call /grasp/reset_object std_srvs/srv/Trigger '{}'
```

Run `./scripts/run_pick_place.sh` again after the underlying interface is ready.
Initialization detaches, opens the gripper, resets the cube, and clears stale
MoveIt attachments before moving HOME. Pose acquisition happens after HOME so
an arm left over the cube by an interrupted run cannot permanently occlude it.

## No Cube Pose Or Depth Image

The RGB-D estimator runs on the Jetson (see
[`docs/ARCHITECTURE.md`](ARCHITECTURE.md)); only the raw camera topics and
their compressed cross-host copies exist on the ThinkPad. Check the hop in
both directions:

```bash
source infra/thinkpad/env.sh
ros2 topic echo /overhead_camera/camera_info --once
ros2 topic echo /overhead_camera/depth_image --once --field encoding
ros2 topic hz /overhead_camera/image/compressed
ros2 topic hz /overhead_camera/depth_image/compressedDepth
ros2 topic echo /perception/depth_colormap --once --field encoding
ros2 topic echo /perception/cube_pose --once
ssh jetson docker logs --tail 200 panda-planner
```

Expected encodings are `32FC1` for raw depth and `bgr8` for the annotated view.
If the compressed topics on the ThinkPad have no publisher rate, the
`image_transport` republish nodes in `simulation.launch.py` did not start; if
they publish but the Jetson container log shows no decompressor activity,
check that `ros-jazzy-image-transport-plugins` is present in the Jetson image
and that both hosts share `ROS_DOMAIN_ID=42`. The cube must be visible and
blue. The estimator deliberately withholds poses for stale RGB/depth pairs,
fewer than 20 valid object pixels, noisy median depth, missing TF, or
coordinates outside the table workspace. Its throttled warnings (in the
Jetson container log) state which check failed. RViz can be disabled
independently with `launch_rviz:=false`; perception continues to run.

## No Video In `runs/<...>/gazebo.webm`

This doesn't happen yet because it isn't implemented: `PANDA_RECORD_VIDEO`
defaults to `0`. `infra/thinkpad/record_screen.sh` was originally written
against `org.gnome.Shell.Screencast`, a D-Bus method that GNOME Shell 46
removed (confirmed live: `gdbus` returns `UnknownMethod`). The two
replacements are `org.gnome.Mutter.ScreenCast` (private/unstable, undocumented,
intended only for `xdg-desktop-portal-gnome`'s own use) and
`org.freedesktop.portal.ScreenCast` (the stable, documented API, confirmed
present here). The portal is the correct long-term fix, but `Start()` shows a
one-time interactive "Share" consent dialog per session (Wayland's security
model disallows silent capture by design) and getting frames out requires
consuming a PipeWire stream, e.g. via `gst-launch-1.0 pipewiresrc`. Until that
is built, the practical options are: (1) trigger GNOME's own built-in
recorder manually (`Ctrl+Alt+Shift+R`, saves to `~/Videos`) and move the file
into the run's directory by hand when a demo is needed; (2) implement the
portal + PipeWire pipeline and accept the one-time consent click per session;
or (3) skip live desktop capture and instead record a demo by replaying a
saved rosbag through RViz. The rosbag and text log are unaffected either way.

## Duplicate Stale Processes Corrupt The DDS Graph

A cleanup pass that only kills the "obvious" big processes (`gz sim`,
`robot_state_publisher`, `static_transform_publisher`, `ros2_control_node`)
can miss `parameter_bridge`, `rviz2`, `republish`, and `grasp_adapter`
instances left over from a previous, incompletely-stopped run. Two
`parameter_bridge` processes both bridging `/clock` produces two independent
`/clock` publishers, which shows up as "Detected jump back in time. Clearing
TF buffer." warnings scattered across `rviz`, `move_group`, and
`cube_pose_estimator`. Diagnose with:

```bash
ros2 topic info /clock -v   # should show exactly one publisher
ps -eo pid,etime,cmd | grep -E 'gz sim|parameter_bridge|rviz2|republish|static_transform|robot_state_publisher|grasp_adapter'
```

Cross-check `etime` against when you actually started the current stack; kill
anything older. Prefer restarting cleanly (`pkill` by the full process list
above, not just `gz sim`) over trying to reuse a partially-torn-down stack.

## Simulation Runs Far Below Real Time (Low RTF)

Gazebo's own GUI client (`gz sim gui`, separate from any RViz window) and
RViz are both significant CPU consumers. Running both alongside the physics
server on a modest laptop can drop the real-time factor to ~0.1-0.2x, which
then makes every wall-clock-based readiness timeout elsewhere in the stack
(e.g. `launch_planner.sh`'s 60s cube-pose wait) fail spuriously even though
the pipeline is otherwise healthy. Measure RTF by sampling `/clock`'s `sec`
field at the start and end of a wall-clock window:

```bash
timeout 20 stdbuf -oL ros2 topic echo /clock --field clock.sec > /tmp/clk.log
# RTF ≈ (last sample - first sample) / 20
```

Fix: launch headless for any automated/recorded run —
`launch_rviz:=false gz_headless_args:="-s --headless-rendering"` passed to
`launch_simulation.sh`. `-s` skips Gazebo's own GUI client entirely;
`--headless-rendering` keeps the RGB-D camera sensor working without a
window. This alone took one observed run from ~13% RTF to effectively 1x.

## Cross-Host RGB-D Perception Fails Over WiFi (High Latency/Packet Loss)

The most stubborn recurring failure in this project turned out to be the
ThinkPad↔Jetson **WiFi link itself**, not application code. Symptoms:
`cube_pose_estimator` logs a growing "RGB and depth frames are not
synchronized" delta (seconds to minutes), or goes completely silent (no
further logging of any kind, including its success path) while still
consuming CPU, and `/perception/cube_pose` stops publishing. `pick_place`
then fails with "Timeout waiting for RGB-D cube pose" or "No fresh RGB-D
cube estimate received".

Root cause: WiFi congestion/interference causes real packet loss (observed
5-10%) and huge latency jitter (observed up to 350ms, mdev 80-115ms) even
with good signal strength — check with:

```bash
ping -c 20 -i 0.3 user-desktop.fritz.box
nmcli -f SIGNAL,RATE device wifi list
```

Because the compressed depth stream produces larger UDP-fragmented messages
than compressed color, it is disproportionately affected — best-effort QoS
just drops what doesn't arrive in time, so color keeps flowing while depth
silently stalls, sometimes for minutes.

**The real fix is a wired connection.** Plugging both the ThinkPad and the
Jetson into the router via Ethernet (instead of WiFi) eliminated the loss and
jitter entirely (observed: 0% loss, ~0.3ms latency, vs 100+ms average and up
to 10% loss over WiFi) and the perception pipeline has been reliable since.
If only WiFi is available, `nmcli radio wifi off` after connecting the wired
interface avoids dual-homing the same subnet on two interfaces, which can
otherwise itself cause DDS discovery to stall.

Two secondary code-level mitigations also help ride out short stalls (but do
**not** fix a sustained multi-minute WiFi outage — only a good link does):
- `cube_pose_estimator.cpp`: RGB/depth sync tolerance raised from 0.25s to
  3.0s (the demo's cube is static, so pairing a slightly-stale depth frame
  with a fresh color frame is still positionally correct).
- `pick_place.cpp`'s `wait_for_cube_pose()`: wait timeout raised from 30s to
  90s.

If `cube_pose_estimator` ever goes fully silent (no log line for minutes)
even after these fixes, a full `docker rm -f panda-planner` + relaunch is
currently the only known way to un-wedge it — a `restart` in place isn't
enough, since the underlying subscription state seems to get stuck rather
than just delayed.

## OMPL Planning Occasionally Fails On A Reachable Pose

MoveIt's default sampling-based planner (OMPL) is stochastic; the exact same,
genuinely reachable and collision-free pose can occasionally fail to produce
a plan within one attempt (observed on `STATE RETREAT`, using the identical
target pose that `STATE PRE_PLACE` had just reached fine moments earlier in
the same run). `pick_place.cpp`'s `plan_and_execute()` now retries planning
up to 3 times before giving up, which is a standard, safe mitigation for this
kind of transient planner failure.

## Rosbag Has No `metadata.yaml` After A Run

`ros2 bag record --disable-keyboard-controls` (used to avoid a SIGINT/raw-tty
hang under `run_pick_place.sh`) can bypass rosbag2's normal graceful shutdown
path, leaving an `.mcap` file with data but no `metadata.yaml` — `ros2 bag
info`/`play` then fail with "Could not find metadata in bag directory".  The
data itself is intact; regenerate the index with:

```bash
ros2 bag reindex <run_dir>/rosbag
```

`run_pick_place.sh`'s cleanup trap now does this automatically whenever
`ros2 bag info` fails right after recording stops, so new runs shouldn't need
the manual step.

## Known Non-Fatal Warnings

- KDL warns that the official Panda root link has inertia. The validated
  kinematics and trajectories are unaffected.
- MoveIt warns that no 3D sensor plugin is configured. This demo uses explicit
  table/cube collision objects and intentionally has no Octomap sensor.
- Gazebo's selected physics engine reports no native mimic constraint for the
  second finger. `gz_ros2_control` publishes the finger state and the validated
  workflow uses a position-controlled first finger plus DetachableJoint for
  deterministic grasp retention.
- SDFormat warns that `gz_frame_id` is an extension element. Gazebo Harmonic
  still applies it, and the validated image headers use
  `overhead_camera_optical_frame` as intended.
- The legacy `position_controllers/GripperActionController` reports a
  deprecation warning. It is available in Jazzy and the tested action succeeds;
  migration to the parallel-gripper controller can be done independently.
