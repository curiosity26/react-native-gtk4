#include "GtkPointerHandler.h"

#include "GtkMountingManager.h"
#include "rn_scroll_view.h"
#include "rn_text.h"
#include "rn_text_input.h"
#include "rn_view.h"

#include <react/renderer/components/view/PointerEvent.h>
#include <react/renderer/components/view/TouchEvent.h>
#include <react/renderer/components/view/TouchEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/components/view/primitives.h>

#include <algorithm>
#include <utility>
#include <cmath>
#include <cstring>

using namespace facebook::react;

namespace rngtk {

namespace {

using Offset = ViewEvents::Offset;

constexpr size_t bit(Offset offset) { return static_cast<size_t>(offset); }

// W3C: the mouse is pointer 1; touch points follow.
constexpr int kMousePointerId = 1;

int buttonBit(int gdkButton) {
  switch (gdkButton) {
    case 1: return 1;   // primary
    case 2: return 4;   // auxiliary (middle)
    case 3: return 2;   // secondary
    default: return 0;
  }
}

// W3C `button`: 0 primary, 1 auxiliary, 2 secondary.
int w3cButton(int gdkButton) {
  return gdkButton == 2 ? 1 : gdkButton == 3 ? 2 : 0;
}

std::shared_ptr<GtkWidget> ref(GtkWidget *widget) {
  if (!widget) return nullptr;
  g_object_ref(widget);
  return std::shared_ptr<GtkWidget>(
      widget, [](GtkWidget *w) { g_object_unref(w); });
}

std::shared_ptr<const TouchEventEmitter> touchEmitter(
    const SharedEventEmitter &emitter) {
  return std::dynamic_pointer_cast<const TouchEventEmitter>(emitter);
}

}  // namespace

GtkPointerHandler::GtkPointerHandler(GtkMountingManager &mountingManager,
                                     GtkWidget *root)
    : mountingManager_(mountingManager), root_(root) {
  controller_ = gtk_event_controller_legacy_new();
  // Capture: see input before any widget inside the surface handles it.
  gtk_event_controller_set_propagation_phase(controller_, GTK_PHASE_CAPTURE);
  g_signal_connect(controller_, "event", G_CALLBACK(onEvent), this);
  gtk_widget_add_controller(root_, controller_);

  // Ctrl+C copies selected text from anywhere in the window; a focused
  // TextInput handles its own first (global shortcuts run after it).
  shortcuts_ = gtk_shortcut_controller_new();
  gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(shortcuts_),
                                    GTK_SHORTCUT_SCOPE_GLOBAL);
  for (const char *accel : {"<Control>c", "<Control>Insert"}) {
    gtk_shortcut_controller_add_shortcut(
        GTK_SHORTCUT_CONTROLLER(shortcuts_),
        gtk_shortcut_new(gtk_shortcut_trigger_parse_string(accel),
                         gtk_callback_action_new(onCopyShortcut, this, nullptr)));
  }
  gtk_widget_add_controller(root_, shortcuts_);
  setUpDragAndDrop();
}

GtkPointerHandler::~GtkPointerHandler() {
  tearDownDragAndDrop();
  g_signal_handlers_disconnect_by_data(controller_, this);
  gtk_widget_remove_controller(root_, controller_);
  gtk_widget_remove_controller(root_, shortcuts_);
}

gboolean GtkPointerHandler::onEvent(GtkEventControllerLegacy *, GdkEvent *event,
                                    gpointer self) {
  return static_cast<GtkPointerHandler *>(self)->handleEvent(event);
}

