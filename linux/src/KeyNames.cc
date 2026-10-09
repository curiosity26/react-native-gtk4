#include "KeyNames.h"

#include <unordered_map>

namespace rngtk {

std::string w3cKey(guint keyval) {
  static const std::unordered_map<guint, const char *> named = {
      {GDK_KEY_Return, "Enter"},
      {GDK_KEY_KP_Enter, "Enter"},
      {GDK_KEY_ISO_Enter, "Enter"},
      {GDK_KEY_Tab, "Tab"},
      {GDK_KEY_ISO_Left_Tab, "Tab"},
      {GDK_KEY_KP_Tab, "Tab"},
      {GDK_KEY_BackSpace, "Backspace"},
      {GDK_KEY_Escape, "Escape"},
      {GDK_KEY_Delete, "Delete"},
      {GDK_KEY_KP_Delete, "Delete"},
      {GDK_KEY_Insert, "Insert"},
      {GDK_KEY_KP_Insert, "Insert"},
      {GDK_KEY_Home, "Home"},
      {GDK_KEY_KP_Home, "Home"},
      {GDK_KEY_End, "End"},
      {GDK_KEY_KP_End, "End"},
      {GDK_KEY_Page_Up, "PageUp"},
      {GDK_KEY_KP_Page_Up, "PageUp"},
      {GDK_KEY_Page_Down, "PageDown"},
      {GDK_KEY_KP_Page_Down, "PageDown"},
      {GDK_KEY_Left, "ArrowLeft"},
      {GDK_KEY_KP_Left, "ArrowLeft"},
      {GDK_KEY_Right, "ArrowRight"},
      {GDK_KEY_KP_Right, "ArrowRight"},
      {GDK_KEY_Up, "ArrowUp"},
      {GDK_KEY_KP_Up, "ArrowUp"},
      {GDK_KEY_Down, "ArrowDown"},
      {GDK_KEY_KP_Down, "ArrowDown"},
      {GDK_KEY_KP_Begin, "Clear"},
      {GDK_KEY_Clear, "Clear"},
      {GDK_KEY_Shift_L, "Shift"},
      {GDK_KEY_Shift_R, "Shift"},
      {GDK_KEY_Control_L, "Control"},
      {GDK_KEY_Control_R, "Control"},
      {GDK_KEY_Alt_L, "Alt"},
      {GDK_KEY_Alt_R, "Alt"},
      {GDK_KEY_ISO_Level3_Shift, "AltGraph"},
      {GDK_KEY_Meta_L, "Meta"},
      {GDK_KEY_Meta_R, "Meta"},
      {GDK_KEY_Super_L, "Meta"},
      {GDK_KEY_Super_R, "Meta"},
      {GDK_KEY_Hyper_L, "Hyper"},
      {GDK_KEY_Hyper_R, "Hyper"},
      {GDK_KEY_Caps_Lock, "CapsLock"},
      {GDK_KEY_Num_Lock, "NumLock"},
      {GDK_KEY_Scroll_Lock, "ScrollLock"},
      {GDK_KEY_Print, "PrintScreen"},
      {GDK_KEY_Pause, "Pause"},
      {GDK_KEY_Menu, "ContextMenu"},
      {GDK_KEY_Help, "Help"},
      {GDK_KEY_Undo, "Undo"},
      {GDK_KEY_Redo, "Redo"},
      {GDK_KEY_Find, "Find"},
      {GDK_KEY_Cancel, "Cancel"},
      {GDK_KEY_AudioMute, "AudioVolumeMute"},
      {GDK_KEY_AudioLowerVolume, "AudioVolumeDown"},
      {GDK_KEY_AudioRaiseVolume, "AudioVolumeUp"},
      {GDK_KEY_AudioPlay, "MediaPlayPause"},
      {GDK_KEY_AudioStop, "MediaStop"},
      {GDK_KEY_AudioNext, "MediaTrackNext"},
      {GDK_KEY_AudioPrev, "MediaTrackPrevious"},
      {GDK_KEY_dead_grave, "Dead"},
      {GDK_KEY_dead_acute, "Dead"},
      {GDK_KEY_dead_circumflex, "Dead"},
      {GDK_KEY_dead_tilde, "Dead"},
      {GDK_KEY_dead_diaeresis, "Dead"},
      {GDK_KEY_Multi_key, "Compose"},
  };
  if (auto it = named.find(keyval); it != named.end()) return it->second;
  if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F35) {
    return "F" + std::to_string(keyval - GDK_KEY_F1 + 1);
  }
  gunichar c = gdk_keyval_to_unicode(keyval);
  if (c >= 0x20 && c != 0x7F) {
    char utf8[8];
    int n = g_unichar_to_utf8(c, utf8);
    return std::string(utf8, n);
  }
  return "Unidentified";
}

