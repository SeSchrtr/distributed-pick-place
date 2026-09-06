#!/usr/bin/env bash
set -euo pipefail

# Wraps GNOME Shell's built-in Screencast D-Bus API, which is present on an
# unmodified Ubuntu 24.04 GNOME/Wayland session (the ThinkPad default) with no
# extra packages. This has not been exercised against a live GNOME session;
# verify it once on the real ThinkPad before relying on it.
#
# If the ThinkPad runs a different desktop (Sway/wlroots, KDE, or an X11
# session), replace start_recording/stop_recording with `wf-recorder` or
# `ffmpeg -f x11grab` equivalents; callers only depend on this script's
# `start <output-file>` / `stop` interface, not on how it is implemented.
#
# Caveat: GNOME's Screencast API historically writes into $XDG_VIDEOS_DIR
# and may echo back a different actual filename; this script reports what
# GNOME returned so the caller can relocate the file if needed.

start_recording() {
  local output_file="$1"
  gdbus call --session \
    --dest org.gnome.Shell \
    --object-path /org/gnome/Shell/Screencast \
    --method org.gnome.Shell.Screencast.Screencast \
    "${output_file}" "{'framerate': <30>, 'draw-cursor': <true>}"
}

stop_recording() {
  gdbus call --session \
    --dest org.gnome.Shell \
    --object-path /org/gnome/Shell/Screencast \
    --method org.gnome.Shell.Screencast.StopScreencast
}

case "${1:-}" in
  start)
    [[ -n "${2:-}" ]] || { echo "Usage: $0 start <output-file>" >&2; exit 1; }
    start_recording "$2"
    ;;
  stop)
    stop_recording
    ;;
  *)
    echo "Usage: $0 {start <output-file>|stop}" >&2
    exit 1
    ;;
esac