bool GtkPointerHandler::handleEvent(GdkEvent *event) {
  if (!realInput_) return false;
  GdkEventType type = gdk_event_get_event_type(event);
  Input input{};
  switch (type) {
    case GDK_BUTTON_PRESS: input.phase = Phase::Down; break;
    case GDK_BUTTON_RELEASE: input.phase = Phase::Up; break;
    case GDK_MOTION_NOTIFY: input.phase = Phase::Move; break;
    case GDK_TOUCH_BEGIN: input.phase = Phase::Down; break;
    case GDK_TOUCH_UPDATE: input.phase = Phase::Move; break;
    case GDK_TOUCH_END: input.phase = Phase::Up; break;
    case GDK_TOUCH_CANCEL: input.phase = Phase::Cancel; break;
    case GDK_LEAVE_NOTIFY: input.phase = Phase::Leave; break;
    default: return false;
  }
  bool touch = type == GDK_TOUCH_BEGIN || type == GDK_TOUCH_UPDATE ||
               type == GDK_TOUCH_END || type == GDK_TOUCH_CANCEL;
  if (!touch) {
    // Touchscreens send touch events; skip the pointer events GDK emulates
    // for them.
    GdkDevice *device = gdk_event_get_device(event);
    if (device && gdk_device_get_source(device) == GDK_SOURCE_TOUCHSCREEN) {
      return false;
    }
  }
  input.device = touch ? Device::Touch : Device::Mouse;
  if (touch) {
    // GDK sequences are opaque pointers; give each a small id.
    static std::unordered_map<GdkEventSequence *, int> ids;
    static int next = 0;
    GdkEventSequence *seq = gdk_event_get_event_sequence(event);
    auto it = ids.find(seq);
    if (it == ids.end()) it = ids.emplace(seq, next++ % 1000).first;
    input.sequence = it->second;
    if (input.phase == Phase::Up || input.phase == Phase::Cancel) {
      ids.erase(it);
    }
  }
  if (type == GDK_BUTTON_PRESS || type == GDK_BUTTON_RELEASE) {
    input.button = int(gdk_button_event_get_button(event));
  }
  input.modifiers = gdk_event_get_modifier_state(event);
  input.timeMs = gdk_event_get_time(event);

  double sx = 0, sy = 0;
  gdk_event_get_position(event, &sx, &sy);
  GtkNative *native = gtk_widget_get_native(root_);
  double nx = 0, ny = 0;
  gtk_native_get_surface_transform(native, &nx, &ny);
  graphene_point_t in_native{float(sx - nx), float(sy - ny)};
  graphene_point_t in_root;
  if (!gtk_widget_compute_point(GTK_WIDGET(native), root_, &in_native,
                                &in_root)) {
    return false;
  }
  input.x = in_root.x;
  input.y = in_root.y;
  contextMenuShown_ = false;
  dispatch(input);
  // Let GTK carry on (cursors, the dev menu button...), except after our
  // context menu opened: a TextInput's own mustn't open too.
  return std::exchange(contextMenuShown_, false);
}

GtkPointerHandler::Target GtkPointerHandler::targetAt(double x, double y,
                                                      double *localX,
                                                      double *localY) const {
  double lx = x, ly = y;
  GtkWidget *hit = rn_widget_pick(root_, x, y, &lx, &ly);
  if (!hit) {
    hit = root_;
    lx = x;
    ly = y;
  }
  int textIndex = -1;
  if (RN_IS_TEXT(hit)) {
    int index = 0, trailing = 0;
    if (pango_layout_xy_to_index(rn_text_get_layout(RN_TEXT(hit)),
                                 int(lx * PANGO_SCALE), int(ly * PANGO_SCALE),
                                 &index, &trailing)) {
      textIndex = index;
    }
  }
  auto target = mountingManager_.targetForView(hit, textIndex);
  if (localX) *localX = lx;
  if (localY) *localY = ly;
  return Target{target.tag, target.emitter, ref(hit)};
}

std::vector<GtkPointerHandler::Target> GtkPointerHandler::pathTo(
    const Target &target) const {
  std::vector<Target> path;
  if (!target.widget) return path;
  // A nested text span sits below its paragraph.
  auto widgetTarget = mountingManager_.targetForView(target.widget.get());
  if (widgetTarget.tag != target.tag) path.push_back(target);
  for (GtkWidget *w = target.widget.get(); w; w = gtk_widget_get_parent(w)) {
    auto t = mountingManager_.targetForView(w);
    if (t.tag != 0) path.push_back(Target{t.tag, t.emitter, ref(w)});
    if (w == root_) break;
  }
  std::reverse(path.begin(), path.end());
  return path;
}

bool GtkPointerHandler::hasListener(GtkWidget *widget, size_t offset) const {
  auto t = mountingManager_.targetForView(widget);
  return t.tag != 0 && mountingManager_.hasEventListener(t.tag, offset);
}

