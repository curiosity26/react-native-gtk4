// W3C KeyboardEvent names for GDK keys: `key` from the keyval (what the
// key produces with the current layout and modifiers) and `code` from the
// hardware keycode (the physical key, layout-independent), as browsers on
// Linux report them.
#pragma once

#include <gdk/gdk.h>

#include <string>

namespace rngtk {

// "a", "A", "Enter", "ArrowLeft", "Shift"... or "Unidentified".
std::string w3cKey(guint keyval);
// "KeyA", "Digit1", "Enter", "ShiftLeft"... or "" when unknown. GDK's
// keycodes on X11 and Wayland are XKB's: evdev's plus 8.
std::string w3cCode(guint keycode);

}  // namespace rngtk
