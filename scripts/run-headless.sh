#!/usr/bin/env bash
# Runs a command inside a headless display server.
#   scripts/run-headless.sh x11|wayland|weston -- <command> [args...]
# x11: Xvfb with mutter as its window manager (as GNOME on Xorg; X11 is
# Mint Cinnamon's default session type); wayland: GNOME's compositor
# (mutter --headless); weston: a non-GNOME Wayland compositor. x11 and
# wayland run on a session bus of their own.
set -euo pipefail
backend=${1:?usage: run-headless.sh x11|wayland|weston -- cmd...}
shift
[[ "${1:-}" == "--" ]] && shift

export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-$(mktemp -d)}
chmod 700 "$XDG_RUNTIME_DIR"
export GTK_A11Y=${GTK_A11Y:-none}
export NO_AT_BRIDGE=1

case "$backend" in
  x11)
    exec dbus-run-session -- xvfb-run -a -s "-screen 0 1920x1080x24" bash -c '
      set -euo pipefail
      mutter --x11 --replace --sm-disable >"$XDG_RUNTIME_DIR/mutter.log" 2>&1 &
      mpid=$!
      # Up once it claims the screen (_NET_SUPPORTING_WM_CHECK).
      for _ in $(seq 100); do
        xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q "window id" && break
        sleep 0.1
      done
      xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q "window id" ||
        { cat "$XDG_RUNTIME_DIR/mutter.log"; exit 1; }
      set +e
      GDK_BACKEND=x11 "$@"
      status=$?
      kill $mpid 2>/dev/null; wait $mpid 2>/dev/null
      exit $status
    ' bash "$@"
    ;;
  wayland)
    export RNGTK_SCRIPTS=$(cd "$(dirname "$0")" && pwd)
    exec dbus-run-session -- bash -c '
      set -euo pipefail
      sock=wayland-rngtk-$$
      mutter --headless --wayland --no-x11 --wayland-display "$sock" \
        --virtual-monitor 1920x1080 >"$XDG_RUNTIME_DIR/mutter.log" 2>&1 &
      mpid=$!
      for _ in $(seq 100); do [[ -S "$XDG_RUNTIME_DIR/$sock" ]] && break; sleep 0.1; done
      [[ -S "$XDG_RUNTIME_DIR/$sock" ]] || { cat "$XDG_RUNTIME_DIR/mutter.log"; exit 1; }
      # A keyboard on the seat, or mutter never activates a window.
      coproc kbd { "${PYTHON:-python3}" "$RNGTK_SCRIPTS/mutter-keyboard.py"; }
      read -r -u "${kbd[0]}" ready || { echo "mutter-keyboard.py failed" >&2; exit 1; }
      set +e
      WAYLAND_DISPLAY=$sock GDK_BACKEND=wayland "$@"
      status=$?
      kill $kbd_PID $mpid 2>/dev/null; wait $mpid 2>/dev/null
      exit $status
    ' bash "$@"
    ;;
  weston)
    sock=wayland-rngtk-weston-$$
    weston --backend=headless --socket="$sock" --width=1920 --height=1080 \
      --idle-time=0 >"$XDG_RUNTIME_DIR/weston.log" 2>&1 &
    wpid=$!
    trap 'kill $wpid 2>/dev/null || true' EXIT
    for _ in $(seq 100); do [[ -S "$XDG_RUNTIME_DIR/$sock" ]] && break; sleep 0.1; done
    [[ -S "$XDG_RUNTIME_DIR/$sock" ]] || { cat "$XDG_RUNTIME_DIR/weston.log"; exit 1; }
    WAYLAND_DISPLAY=$sock GDK_BACKEND=wayland "$@"
    ;;
  *) echo "unknown backend: $backend" >&2; exit 2 ;;
esac