void GtkPointerHandler::dispatch(const Input &input) {
  double lx = 0, ly = 0;
  Target target = input.phase == Phase::Leave
                      ? Target{}
                      : targetAt(input.x, input.y, &lx, &ly);
  int id = input.device == Device::Mouse ? 0 : input.sequence;
  bool primary = input.device == Device::Touch || input.button == 1;

  // A GtkSwitch handles its own clicks and drags, like a UISwitch: no React
  // touches or pointer presses that could start a parent's press.
  if (target.widget && GTK_IS_SWITCH(target.widget.get()) &&
      (input.phase == Phase::Down || input.phase == Phase::Up)) {
    if (input.device == Device::Mouse) updateHover(input, target);
    return;
  }

  // Hover (pointerover/out, pointerenter/leave) follows the mouse.
  if (input.device == Device::Mouse && input.phase != Phase::Scroll) {
    updateHover(input, target);
  }

  switch (input.phase) {
    case Phase::Down: {
      buttons_ |= input.device == Device::Mouse ? buttonBit(input.button) : 1;
      pressTarget_ = target;
      if (primary) focusOnPress(target);
      if (input.device == Device::Mouse && input.button != 1) {
        auxPressTarget_ = target;
        auxButton_ = input.button;
      }
      dispatchPointer("pointerDown", target, input);
      if (primary) {
        touches_[id] = ActiveTouch{target, input.x, input.y, input.timeMs};
        dispatchTouch("touchStart", id, input);
      } else if (input.button == 3 && target.widget &&
                 !(contextMenuShown_ = mountingManager_.showContextMenu(
                       root_, target.widget.get(), input.x, input.y)) &&
                 mountingManager_.isSelectableText(
                     mountingManager_.targetForView(target.widget.get()).tag)) {
        showCopyMenu(target, input.x, input.y);
      }
      break;
    }
    case Phase::Move: {
      if (touches_.count(id)) {
        auto &t = touches_[id];
        t.x = input.x;
        t.y = input.y;
        t.timeMs = input.timeMs;
        dispatchTouch("touchMove", id, input);
      }
      dispatchPointer("pointerMove", target, input);
      break;
    }
    case Phase::Up: {
      buttons_ &= ~(input.device == Device::Mouse ? buttonBit(input.button) : 1);
      dispatchPointer("pointerUp", target, input);
      if (touches_.count(id)) {
        auto &t = touches_[id];
        t.x = input.x;
        t.y = input.y;
        t.timeMs = input.timeMs;
        dispatchTouch("touchEnd", id, input);
        touches_.erase(id);
      }
      if (primary && target == pressTarget_) {
        dispatchPointer("click", target, input);
      }
      if (!primary && input.button == auxButton_ && target == auxPressTarget_) {
        // auxclick bubbles through React, past ancestors Fabric flattened
        // (and so can't be asked here), so it always goes; it's rare.
        dispatchMouse("auxClick", target, input);
      }
      if (!primary) {
        auxPressTarget_ = Target{};
        auxButton_ = 0;
      }
      pressTarget_ = Target{};
      break;
    }
    case Phase::Cancel: {
      if (touches_.count(id)) {
        dispatchTouch("touchCancel", id, input);
        touches_.erase(id);
      }
      dispatchPointer("pointerCancel", target, input);
      break;
    }
    case Phase::Leave:
      break;
    case Phase::Scroll: {
      // The innermost scroll view under the pointer. Real wheel and
      // touchpad events go to its GtkScrolledWindow directly (kinetic,
      // smooth); this is the same scroll, by GTK's wheel step.
      for (GtkWidget *w = target.widget.get(); w; w = gtk_widget_get_parent(w)) {
        if (RN_IS_SCROLL_VIEW(w)) {
          rn_scroll_view_scroll_by_wheel(RN_SCROLL_VIEW(w), input.dx, input.dy);
          break;
        }
        if (w == root_) break;
      }
      break;
    }
  }

  // After the touches: a selection that turns non-empty cancels them.
  if (input.device == Device::Mouse && input.button == 1) {
    if (input.phase == Phase::Down) {
      if (!beginSelection(input, target, clickCount(input))) clearSelection();
    } else if (input.phase == Phase::Move && selecting_) {
      extendSelection(input);
    } else if (input.phase == Phase::Up && pressInSelection_) {
      // A click in the selection (no drag): the caret goes there.
      pressInSelection_ = false;
      if (selectionWidget_) {
        rn_text_set_selection(RN_TEXT(selectionWidget_.get()), pressIndex_, pressIndex_);
      }
    } else if (input.phase == Phase::Up && selecting_) {
      selecting_ = false;
      // X11 and Wayland's primary selection: middle-click pastes it.
      std::string text = selectedText();
      if (!text.empty()) {
        gdk_clipboard_set_text(
            gdk_display_get_primary_clipboard(gtk_widget_get_display(root_)),
            text.c_str());
      }
    }
  }
}

