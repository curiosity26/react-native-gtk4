#!/usr/bin/env python3
"""Gives a headless mutter a keyboard, so its windows get activated.

    scripts/mutter-keyboard.py   (on mutter's session bus; runs until killed)

`mutter --headless` starts with no input devices, and without a keyboard on
the seat it never activates a window: focus, focus-dependent widgets and the
frame rate of an inactive window all differ from a desktop session. A remote
desktop session (org.gnome.Mutter.RemoteDesktop) adds virtual devices; one
Shift press makes the keyboard exist. The session lasts while this runs.
scripts/run-headless.sh wayland starts it.
"""
import sys
import time

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

NAME = "org.gnome.Mutter.RemoteDesktop"
bus = Gio.bus_get_sync(Gio.BusType.SESSION)
for _ in range(100):
    try:
        reply = bus.call_sync(NAME, "/org/gnome/Mutter/RemoteDesktop", NAME, "CreateSession",
                              None, None, Gio.DBusCallFlags.NONE, -1, None)
        break
    except GLib.Error:
        time.sleep(0.1)
else:
    sys.exit("mutter-keyboard: no org.gnome.Mutter.RemoteDesktop on the session bus")
path = reply.unpack()[0]
session = NAME + ".Session"
bus.call_sync(NAME, path, session, "Start", None, None, Gio.DBusCallFlags.NONE, -1, None)
for pressed in (True, False):
    bus.call_sync(NAME, path, session, "NotifyKeyboardKeysym",
                  GLib.Variant("(ub)", (0xFFE1, pressed)),  # Shift_L
                  None, Gio.DBusCallFlags.NONE, -1, None)
print("ready", flush=True)
GLib.MainLoop().run()
