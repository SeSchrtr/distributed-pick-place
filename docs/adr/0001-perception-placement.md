# ADR-0001: Run RGB-D Perception On The Jetson, Not The ThinkPad

## Status

Accepted. Simulation-validated; real-camera hardware validation pending (see
"Consequences").

## Context

The distributed demo splits Gazebo (ThinkPad) from MoveIt/pick-and-place
(Jetson). Perception (`panda_perception`, an RGB-D cube-pose estimator) was
originally placed on the ThinkPad, next to the simulated camera, so that only
the small `/perception/cube_pose` message crossed the WiFi link to the Jetson.

The target deployment for this project is a real Panda with a real overhead
camera wired directly into the Jetson, and the ThinkPad replaced or reduced to
a dev workstation. In that topology, the camera is local to the Jetson, not to
whatever machine happens to run Gazebo today.

## Decision

Run `panda_perception` on the Jetson, co-located with `panda_pick_place` and
`move_group`, mirroring the target real-robot topology now rather than moving
it later. In the simulation, the camera stays on the ThinkPad (Gazebo needs a
GUI-capable host), so its raw RGB and depth streams are compressed with
`image_transport` (`compressed` / `compressedDepth`) before crossing WiFi to
the Jetson, then decompressed there before reaching the unmodified estimator.

## Alternatives Considered

- **Keep perception on the ThinkPad (status quo).** Simpler for the
  simulation and avoids the compression hop, but does not reflect the real
  deployment target: a physical build would then require re-plumbing
  perception, its launch files, and its Docker packaging onto the Jetson
  later, and the architecture diagram would misrepresent where the real
  system's bottleneck (camera bandwidth vs. edge compute) actually lives.
- **Run perception on both hosts, choose via launch argument.** Rejected as
  unnecessary duplication for a demo repository; the simulation-only code path
  would rot unexercised. A single, real-topology-first placement is simpler to
  reason about and to review.

## Consequences

- `/perception/cube_pose` no longer crosses the network; it is produced and
  consumed on the Jetson. This removes a cross-host hop from the critical
  path of every planning cycle.
- The camera feed now crosses the network instead, in compressed form
  (`/overhead_camera/image/compressed`, `/overhead_camera/depth_image/compressedDepth`).
  This hop, and the compression it requires, is purely a simulation artifact:
  a real camera plugged into the Jetson removes it entirely.
- The Jetson Docker image now also builds `panda_perception` and needs
  `cv_bridge` and the `image_transport` compressed/compressedDepth plugins.
- RViz's debug-only depth colormap and cube marker now also cross from Jetson
  to ThinkPad; this is accepted as non-blocking, low-priority debug traffic.
- This change has been validated by rebuilding all affected ROS 2 packages and
  by a Docker build of the updated Jetson image. A full hardware-in-the-loop
  rerun (`launch_simulation.sh` -> `launch_planner.sh` -> `run_pick_place.sh`,
  as previously captured in
  [`docs/SETUP_REPORT.md`](../SETUP_REPORT.md)) is a recommended follow-up
  before relying on this as a regression-free change.