void GtkPointerHandler::cancelTouches() {
  if (touches_.empty()) return;
  Input input{};
  input.phase = Phase::Cancel;
  std::vector<int> ids;
  for (const auto &[id, t] : touches_) ids.push_back(id);
  for (int id : ids) {
    const auto &t = touches_.at(id);
    input.x = t.x;
    input.y = t.y;
    input.timeMs = t.timeMs;
    dispatchTouch("touchCancel", id, input);
    touches_.erase(id);
  }
  pressTarget_ = Target{};
}

void GtkPointerHandler::dispatchTouch(const char *type, int id,
                                      const Input &input) {
  auto toTouch = [this](int touchId, const ActiveTouch &t) {
    Touch touch{};
    touch.pagePoint = {.x = Float(t.x), .y = Float(t.y)};
    touch.screenPoint = touch.pagePoint;
    // Relative to the original target, like iOS.
    graphene_point_t p{float(t.x), float(t.y)}, local;
    if (t.target.widget &&
        gtk_widget_compute_point(root_, t.target.widget.get(), &p, &local)) {
      touch.offsetPoint = {.x = local.x, .y = local.y};
    }
    touch.identifier = touchId;
    touch.target = t.target.tag;
    touch.force = 0;
    touch.timestamp = Float(t.timeMs);
    touch.timeStamp = HighResTimeStamp::now();
    return touch;
  };

  const ActiveTouch &changed = touches_.at(id);
  TouchEvent event;
  Touch changedTouch = toTouch(id, changed);
  event.changedTouches.insert(changedTouch);
  bool ended = std::string(type) == "touchEnd" ||
               std::string(type) == "touchCancel";
  for (const auto &[touchId, t] : touches_) {
    if (ended && touchId == id) continue;
    Touch touch = toTouch(touchId, t);
    event.touches.insert(touch);
    if (t.target.tag == changed.target.tag) event.targetTouches.insert(touch);
  }
  auto emitter = touchEmitter(changed.target.emitter);
  if (!emitter) return;
  std::string name = type;
  if (name == "touchStart") emitter->onTouchStart(std::move(event));
  else if (name == "touchMove") emitter->onTouchMove(std::move(event));
  else if (name == "touchEnd") emitter->onTouchEnd(std::move(event));
  else emitter->onTouchCancel(std::move(event));
  (void)input;
}

void GtkPointerHandler::dispatchPointer(const char *type, const Target &target,
                                        const Input &input) {
  auto emitter = touchEmitter(target.emitter);
  if (!emitter) return;
  std::string name = type;

  // Only send what some view asks for: every mouse move would otherwise
  // wake JS. Bubbling events count listeners on ancestors too.
  auto wanted = [&](Offset own, Offset capture) {
    for (const auto &t : pathTo(target)) {
      if (mountingManager_.hasEventListener(t.tag, bit(own)) ||
          mountingManager_.hasEventListener(t.tag, bit(capture))) {
        return true;
      }
    }
    return false;
  };
  if (name == "pointerMove" &&
      !wanted(Offset::PointerMove, Offset::PointerMoveCapture)) {
    return;
  }
  if (name == "pointerDown" &&
      !wanted(Offset::PointerDown, Offset::PointerDownCapture)) {
    return;
  }
  if (name == "pointerUp" &&
      !wanted(Offset::PointerUp, Offset::PointerUpCapture)) {
    return;
  }
  if (name == "click" && !wanted(Offset::Click, Offset::ClickCapture)) return;

  PointerEvent event = pointerEvent(type, target, input);

  if (name == "pointerDown") emitter->onPointerDown(std::move(event));
  else if (name == "pointerMove") emitter->onPointerMove(std::move(event));
  else if (name == "pointerUp") emitter->onPointerUp(std::move(event));
  else if (name == "pointerCancel") emitter->onPointerCancel(std::move(event));
  else if (name == "click") emitter->onClick(std::move(event));
  else if (name == "pointerOver") emitter->onPointerOver(std::move(event));
  else if (name == "pointerOut") emitter->onPointerOut(std::move(event));
  else if (name == "pointerEnter") emitter->onPointerEnter(std::move(event));
  else if (name == "pointerLeave") emitter->onPointerLeave(std::move(event));
}

