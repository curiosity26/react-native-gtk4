// Keyboard input on a surface's root widget, for React views:
//
// - Focus: which view has GTK's keyboard focus. A focusable View (or
//   Pressable, Touchable, Button) gets onFocus / onBlur when it gains or
//   loses it; Tab and Shift+Tab move it (RNView's tree-order focus), as do
//   clicks, ref.focus() / blur() and autoFocus (GtkMountingManager).
// - Keys: onKeyDown / onKeyUp on the focused view (TextInputs and native
//   controls too), bubbling through React, with W3C `key` and `code` and
//   the modifiers. Keys a view or an ancestor lists in keyDownEvents /
//   keyUpEvents are handled: GTK doesn't act on them (Tab won't move
//   focus, a TextInput won't type them).
// - Activation: Enter (on press) and Space (on release) press the focused
//   Pressable: a click event without pointerType, which Pressability turns
//   into onPress, as Android's keyboard clicks do.
//
// Key events reach the root only while the focus is inside it; with
// nothing focused there is no target, as on react-native-macos.
#pragma once

#include <gtk/gtk.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/core/ReactPrimitives.h>

#include <memory>
#include <set>
#include <string>

namespace rngtk {

class GtkMountingManager;

class GtkKeyboardHandler {
 public:
  GtkKeyboardHandler(GtkMountingManager &mountingManager, GtkWidget *root);
  ~GtkKeyboardHandler();
  GtkKeyboardHandler(const GtkKeyboardHandler &) = delete;
  GtkKeyboardHandler &operator=(const GtkKeyboardHandler &) = delete;

  // What GDK key events turn into; tests call them directly. keyPressed
  // returns true when the key was handled (GTK then skips it).
  bool keyPressed(guint keyval, guint keycode, GdkModifierType state);
  void keyReleased(guint keyval, guint keycode, GdkModifierType state);

 private:
  struct Target {
    facebook::react::Tag tag = 0;
    facebook::react::SharedEventEmitter emitter;
    GtkWidget *widget = nullptr;
  };
  // The React view at or above the focus widget, inside the root.
  Target focusTarget() const;
  bool isHandled(const Target &target, const std::string &key,
                 const std::string &code, GdkModifierType state,
                 bool down) const;
  void dispatchKey(const char *type, const Target &target,
                   const std::string &key, const std::string &code,
                   GdkModifierType state, bool down, bool repeat);
  bool canActivate(const Target &target, GdkModifierType state) const;
  void activate(const Target &target);
  void focusChanged();
  void connectWindow();

  static gboolean onKeyPressed(GtkEventControllerKey *, guint keyval,
                               guint keycode, GdkModifierType state,
                               gpointer self);
  static void onKeyReleased(GtkEventControllerKey *, guint keyval,
                            guint keycode, GdkModifierType state,
                            gpointer self);
  static void onFocusWidget(GObject *, GParamSpec *, gpointer self);
  static void onRealize(GtkWidget *, gpointer self);

  GtkMountingManager &mountingManager_;
  GtkWidget *root_;
  GtkEventController *controller_;
  GtkWindow *window_ = nullptr;
  gulong focusHandler_ = 0;
  // The focused React view (a View, not a TextInput, which reports its
  // own focus), referenced while focused.
  std::shared_ptr<GtkWidget> focusedView_;
  // Keys held down (hardware keycodes), for `repeat`.
  std::set<guint> held_;
  // A Space that pressed a Pressable, until its release.
  guint activationKeycode_ = 0;
};

}  // namespace rngtk
