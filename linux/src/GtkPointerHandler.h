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
//
// Desktop mouse props, as react-native-windows and react-native-macos spell
// them: onMouseEnter / onMouseLeave (the mouse only, not bubbling, like
// pointerenter/leave) and onAuxClick (a middle or right click: press and
// release on the same view; bubbles).
//
// Selectable text selects with the mouse like a GtkLabel: drag, double-
// click for words, triple-click for the paragraph, Shift+click to extend.
// Ctrl+C (or Ctrl+Insert) and the right-click Copy copy it. A selection
// that becomes non-empty cancels the press it started with.
#pragma once

#include <gtk/gtk.h>
#include <react/renderer/components/view/PointerEvent.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/core/ReactPrimitives.h>

#include <cstdint>
#include <memory>
#include <string>
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
  // The selected text (of selectable Text), or "" with no selection.
  std::string selectedText() const;
  // Copies the selection to the clipboard; false with no selection.
  bool copySelection();

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
  facebook::react::PointerEvent pointerEvent(const char *type,
                                             const Target &target,
                                             const Input &input) const;
  // The view's own onMouseEnter / onMouseLeave props.
  bool listensForMouse(const Target &target, const char *event) const;
  void dispatchMouse(const char *type, const Target &target, const Input &input);
  void showCopyMenu(const Target &target, double x, double y);
  // A press focuses the focusable view it lands in (as on the web).
  void focusOnPress(const Target &target);
  // Selection by mouse on selectable text.
  int clickCount(const Input &input);
  bool beginSelection(const Input &input, const Target &target, int clicks);
  void extendSelection(const Input &input);
  void clearSelection();
  static gboolean onCopyShortcut(GtkWidget *, GVariant *, gpointer self);

  GtkMountingManager &mountingManager_;
  GtkWidget *root_;
  GtkEventController *controller_;
  std::unordered_map<int, ActiveTouch> touches_;
  std::vector<Target> hovered_;  // root first
  Target pressTarget_;           // for click: where the button went down
  Target auxPressTarget_;        // for auxClick
  int auxButton_ = 0;
  int buttons_ = 0;              // W3C buttons bitmask
  bool realInput_ = true;
  // A right-click just opened a context menu (GTK then skips the press).
  bool contextMenuShown_ = false;
  GtkEventController *shortcuts_;

  // The paragraph with a selection (or being dragged over), what the
  // press selected first (a caret, word or paragraph: drags extend from
  // it), and how: 1 characters, 2 words, 3 paragraphs.
  std::shared_ptr<GtkWidget> selectionWidget_;
  int anchorStart_ = 0, anchorEnd_ = 0;
  int selectionUnit_ = 1;
  bool selecting_ = false;
  // Double and triple clicks.
  uint32_t lastClickMs_ = 0;
  double lastClickX_ = 0, lastClickY_ = 0;
  int clicks_ = 0;
};

}  // namespace rngtk