PointerEvent GtkPointerHandler::pointerEvent(const char *type,
                                             const Target &target,
                                             const Input &input) const {
  PointerEvent event{};
  bool mouse = input.device == Device::Mouse;
  event.pointerId = mouse ? kMousePointerId : 2 + input.sequence;
  event.pressure = buttons_ ? 0.5f : 0.0f;
  event.pointerType = mouse ? "mouse" : "touch";
  event.clientPoint = {.x = Float(input.x), .y = Float(input.y)};
  event.screenPoint = event.clientPoint;
  graphene_point_t p{float(input.x), float(input.y)}, local;
  if (target.widget &&
      gtk_widget_compute_point(root_, target.widget.get(), &p, &local)) {
    event.offsetPoint = {.x = local.x, .y = local.y};
  }
  event.width = event.height = 1;
  event.buttons = buttons_;
  event.button = std::string(type) == "pointerMove" ? -1 : w3cButton(input.button);
  event.ctrlKey = input.modifiers & GDK_CONTROL_MASK;
  event.shiftKey = input.modifiers & GDK_SHIFT_MASK;
  event.altKey = input.modifiers & GDK_ALT_MASK;
  event.metaKey = input.modifiers & (GDK_META_MASK | GDK_SUPER_MASK);
  event.isPrimary = true;
  event.timeStamp = HighResTimeStamp::now();

  return event;
}

bool GtkPointerHandler::listensForMouse(const Target &target,
                                        const char *event) const {
  auto props = std::dynamic_pointer_cast<const ViewProps>(
      mountingManager_.propsForTag(target.tag));
  if (!props) return false;
  std::string name = event;
  if (name == "mouseEnter") return props->onMouseEnter;
  return props->onMouseLeave;
}

void GtkPointerHandler::dispatchMouse(const char *type, const Target &target,
                                      const Input &input) {
  if (!target.emitter) return;
  target.emitter->dispatchEvent(
      type, std::make_shared<PointerEvent>(pointerEvent(type, target, input)),
      RawEvent::Category::Discrete);
}

void GtkPointerHandler::updateHover(const Input &input, const Target &target) {
  std::vector<Target> path = pathTo(target);
  if (path == hovered_) return;

  // Views whose listeners count: their own enter/leave, or a capture
  // listener on an ancestor.
  auto notify = [&](const std::vector<Target> &chain, size_t index,
                    const char *type, Offset own, Offset capture) {
    const Target &t = chain[index];
    bool listen = t.widget && hasListener(t.widget.get(), bit(own));
    for (size_t i = 0; !listen && i <= index; i++) {
      listen = chain[i].widget &&
               hasListener(chain[i].widget.get(), bit(capture));
    }
    if (listen) dispatchPointer(type, t, input);
  };
  auto bubbles = [&](const std::vector<Target> &chain, Offset own,
                     Offset capture) {
    for (const auto &t : chain) {
      if (t.widget && (hasListener(t.widget.get(), bit(own)) ||
                       hasListener(t.widget.get(), bit(capture)))) {
        return true;
      }
    }
    return false;
  };

  if (!hovered_.empty() &&
      bubbles(hovered_, Offset::PointerOut, Offset::PointerOutCapture)) {
    dispatchPointer("pointerOut", hovered_.back(), input);
  }
  // Leave the views no longer under the pointer, deepest first.
  for (size_t i = hovered_.size(); i-- > 0;) {
    if (std::find(path.begin(), path.end(), hovered_[i]) == path.end()) {
      notify(hovered_, i, "pointerLeave", Offset::PointerLeave,
             Offset::PointerLeaveCapture);
      if (listensForMouse(hovered_[i], "mouseLeave")) {
        dispatchMouse("mouseLeave", hovered_[i], input);
      }
    }
  }
  if (!path.empty() &&
      bubbles(path, Offset::PointerOver, Offset::PointerOverCapture)) {
    dispatchPointer("pointerOver", path.back(), input);
  }
  // Enter the new ones, outermost first.
  for (size_t i = 0; i < path.size(); i++) {
    if (std::find(hovered_.begin(), hovered_.end(), path[i]) ==
        hovered_.end()) {
      notify(path, i, "pointerEnter", Offset::PointerEnter,
             Offset::PointerEnterCapture);
      if (listensForMouse(path[i], "mouseEnter")) {
        dispatchMouse("mouseEnter", path[i], input);
      }
    }
  }
  hovered_ = std::move(path);
}

