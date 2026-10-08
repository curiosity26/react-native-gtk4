// Feeds mouse and touchscreen input on a surface's root widget into
// Fabric: React Native touch events (touchStart/Move/End/Cancel, which drive
// the responder system behind Pressable and the Touchables) and W3C pointer
// events (pointerdown/move/up, click, and hover: pointerover/out and
// pointerenter/leave, behind onHoverIn/onHoverOut).
//
// Like iOS's RCTSurfaceTouchHandler: a press targets the deepest view under
// it (rn_widget_pick: transforms, overflow, pointerEvents), or the nested
// <Text> span under it; moves and the release go to that same target.
// Pointer events target whatever is under the pointer now.
#pragma once

#include <gtk/gtk.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/core/ReactPrimitives.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace rngtk {

class GtkMountingManager;

class GtkPointerHandler {
 public:
  // Scroll: a wheel turn of (dx, dy) notches over the scroll view under the
  // pointer.
  enum class Phase { Down, Move, Up, Cancel, Leave, Scroll };
  enum class Device { Mouse, Touch };

  struct Input {
    Phase phase;
    Device device = Device::Mouse;
    double x = 0, y = 0;   // in root widget coordinates
    int sequence = 0;      // touch point id; 0 for the mouse
    int button = 1;        // 1 primary, 2 middle, 3 secondary
    GdkModifierType modifiers = GdkModifierType(0);
    uint32_t timeMs = 0;   // event time, for velocity in JS
    double dx = 0, dy = 0; // Scroll
  };

  // Listens to input on `root` (an RNView registered as a surface root).
  GtkPointerHandler(GtkMountingManager &mountingManager, GtkWidget *root);
  ~GtkPointerHandler();
  GtkPointerHandler(const GtkPointerHandler &) = delete;
  GtkPointerHandler &operator=(const GtkPointerHandler &) = delete;

  // What GDK events turn into; tests call it directly.
  void dispatch(const Input &input);
  // Self-tests drive dispatch() alone: the desktop's real pointer may sit
  // over the window and would move the hover state under them.
  void setRealInputEnabled(bool enabled) { realInput_ = enabled; }
  // Ends every touch in progress with touchCancel (a scroll view took over
  // the gesture), so presses under it don't fire.
  void cancelTouches();

 private:
  struct Target {
    facebook::react::Tag tag = 0;
    facebook::react::SharedEventEmitter emitter;
    // The widget hit (the paragraph, for a span); referenced, since a view
    // can unmount while it's pressed or hovered.
    std::shared_ptr<GtkWidget> widget;
    bool operator==(const Target &other) const { return tag == other.tag; }
  };
  struct ActiveTouch {
    Target target;
    double x, y;
    uint32_t timeMs;
  };

  static gboolean onEvent(GtkEventControllerLegacy *, GdkEvent *event,
                          gpointer self);
  bool handleEvent(GdkEvent *event);
  Target targetAt(double x, double y, double *localX, double *localY) const;
  // The target and its mounted ancestors, root first.
  std::vector<Target> pathTo(const Target &target) const;
  bool hasListener(GtkWidget *widget, size_t offset) const;

  void dispatchTouch(const char *type, int sequence, const Input &input);
  void dispatchPointer(const char *type, const Target &target,
                       const Input &input);
  void updateHover(const Input &input, const Target &target);
  void showCopyMenu(const Target &target, double x, double y);

  GtkMountingManager &mountingManager_;
  GtkWidget *root_;
  GtkEventController *controller_;
  std::unordered_map<int, ActiveTouch> touches_;
  std::vector<Target> hovered_;  // root first
  Target pressTarget_;           // for click: where the button went down
  int buttons_ = 0;              // W3C buttons bitmask
  bool realInput_ = true;
};

}  // namespace rngtk
