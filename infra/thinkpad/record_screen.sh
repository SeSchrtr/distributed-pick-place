#!/usr/bin/env bash
set -euo pipefail

# NOT CURRENTLY IMPLEMENTED. This was meant to wrap GNOME Shell's Screencast
# D-Bus API (org.gnome.Shell.Screencast), but that interface no longer exists
# on GNOME Shell 46 (confirmed live on this machine: gdbus reports
# UnknownMethod). The replacement is either:
#   - org.gnome.Mutter.ScreenCast: private/unstable, undocumented, meant only
#     for xdg-desktop-portal-gnome's own use; or
#   - org.freedesktop.portal.ScreenCast: the stable, documented API, but it
#     requires a one-time interactive "Share" consent dialog per session
#     (Wayland's security model deliberately disallows silent screen capture),
#     plus a real PipeWire consumer (e.g. gst-launch-1.0 pipewiresrc) to turn
#     the resulting stream into a file.
# Neither is a drop-in replacement for a single unattended gdbus call, so this
# has been left unimplemented rather than shipped as something that silently
# fails every run. See docs/TROUBLESHOOTING.md for the options and callers
# must keep working: `record_screen.sh start <output-file>` / `stop`.

start_recording() {
  local output_file="$1"
  echo "record_screen.sh: automatic screen recording is not implemented on this GNOME version (see script header); skipping ${output_file}." >&2
  return 1
}

stop_recording() {
  return 0
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