void GtkPointerHandler::focusOnPress(const Target &target) {
  for (GtkWidget *w = target.widget.get(); w && w != root_;
       w = gtk_widget_get_parent(w)) {
    // Native controls focus themselves on their own clicks.
    if (RN_IS_TEXT_INPUT(w) || GTK_IS_SWITCH(w) || GTK_IS_EDITABLE(w)) return;
    if (RN_IS_VIEW(w) && gtk_widget_get_focusable(w)) {
      if (!gtk_widget_has_focus(w)) gtk_widget_grab_focus(w);
      return;
    }
  }
}

int GtkPointerHandler::clickCount(const Input &input) {
  // GTK's double-click time and distance (a third click within them is a
  // triple click).
  int time = 400, distance = 5;
  g_object_get(gtk_widget_get_settings(root_), "gtk-double-click-time", &time,
               "gtk-double-click-distance", &distance, nullptr);
  bool again = clicks_ > 0 && input.timeMs - lastClickMs_ <= uint32_t(time) &&
               std::abs(input.x - lastClickX_) <= distance &&
               std::abs(input.y - lastClickY_) <= distance;
  clicks_ = again ? clicks_ % 3 + 1 : 1;
  lastClickMs_ = input.timeMs;
  lastClickX_ = input.x;
  lastClickY_ = input.y;
  return clicks_;
}

// A primary press on selectable text: a caret (nothing selected), the
// word, or the paragraph, which a drag then extends.
bool GtkPointerHandler::beginSelection(const Input &input, const Target &target,
                                       int clicks) {
  GtkWidget *widget = target.widget.get();
  if (!widget || !RN_IS_TEXT(widget) ||
      !mountingManager_.isSelectableText(
          mountingManager_.targetForView(widget).tag)) {
    return false;
  }
  auto *text = RN_TEXT(widget);
  graphene_point_t p{float(input.x), float(input.y)}, local;
  if (!gtk_widget_compute_point(root_, widget, &p, &local)) return false;
  bool extend = clicks == 1 && (input.modifiers & GDK_SHIFT_MASK) &&
                selectionWidget_.get() == widget;
  if (selectionWidget_ && selectionWidget_.get() != widget) clearSelection();
  selectionWidget_ = target.widget;
  selectionUnit_ = clicks;
  selecting_ = true;
  if (extend) {
    // Shift+click: from the selection's start (or the caret) to here.
    extendSelection(input);
    return true;
  }
  int start = rn_text_index_at(text, local.x, local.y, clicks == 1);
  // A press inside the selection may start dragging it out: keep it (a
  // release without a drag puts the caret there).
  int selStart = 0, selEnd = 0;
  if (clicks == 1 && rn_text_get_selection(text, &selStart, &selEnd) && selStart != selEnd) {
    int at = rn_text_index_at(text, local.x, local.y, FALSE);
    if (at >= std::min(selStart, selEnd) && at < std::max(selStart, selEnd)) {
      pressInSelection_ = true;
      pressIndex_ = start;
      selecting_ = false;
      return true;
    }
  }
  int end = start;
  if (clicks == 2) rn_text_extend_to_words(text, &start, &end);
  if (clicks == 3) rn_text_extend_to_paragraph(text, &start, &end);
  anchorStart_ = start;
  anchorEnd_ = end;
  rn_text_set_selection(text, start, end);
  if (start != end) cancelTouches();
  return true;
}

void GtkPointerHandler::extendSelection(const Input &input) {
  GtkWidget *widget = selectionWidget_.get();
  if (!widget) return;
  auto *text = RN_TEXT(widget);
  graphene_point_t p{float(input.x), float(input.y)}, local;
  if (!gtk_widget_compute_point(root_, widget, &p, &local)) return;
  int start = rn_text_index_at(text, local.x, local.y, selectionUnit_ == 1);
  int end = start;
  if (selectionUnit_ == 2) rn_text_extend_to_words(text, &start, &end);
  if (selectionUnit_ == 3) rn_text_extend_to_paragraph(text, &start, &end);
  // Backwards from the anchor's end, or forwards from its start.
  if (start < anchorStart_) {
    rn_text_set_selection(text, anchorEnd_, start);
  } else {
    rn_text_set_selection(text, anchorStart_, std::max(end, anchorEnd_));
  }
  int s, e;
  // Selecting, not pressing: whatever the press started is cancelled.
  if (rn_text_get_selection(text, &s, &e)) cancelTouches();
}

