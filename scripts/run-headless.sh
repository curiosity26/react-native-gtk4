#!/usr/bin/env bash
# Runs a command inside a headless display server.
#   scripts/run-headless.sh x11|xwayland|wayland|weston -- <command> [args...]
# x11: Xvfb with mutter as its window manager (as GNOME on Xorg; X11 is
# Mint Cinnamon's default session type); xwayland: X11 through Xwayland
# under GNOME's compositor, as X11 apps run on a GNOME desktop (mutter 49+
# has no X11 mode, so Ubuntu 26.04 tests X11 this way); wayland: GNOME's
# compositor (mutter --headless); weston: a non-GNOME Wayland compositor.
# x11, xwayland and wayland run on a session bus of their own. $MUTTER
# overrides the mutter command.
set -euo pipefail
backend=${1:?usage: run-headless.sh x11|xwayland|wayland|weston -- cmd...}
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
      ${MUTTER:-mutter} --x11 --replace --sm-disable >"$XDG_RUNTIME_DIR/mutter.log" 2>&1 &
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
  wayland|xwayland)
    export RNGTK_SCRIPTS=$(cd "$(dirname "$0")" && pwd) RNGTK_BACKEND=$backend
    exec dbus-run-session -- bash -c '
      set -euo pipefail
      sock=wayland-rngtk-$$
      x11=--no-x11
      if [[ $RNGTK_BACKEND == xwayland ]]; then
        x11=
        # A runtime dir of its own: only our Xwayland cookie in it.
        export XDG_RUNTIME_DIR=$(mktemp -d)
      fi
      ${MUTTER:-mutter} --headless --wayland $x11 --wayland-display "$sock" \
        --virtual-monitor 1920x1080 >"$XDG_RUNTIME_DIR/mutter.log" 2>&1 &
      mpid=$!
      for _ in $(seq 100); do [[ -S "$XDG_RUNTIME_DIR/$sock" ]] && break; sleep 0.1; done
      [[ -S "$XDG_RUNTIME_DIR/$sock" ]] || { cat "$XDG_RUNTIME_DIR/mutter.log"; exit 1; }
      # A keyboard on the seat, or mutter never activates a window.
      coproc kbd { "${PYTHON:-python3}" "$RNGTK_SCRIPTS/mutter-keyboard.py"; }
      read -r -u "${kbd[0]}" ready || { echo "mutter-keyboard.py failed" >&2; exit 1; }
      set +e
      if [[ $RNGTK_BACKEND == xwayland ]]; then
        # mutter starts Xwayland when a client connects; it logs the display
        # and keeps the cookie in $XDG_RUNTIME_DIR.
        display=$(sed -n "s/.*Using public X11 display \(:[0-9]*\).*/\1/p" "$XDG_RUNTIME_DIR/mutter.log" | head -1)
        auth=$(ls "$XDG_RUNTIME_DIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -1)
        [[ -n $display && -n $auth ]] || { echo "no Xwayland display" >&2; cat "$XDG_RUNTIME_DIR/mutter.log"; exit 1; }
        WAYLAND_DISPLAY=$sock DISPLAY=$display XAUTHORITY=$auth GDK_BACKEND=x11 "$@"
      else
        WAYLAND_DISPLAY=$sock GDK_BACKEND=wayland "$@"
      fi
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