std::string w3cCode(guint keycode) {
  // Linux evdev codes (linux/input-event-codes.h) to UI Events codes, as
  // Chromium's keycode converter maps them.
  static const std::unordered_map<guint, const char *> codes = {
      {1, "Escape"},          {2, "Digit1"},         {3, "Digit2"},
      {4, "Digit3"},          {5, "Digit4"},         {6, "Digit5"},
      {7, "Digit6"},          {8, "Digit7"},         {9, "Digit8"},
      {10, "Digit9"},         {11, "Digit0"},        {12, "Minus"},
      {13, "Equal"},          {14, "Backspace"},     {15, "Tab"},
      {16, "KeyQ"},           {17, "KeyW"},          {18, "KeyE"},
      {19, "KeyR"},           {20, "KeyT"},          {21, "KeyY"},
      {22, "KeyU"},           {23, "KeyI"},          {24, "KeyO"},
      {25, "KeyP"},           {26, "BracketLeft"},   {27, "BracketRight"},
      {28, "Enter"},          {29, "ControlLeft"},   {30, "KeyA"},
      {31, "KeyS"},           {32, "KeyD"},          {33, "KeyF"},
      {34, "KeyG"},           {35, "KeyH"},          {36, "KeyJ"},
      {37, "KeyK"},           {38, "KeyL"},          {39, "Semicolon"},
      {40, "Quote"},          {41, "Backquote"},     {42, "ShiftLeft"},
      {43, "Backslash"},      {44, "KeyZ"},          {45, "KeyX"},
      {46, "KeyC"},           {47, "KeyV"},          {48, "KeyB"},
      {49, "KeyN"},           {50, "KeyM"},          {51, "Comma"},
      {52, "Period"},         {53, "Slash"},         {54, "ShiftRight"},
      {55, "NumpadMultiply"}, {56, "AltLeft"},       {57, "Space"},
      {58, "CapsLock"},       {59, "F1"},            {60, "F2"},
      {61, "F3"},             {62, "F4"},            {63, "F5"},
      {64, "F6"},             {65, "F7"},            {66, "F8"},
      {67, "F9"},             {68, "F10"},           {69, "NumLock"},
      {70, "ScrollLock"},     {71, "Numpad7"},       {72, "Numpad8"},
      {73, "Numpad9"},        {74, "NumpadSubtract"}, {75, "Numpad4"},
      {76, "Numpad5"},        {77, "Numpad6"},       {78, "NumpadAdd"},
      {79, "Numpad1"},        {80, "Numpad2"},       {81, "Numpad3"},
      {82, "Numpad0"},        {83, "NumpadDecimal"}, {86, "IntlBackslash"},
      {87, "F11"},            {88, "F12"},           {89, "IntlRo"},
      {96, "NumpadEnter"},    {97, "ControlRight"},  {98, "NumpadDivide"},
      {99, "PrintScreen"},    {100, "AltRight"},     {102, "Home"},
      {103, "ArrowUp"},       {104, "PageUp"},       {105, "ArrowLeft"},
      {106, "ArrowRight"},    {107, "End"},          {108, "ArrowDown"},
      {109, "PageDown"},      {110, "Insert"},       {111, "Delete"},
      {113, "AudioVolumeMute"}, {114, "AudioVolumeDown"},
      {115, "AudioVolumeUp"}, {117, "NumpadEqual"},  {119, "Pause"},
      {121, "NumpadComma"},   {124, "IntlYen"},      {125, "MetaLeft"},
      {126, "MetaRight"},     {127, "ContextMenu"},  {183, "F13"},
      {184, "F14"},           {185, "F15"},          {186, "F16"},
      {187, "F17"},           {188, "F18"},          {189, "F19"},
      {190, "F20"},           {191, "F21"},          {192, "F22"},
      {193, "F23"},           {194, "F24"},
  };
  if (keycode < 8) return "";
  auto it = codes.find(keycode - 8);
  return it == codes.end() ? "" : it->second;
}

}  // namespace rngtk