void GtkPointerHandler::clearSelection() {
  if (selectionWidget_) rn_text_set_selection(RN_TEXT(selectionWidget_.get()), 0, 0);
  selectionWidget_ = nullptr;
  selecting_ = false;
}

std::string GtkPointerHandler::selectedText() const {
  if (!selectionWidget_) return "";
  char *text = rn_text_get_selected_text(RN_TEXT(selectionWidget_.get()));
  std::string s = text ? text : "";
  g_free(text);
  return s;
}

bool GtkPointerHandler::copySelection() {
  std::string text = selectedText();
  if (text.empty()) return false;
  gdk_clipboard_set_text(gdk_display_get_clipboard(gtk_widget_get_display(root_)),
                         text.c_str());
  return true;
}

gboolean GtkPointerHandler::onCopyShortcut(GtkWidget *, GVariant *,
                                           gpointer self) {
  return static_cast<GtkPointerHandler *>(self)->copySelection();
}

void GtkPointerHandler::showCopyMenu(const Target &target, double x, double y) {
  if (!target.widget || !RN_IS_TEXT(target.widget.get())) return;
  // The selection if this paragraph has one, else all of it.
  auto *paragraph = RN_TEXT(target.widget.get());
  char *selected = rn_text_get_selected_text(paragraph);
  std::string text = selected ? selected : rn_text_get_text(paragraph);
  g_free(selected);
  GMenu *menu = g_menu_new();
  g_menu_append(menu, "Copy", "rngtk-text.copy");
  g_menu_append(menu, "Select All", "rngtk-text.select-all");
  GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
  g_object_unref(menu);

  GSimpleActionGroup *group = g_simple_action_group_new();
  GSimpleAction *copy = g_simple_action_new("copy", nullptr);
  g_signal_connect_data(
      copy, "activate",
      G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer data) {
        auto *text = static_cast<std::string *>(data);
        gdk_clipboard_set_text(gdk_display_get_clipboard(gdk_display_get_default()),
                               text->c_str());
      }),
      new std::string(text),
      +[](gpointer data, GClosure *) { delete static_cast<std::string *>(data); },
      GConnectFlags(0));
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(copy));
  g_object_unref(copy);
  GSimpleAction *selectAll = g_simple_action_new("select-all", nullptr);
  struct SelectAll {
    GtkPointerHandler *handler;
    std::shared_ptr<GtkWidget> widget;
  };
  g_signal_connect_data(
      selectAll, "activate",
      G_CALLBACK(+[](GSimpleAction *, GVariant *, gpointer data) {
        auto *d = static_cast<SelectAll *>(data);
        auto *text = RN_TEXT(d->widget.get());
        if (d->handler->selectionWidget_ != d->widget) d->handler->clearSelection();
        d->handler->selectionWidget_ = d->widget;
        d->handler->selecting_ = false;
        rn_text_set_selection(text, 0, int(strlen(rn_text_get_text(text))));
      }),
      new SelectAll{this, target.widget},
      +[](gpointer data, GClosure *) { delete static_cast<SelectAll *>(data); },
      GConnectFlags(0));
  g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(selectAll));
  g_object_unref(selectAll);
  gtk_widget_insert_action_group(popover, "rngtk-text", G_ACTION_GROUP(group));
  g_object_unref(group);

  // RNView presents popovers parented to it (see rn_view_size_allocate).
  gtk_widget_set_parent(popover, root_);
  GdkRectangle at{int(x), int(y), 1, 1};
  gtk_popover_set_pointing_to(GTK_POPOVER(popover), &at);
  gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
  g_signal_connect(popover, "closed",
                   G_CALLBACK(+[](GtkPopover *p, gpointer) {
                     // Unparent once GTK is done with the close.
                     g_idle_add_full(
                         G_PRIORITY_DEFAULT_IDLE,
                         [](gpointer w) -> gboolean {
                           gtk_widget_unparent(GTK_WIDGET(w));
                           return G_SOURCE_REMOVE;
                         },
                         g_object_ref(p), g_object_unref);
                   }),
                   nullptr);
  gtk_popover_popup(GTK_POPOVER(popover));
}

}  // namespace rngtk
