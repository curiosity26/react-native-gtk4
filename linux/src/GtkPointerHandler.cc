#include "GtkPointerHandler.h"

#include "GtkMountingManager.h"
#include "rn_scroll_view.h"
#include "rn_text.h"
#include "rn_view.h"

#include <react/renderer/components/view/PointerEvent.h>
#include <react/renderer/components/view/TouchEvent.h>
#include <react/renderer/components/view/TouchEventEmitter.h>
#include <react/renderer/components/view/primitives.h>

#include <algorithm>

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
}

GtkPointerHandler::~GtkPointerHandler() {
  g_signal_handlers_disconnect_by_data(controller_, this);
  gtk_widget_remove_controller(root_, controller_);
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
  dispatch(input);
  return false;  // let GTK carry on (cursors, the dev menu button...)
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

  // Hover (pointerover/out, pointerenter/leave) follows the mouse.
  if (input.device == Device::Mouse && input.phase != Phase::Scroll) {
    updateHover(input, target);
  }

  switch (input.phase) {
    case Phase::Down: {
      buttons_ |= input.device == Device::Mouse ? buttonBit(input.button) : 1;
      pressTarget_ = target;
      dispatchPointer("pointerDown", target, input);
      if (primary) {
        touches_[id] = ActiveTouch{target, input.x, input.y, input.timeMs};
        dispatchTouch("touchStart", id, input);
      } else if (input.button == 3 &&
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
  event.button = name == "pointerMove" ? -1 : w3cButton(input.button);
  event.ctrlKey = input.modifiers & GDK_CONTROL_MASK;
  event.shiftKey = input.modifiers & GDK_SHIFT_MASK;
  event.altKey = input.modifiers & GDK_ALT_MASK;
  event.metaKey = input.modifiers & (GDK_META_MASK | GDK_SUPER_MASK);
  event.isPrimary = true;
  event.timeStamp = HighResTimeStamp::now();

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
    }
  }
  hovered_ = std::move(path);
}

void GtkPointerHandler::showCopyMenu(const Target &target, double x, double y) {
  if (!target.widget || !RN_IS_TEXT(target.widget.get())) return;
  std::string text = rn_text_get_text(RN_TEXT(target.widget.get()));
  GMenu *menu = g_menu_new();
  g_menu_append(menu, "Copy", "rngtk-text.copy");
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
