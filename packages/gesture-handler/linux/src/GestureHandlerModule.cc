// react-native-gesture-handler's native side on Linux:
//
// - RNGestureHandlerModule (the TurboModule): handlers created, configured,
//   attached to views and dropped, on the main thread in call order, after
//   the mounts JS committed before the call. It puts _setGestureStateAsync
//   (manual activation from JS), _isViewFlatteningDisabled and
//   _RNGH_MODULE_ID on the JS runtime.
// - Input: every pointer event on the app's surfaces (rngtk::Host's pointer
//   observer). A press reaches the handlers attached to the view under it
//   and its ancestors, each through an event manager like the web
//   implementation's PointerEventManager (down / additional down, moves in
//   and out of the view, enter and leave, up, cancel; hover for Hover
//   handlers; touchpad scrolling for Pan).
// - Events: device events (onGestureHandlerEvent, onGestureHandlerStateChange)
//   for the old and the new JS API, events on the RNGestureHandlerDetector
//   for the hook API (v3), and onGestureHandlerEvent on the view for
//   Animated.event with the native driver.
// - RNGestureHandlerDetector (v3's GestureDetector): its frame is its
//   children's, and it attaches the handlers in handlerTags to itself (and
//   virtualChildren's to those views).
#include "GestureHandler.h"
#include "Orchestrator.h"

#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/uimanager/primitives.h>
#include <rngtk/CxxModule.h>
#include <rngtk/Extensions.h>

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <set>

using namespace facebook;
using namespace facebook::react;

namespace rngtk_gh {

namespace {

rngtk::Host *host = nullptr;
constexpr int kModuleId = 1;

Orchestrator &orchestrator() {
  static Orchestrator o;
  return o;
}

// ---- Attachments: a handler on a view -----------------------------------------

struct Attachment final : HandlerDelegate {
  GestureHandler *handler = nullptr;
  GtkWidget *view = nullptr;      // weak
  GtkWidget *detector = nullptr;  // weak: the detector its events go to (v3)
  GtkWidget *root = nullptr;      // weak: the surface its input comes from
  std::set<guint> timers;
  // The event manager (the web's PointerEventManager).
  int activePointers = 0;
  std::vector<int> inBounds;
  std::set<int> tracked;
  bool hoverInside = false;
  double wheelX = 0, wheelY = 0;

  ~Attachment() override {
    for (guint id : timers) g_source_remove(id);
    if (view) g_object_remove_weak_pointer(G_OBJECT(view), reinterpret_cast<gpointer *>(&view));
    if (detector) g_object_remove_weak_pointer(G_OBJECT(detector), reinterpret_cast<gpointer *>(&detector));
    if (root) g_object_remove_weak_pointer(G_OBJECT(root), reinterpret_cast<gpointer *>(&root));
  }
  void setWeak(GtkWidget *&field, GtkWidget *value) {
    if (field == value) return;
    if (field) g_object_remove_weak_pointer(G_OBJECT(field), reinterpret_cast<gpointer *>(&field));
    field = value;
    if (field) g_object_add_weak_pointer(G_OBJECT(field), reinterpret_cast<gpointer *>(&field));
  }

  // Reported coordinates are in the detector's single child, as on Android
  // (that's the view transforms apply to).
  GtkWidget *coordinateView() const {
    if (!view) return nullptr;
    GtkWidget *child = gtk_widget_get_first_child(view);
    if (handler->actionType() == ACTION_NATIVE_DETECTOR && child && !gtk_widget_get_next_sibling(child)) {
      return child;
    }
    return view;
  }

  graphene_rect_t viewBounds() const override {
    graphene_rect_t b;
    graphene_rect_init(&b, 0, 0, 0, 0);
    GtkWidget *v = coordinateView();
    if (v && root && !gtk_widget_compute_bounds(v, root, &b)) graphene_rect_init(&b, 0, 0, 0, 0);
    return b;
  }

  Point local(double x, double y) const {
    GtkWidget *v = coordinateView();
    graphene_point_t in, out;
    graphene_point_init(&in, float(x), float(y));
    out = in;
    if (v && root && gtk_widget_compute_point(root, v, &in, &out)) return {out.x, out.y};
    graphene_rect_t b = viewBounds();
    return {x - b.origin.x, y - b.origin.y};
  }

  void sendEvent(GestureHandler &h, folly::dynamic event, const char *kind) override;

  guint startTimer(int ms, std::function<void()> fn) override {
    struct Timer {
      Attachment *self;
      std::function<void()> fn;
      guint id = 0;
    };
    auto *t = new Timer{this, std::move(fn)};
    t->id = g_timeout_add_full(G_PRIORITY_DEFAULT, guint(std::max(ms, 0)), [](gpointer data) -> gboolean {
      auto *t = static_cast<Timer *>(data);
      t->self->timers.erase(t->id);
      auto fn = std::move(t->fn);
      fn();
      return G_SOURCE_REMOVE;
    }, t, [](gpointer data) { delete static_cast<Timer *>(data); });
    timers.insert(t->id);
    return t->id;
  }
  void cancelTimer(guint id) override {
    if (timers.erase(id)) g_source_remove(id);
  }
  void cancelJSResponder() override {
    if (host && root) host->cancelTouches(root);
  }
  void buttonPressed(bool pressed) override;
  void buttonEvent(const char *name, folly::dynamic payload) override {
    if (!host || !view) return;
    if (auto emitter = host->eventEmitterForView(view)) emitter->dispatchEvent(name, std::move(payload));
  }
};

std::map<int, std::unique_ptr<GestureHandler>> handlers;
std::map<int, std::unique_ptr<Attachment>> attachments;  // by handler tag
// Handlers detectors want, which may be created after the detector mounts:
// handler tag -> (detector, view, action type).
struct Wanted {
  GtkWidget *detector;
  int viewTag;  // 0: the detector itself
  ActionType actionType;
};
std::multimap<int, Wanted> wanted;

GestureHandler *handlerFor(int tag) {
  auto it = handlers.find(tag);
  return it == handlers.end() ? nullptr : it->second.get();
}

void detach(int tag) {
  auto it = attachments.find(tag);
  if (it == attachments.end()) return;
  if (GestureHandler *h = handlerFor(tag)) h->detach();
  attachments.erase(it);
}

void attach(int tag, GtkWidget *view, ActionType actionType, GtkWidget *detector) {
  GestureHandler *h = handlerFor(tag);
  if (!h || !view) return;
  detach(tag);
  auto a = std::make_unique<Attachment>();
  a->handler = h;
  a->setWeak(a->view, view);
  a->setWeak(a->detector, detector);
  h->attach(a.get(), actionType);
  h->view = view;
  h->hostDetector = detector;
  std::string component = host->componentName(view);
  h->viewRole = component == "RNGestureHandlerButton" ? "Button"
                : component == "ScrollView"           ? "ScrollView"
                : component == "Switch"               ? "Switch"
                                                      : "";
  attachments[tag] = std::move(a);
}

// attachGestureHandler for a view React rendered but the host hasn't
// mounted yet (layout effects run before the mount reaches the main
// thread): try again for a second.
void attachWhenMounted(int tag, int viewTag, ActionType actionType, int tries) {
  if (!handlerFor(tag)) return;
  if (GtkWidget *view = host->viewForTag(viewTag)) {
    attach(tag, view, actionType, nullptr);
    return;
  }
  if (tries >= 60) return;
  struct Retry {
    int tag, viewTag;
    ActionType actionType;
    int tries;
  };
  g_timeout_add_full(G_PRIORITY_DEFAULT, 16, [](gpointer data) -> gboolean {
    auto *r = static_cast<Retry *>(data);
    attachWhenMounted(r->tag, r->viewTag, r->actionType, r->tries + 1);
    return G_SOURCE_REMOVE;
  }, new Retry{tag, viewTag, actionType, tries}, [](gpointer data) { delete static_cast<Retry *>(data); });
}

// ---- RNGestureHandlerButton ---------------------------------------------------

// A button's look while pressed or hovered (activeOpacity, activeScale,
// activeUnderlayOpacity, the hover ones; defaults otherwise), animated over
// tapAnimationIn/OutDuration and hoverAnimationIn/OutDuration and applied
// without a commit (host->setNativeProps, as native Animated does). The
// underlay (underlayColor at that opacity) is blended into the background.
struct Button {
  GtkWidget *view = nullptr;
  folly::dynamic props = folly::dynamic::object();
  // What the feedback last applied: the host hands those back as props.
  folly::dynamic applied = folly::dynamic::object();
  SharedEventEmitter emitter;
  bool pressed = false, hovered = false;
  double opacity = -1, scale = -1, underlay = -1;  // current; -1 before the first frame
  struct Animation {
    double from[3], to[3];
    gint64 start = 0, duration = 0;
  } animation;
  guint tick = 0;

  double prop(const char *key, double fallback) const {
    auto it = props.find(key);
    return it != props.items().end() && it->second.isNumber() ? it->second.asDouble() : fallback;
  }
  bool hasFeedback() const {
    for (const char *k : {"activeOpacity", "activeScale", "activeUnderlayOpacity", "hoverOpacity", "hoverScale",
                          "hoverUnderlayOpacity", "defaultOpacity", "defaultScale", "defaultUnderlayOpacity"}) {
      double v = prop(k, NAN);
      bool isDefault = std::isnan(v) || (strstr(k, "hover") && v < 0) ||
                       ((!strcmp(k, "activeOpacity") || !strcmp(k, "activeScale") || !strcmp(k, "defaultOpacity") ||
                         !strcmp(k, "defaultScale")) && v == 1) ||
                       (strstr(k, "UnderlayOpacity") && v == 0);
      if (!isDefault) return true;
    }
    return false;
  }
  void targets(double out[3]) const {
    out[0] = prop("defaultOpacity", 1);
    out[1] = prop("defaultScale", 1);
    out[2] = prop("defaultUnderlayOpacity", 0);
    if (hovered) {
      if (prop("hoverOpacity", -1) >= 0) out[0] = prop("hoverOpacity", -1);
      if (prop("hoverScale", -1) >= 0) out[1] = prop("hoverScale", -1);
      if (prop("hoverUnderlayOpacity", -1) >= 0) out[2] = prop("hoverUnderlayOpacity", -1);
    }
    if (pressed) {
      out[0] = prop("activeOpacity", 1);
      out[1] = prop("activeScale", 1);
      out[2] = prop("activeUnderlayOpacity", 0);
    }
  }
  void update(bool pressing, bool hovering) {
    if (!hasFeedback()) return;
    double to[3];
    targets(to);
    if (opacity < 0) {
      opacity = to[0];
      scale = to[1];
      underlay = to[2];
      apply();
      return;
    }
    animation.from[0] = opacity;
    animation.from[1] = scale;
    animation.from[2] = underlay;
    std::copy(to, to + 3, animation.to);
    const char *key = pressing ? "tapAnimationInDuration"
                      : hovering ? (hovered ? "hoverAnimationInDuration" : "hoverAnimationOutDuration")
                                 : "tapAnimationOutDuration";
    double fallback = pressing ? 50 : hovering ? (hovered ? 50 : 100) : 100;
    animation.duration = gint64(std::max(0.0, prop(key, fallback)) * 1000);
    animation.start = g_get_monotonic_time();
    if (!tick && view) {
      tick = gtk_widget_add_tick_callback(view, [](GtkWidget *, GdkFrameClock *, gpointer self) -> gboolean {
        return static_cast<Button *>(self)->step() ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
      }, this, nullptr);
    }
  }
  bool step() {
    double t = animation.duration > 0
                   ? std::min(1.0, double(g_get_monotonic_time() - animation.start) / double(animation.duration))
                   : 1.0;
    opacity = animation.from[0] + (animation.to[0] - animation.from[0]) * t;
    scale = animation.from[1] + (animation.to[1] - animation.from[1]) * t;
    underlay = animation.from[2] + (animation.to[2] - animation.from[2]) * t;
    apply();
    if (t < 1) return true;
    tick = 0;
    return false;
  }
  static uint32_t argb(const folly::dynamic &v, uint32_t fallback) {
    return v.isNumber() ? uint32_t(int64_t(v.asDouble())) : fallback;
  }
  void apply() {
    if (!host || !view) return;
    folly::dynamic transform = folly::dynamic::array();
    if (props.count("transform") && props["transform"].isArray()) transform = props["transform"];
    transform.push_back(folly::dynamic::object("scale", scale));
    folly::dynamic out = folly::dynamic::object("opacity", opacity * prop("opacity", 1))("transform", transform);
    // The underlay over the background.
    uint32_t bg = argb(props.count("backgroundColor") ? props["backgroundColor"] : folly::dynamic(), 0);
    uint32_t ul = argb(props.count("underlayColor") ? props["underlayColor"] : folly::dynamic(), 0xFF000000);
    double ua = ((ul >> 24) & 0xFF) / 255.0 * underlay, ba = ((bg >> 24) & 0xFF) / 255.0;
    double a = ua + ba * (1 - ua);
    auto channel = [&](int shift) {
      double c = a > 0 ? (((ul >> shift) & 0xFF) * ua + ((bg >> shift) & 0xFF) * ba * (1 - ua)) / a : 0;
      return uint32_t(std::lround(c)) & 0xFF;
    };
    uint32_t blended = (uint32_t(std::lround(a * 255)) << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
    out["backgroundColor"] = double(int32_t(blended));
    applied = out;
    host->setNativeProps(view, out);
  }
};

std::map<GtkWidget *, std::unique_ptr<Button>> buttons;

Button *buttonFor(GtkWidget *view) {
  auto it = buttons.find(view);
  return it == buttons.end() ? nullptr : it->second.get();
}

void Attachment::buttonPressed(bool pressed) {
  Button *b = buttonFor(view);
  if (!b || b->pressed == pressed) return;
  b->pressed = pressed;
  b->update(true, false);
  if (!pressed) b->update(false, false);
}

// The mouse over buttons: hover feedback and onButtonHoverIn/Out.
void hoverButtons(GtkWidget *target) {
  for (auto &[view, b] : buttons) {
    bool now = target && (target == view || gtk_widget_is_ancestor(target, view));
    if (now == b->hovered) continue;
    b->hovered = now;
    b->update(false, true);
    if (auto emitter = host ? host->eventEmitterForView(view) : nullptr) {
      emitter->dispatchEvent(now ? "onButtonHoverIn" : "onButtonHoverOut",
                             folly::dynamic::object("pointerInside", now)("x", 0)("y", 0)("absoluteX", 0)(
                                 "absoluteY", 0)("numberOfPointers", 0)("pointerType", int(POINTER_MOUSE)));
    }
  }
}

void Attachment::sendEvent(GestureHandler &h, folly::dynamic event, const char *kind) {
  if (!host) return;
  bool stateChange = !strcmp(kind, "stateChange"), touch = !strcmp(kind, "touch");
  switch (h.actionType()) {
    case ACTION_JS_FUNCTION_OLD_API:
    case ACTION_JS_FUNCTION_NEW_API:
      if (touch && h.actionType() == ACTION_JS_FUNCTION_OLD_API) return;
      host->emitDeviceEvent(stateChange ? "onGestureHandlerStateChange" : "onGestureHandlerEvent",
                            std::move(event));
      break;
    case ACTION_NATIVE_ANIMATED_EVENT:
      if (stateChange) {
        host->emitDeviceEvent("onGestureHandlerStateChange", std::move(event));
      } else if (!touch) {
        if (auto emitter = host->eventEmitterForView(view)) {
          emitter->dispatchEvent("onGestureHandlerEvent", std::move(event));
        }
      }
      break;
    case ACTION_NATIVE_DETECTOR:
    case ACTION_VIRTUAL_DETECTOR: {
      auto emitter = host->eventEmitterForView(detector);
      if (!emitter) return;
      const char *name;
      if (stateChange) {
        name = h.dispatchesReanimatedEvents() ? "onGestureHandlerReanimatedStateChange"
                                              : "onGestureHandlerStateChange";
      } else if (touch) {
        name = h.dispatchesReanimatedEvents() ? "onGestureHandlerReanimatedTouchEvent"
                                              : "onGestureHandlerTouchEvent";
      } else {
        name = h.dispatchesAnimatedEvents()     ? "onGestureHandlerAnimatedEvent"
               : h.dispatchesReanimatedEvents() ? "onGestureHandlerReanimatedEvent"
                                                : "onGestureHandlerEvent";
      }
      emitter->dispatchEvent(name, std::move(event));
      break;
    }
    default: break;  // Reanimated worklets: with Reanimated
  }
}

// ---- Input --------------------------------------------------------------------

PointerType pointerTypeOf(const rngtk::PointerInput &e) {
  return e.device == rngtk::PointerInput::Device::Touch ? POINTER_TOUCH : POINTER_MOUSE;
}

// GDK's buttons (1 primary, 2 middle, 3 secondary, 8/9 back/forward) as the
// library's MouseButton flags.
int buttonFlag(const rngtk::PointerInput &e) {
  if (e.device != rngtk::PointerInput::Device::Mouse) return 0;
  switch (e.button) {
    case 1: return MOUSE_LEFT;
    case 2: return MOUSE_MIDDLE;
    case 3: return MOUSE_RIGHT;
    case 8: return MOUSE_BUTTON_4;
    case 9: return MOUSE_BUTTON_5;
    default: return 0;
  }
}

AdaptedEvent adapt(const Attachment &a, const rngtk::PointerInput &e, EventType type) {
  AdaptedEvent out;
  out.x = e.x;
  out.y = e.y;
  Point l = a.local(e.x, e.y);
  out.offsetX = l.x;
  out.offsetY = l.y;
  out.pointerId = e.pointerId;
  out.eventType = type;
  out.pointerType = pointerTypeOf(e);
  out.time = e.timeMs ? double(e.timeMs) : double(g_get_monotonic_time()) / 1000.0;
  out.button = buttonFlag(e);
  return out;
}

bool contains(const std::vector<int> &v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// The attachments for a press on `target`: the target's and its ancestors'
// (innermost first, as DOM events bubble).
std::vector<Attachment *> attachmentsAlong(GtkWidget *target, GtkWidget *root) {
  std::vector<Attachment *> out;
  for (GtkWidget *w = target; w; w = gtk_widget_get_parent(w)) {
    for (auto &[tag, a] : attachments) {
      if (a->view == w && a->handler->enabled()) out.push_back(a.get());
    }
    if (w == root) break;
  }
  return out;
}

bool isHover(const Attachment &a) { return a.handler->name() == "HoverGestureHandler"; }

bool inside(GtkWidget *target, GtkWidget *view) {
  return target && view && (target == view || gtk_widget_is_ancestor(target, view));
}

bool observe(const rngtk::PointerInput &e) {
  using Phase = rngtk::PointerInput::Phase;
  switch (e.phase) {
    case Phase::Down: {
      for (Attachment *a : attachmentsAlong(e.target, e.root)) {
        a->setWeak(a->root, e.root);
        if (!a->handler->isPointerInBounds({e.x, e.y})) continue;
        AdaptedEvent ev = adapt(*a, e, EventType::Down);
        if (!contains(a->inBounds, e.pointerId)) a->inBounds.push_back(e.pointerId);
        a->tracked.insert(e.pointerId);
        if (++a->activePointers > 1) {
          ev.eventType = EventType::AdditionalPointerDown;
          a->handler->onPointerAdd(ev);
        } else {
          a->handler->onPointerDown(ev);
        }
      }
      break;
    }
    case Phase::Move: {
      if (e.device == rngtk::PointerInput::Device::Mouse) hoverButtons(e.target);
      std::vector<Attachment *> list;
      for (auto &[tag, a] : attachments) list.push_back(a.get());
      for (Attachment *a : list) {
        if (attachments.find(a->handler->tag()) == attachments.end()) continue;
        if (!a->handler->enabled()) continue;
        bool pressed = a->tracked.count(e.pointerId) > 0;
        bool hover = isHover(*a) && e.device == rngtk::PointerInput::Device::Mouse;
        if (hover && a->root != e.root) a->setWeak(a->root, e.root);
        // pointerenter / pointerleave on the view: hover.
        if (hover) {
          bool now = inside(e.target, a->view);
          if (now != a->hoverInside) {
            a->hoverInside = now;
            AdaptedEvent ev = adapt(*a, e, now ? EventType::Enter : EventType::Leave);
            if (now) a->handler->onPointerMoveOver(ev);
            else a->handler->onPointerMoveOut(ev);
          }
        }
        if (!pressed && !(hover && a->hoverInside)) continue;
        AdaptedEvent ev = adapt(*a, e, EventType::Move);
        bool in = a->handler->isPointerInBounds({e.x, e.y});
        bool wasIn = contains(a->inBounds, e.pointerId);
        if (in) {
          if (!wasIn && pressed) {
            ev.eventType = EventType::Enter;
            a->handler->onPointerEnter(ev);
            a->inBounds.push_back(e.pointerId);
          } else {
            a->handler->onPointerMove(ev);
          }
        } else if (wasIn) {
          ev.eventType = EventType::Leave;
          a->handler->onPointerLeave(ev);
          a->inBounds.erase(std::remove(a->inBounds.begin(), a->inBounds.end(), e.pointerId), a->inBounds.end());
        } else {
          a->handler->onPointerOutOfBounds(ev);
        }
        a->wheelX = a->wheelY = 0;
      }
      break;
    }
    case Phase::Up: {
      std::vector<Attachment *> list;
      for (auto &[tag, a] : attachments) {
        if (a->tracked.count(e.pointerId) && a->activePointers > 0) list.push_back(a.get());
      }
      for (Attachment *a : list) {
        if (attachments.find(a->handler->tag()) == attachments.end()) continue;
        AdaptedEvent ev = adapt(*a, e, EventType::Up);
        a->inBounds.erase(std::remove(a->inBounds.begin(), a->inBounds.end(), e.pointerId), a->inBounds.end());
        a->tracked.erase(e.pointerId);
        if (--a->activePointers > 0) {
          ev.eventType = EventType::AdditionalPointerUp;
          a->handler->onPointerRemove(ev);
        } else {
          a->handler->onPointerUp(ev);
        }
      }
      break;
    }
    case Phase::Cancel: {
      std::vector<Attachment *> list;
      for (auto &[tag, a] : attachments) {
        if (a->tracked.count(e.pointerId)) list.push_back(a.get());
      }
      for (Attachment *a : list) {
        if (attachments.find(a->handler->tag()) == attachments.end()) continue;
        a->handler->onPointerCancel(adapt(*a, e, EventType::Cancel));
        a->inBounds.clear();
        a->tracked.clear();
        a->activePointers = 0;
      }
      break;
    }
    case Phase::Leave: {
      hoverButtons(nullptr);
      // The mouse left the surface: hover ends.
      for (auto &[tag, a] : attachments) {
        if (!isHover(*a) || !a->hoverInside || a->root != e.root) continue;
        a->hoverInside = false;
        a->handler->onPointerMoveOut(adapt(*a, e, EventType::Leave));
      }
      break;
    }
    case Phase::Pinch: {
      // A touchpad pinch: the Pinch and Rotation handlers under the pointer.
      for (Attachment *a : attachmentsAlong(e.target, e.root)) {
        const std::string &name = a->handler->name();
        if (name != "PinchGestureHandler" && name != "RotationGestureHandler") continue;
        a->setWeak(a->root, e.root);
        a->handler->onTouchpadPinch(GestureHandler::PinchPhase(int(e.pinchPhase)), Point{e.x, e.y}, e.scale,
                                    e.angleDelta, double(e.timeMs));
      }
      break;
    }
    case Phase::Scroll: {
      // Touchpad scrolling: Pan with enableTrackpadTwoFingerGesture. A
      // wheel notch is 120 (the web's wheelDeltaY); a smooth scroll unit
      // here moves 20 px.
      for (Attachment *a : attachmentsAlong(e.target, e.root)) {
        if (a->handler->name() != "PanGestureHandler") continue;
        a->setWeak(a->root, e.root);
        bool notch = e.dy == std::round(e.dy) && e.dx == std::round(e.dx) && (e.dy != 0 || e.dx != 0);
        a->wheelX += e.dx * 20;
        a->wheelY += e.dy * 20;
        AdaptedEvent ev = adapt(*a, e, EventType::Move);
        ev.x += a->wheelX;
        ev.y += a->wheelY;
        ev.offsetX -= e.dx * 20;
        ev.offsetY -= e.dy * 20;
        ev.pointerId = -1;
        ev.pointerType = POINTER_OTHER;
        ev.wheelDeltaY = notch ? e.dy * 120 : e.dy * 20 + 0.5;
        a->handler->onWheel(ev);
      }
      break;
    }
  }
  return false;
}

// ---- Operations from JS, in order on the main thread --------------------------

void configureRelations(GestureHandler &h, const folly::dynamic &relations) {
  auto tags = [&](const char *key, std::vector<int> &out) {
    out.clear();
    if (!relations.isObject() || !relations.count(key) || !relations[key].isArray()) return;
    for (auto &t : relations[key]) {
      if (t.isNumber()) out.push_back(int(t.asDouble()));
      else if (t.isObject() && t.count("handlerTag")) out.push_back(int(t["handlerTag"].asDouble()));
    }
  };
  tags("waitFor", h.waitFor);
  tags("simultaneousHandlers", h.simultaneousWith);
  tags("blocksHandlers", h.blocks);
}

void tryWanted(int tag);
void attachToDetector(int tag, GtkWidget *detector, int viewTag, ActionType type, int tries);

void createGestureHandler(const std::string &name, int tag, const folly::dynamic &config) {
  auto h = createHandler(name, tag, orchestrator());
  if (!h) {
    fprintf(stderr, "react-native-gesture-handler: %s isn't supported on Linux yet\n", name.c_str());
    return;
  }
  h->setGestureConfig(config);
  handlers[tag] = std::move(h);
  tryWanted(tag);
}

void dropGestureHandler(int tag) {
  detach(tag);
  handlers.erase(tag);
}

void tryWanted(int tag) {
  auto range = wanted.equal_range(tag);
  for (auto it = range.first; it != range.second; ++it) {
    const Wanted &w = it->second;
    attachToDetector(tag, w.detector, w.viewTag, w.actionType, 0);
  }
}

// ---- The TurboModule ----------------------------------------------------------

class Module final : public rngtk::CxxModule<Module> {
 public:
  explicit Module(std::shared_ptr<CallInvoker> js) : CxxModule("RNGestureHandlerModule", std::move(js)) {
    method<&Module::createGestureHandler>("createGestureHandler");
    method<&Module::attachGestureHandler>("attachGestureHandler");
    method<&Module::setGestureHandlerConfig>("setGestureHandlerConfig");
    method<&Module::updateGestureHandlerConfig>("updateGestureHandlerConfig");
    method<&Module::configureRelations>("configureRelations");
    method<&Module::dropGestureHandler>("dropGestureHandler");
    method<&Module::flushOperations>("flushOperations");
    method<&Module::installUIRuntimeBindings>("installUIRuntimeBindings");
  }

  jsi::Value get(jsi::Runtime &rt, const jsi::PropNameID &name) override {
    if (!installed_) {
      installed_ = true;
      installRuntimeBindings(rt);
    }
    return CxxModule::get(rt, name);
  }

  void createGestureHandler(jsi::Runtime &, std::string name, double tag, folly::dynamic config) {
    post([name, tag = int(tag), config] { rngtk_gh::createGestureHandler(name, tag, config); });
  }
  void attachGestureHandler(jsi::Runtime &, double tag, double viewTag, double actionType) {
    post([tag = int(tag), viewTag = int(viewTag), actionType = int(actionType)] {
      attachWhenMounted(tag, viewTag, ActionType(actionType), 0);
    });
  }
  void setGestureHandlerConfig(jsi::Runtime &, double tag, folly::dynamic config) {
    post([tag = int(tag), config] {
      if (GestureHandler *h = handlerFor(tag)) h->setGestureConfig(config);
    });
  }
  void updateGestureHandlerConfig(jsi::Runtime &, double tag, folly::dynamic config) {
    post([tag = int(tag), config] {
      if (GestureHandler *h = handlerFor(tag)) h->updateGestureConfig(config);
    });
  }
  void configureRelations(jsi::Runtime &, double tag, folly::dynamic relations) {
    post([tag = int(tag), relations] {
      if (GestureHandler *h = handlerFor(tag)) rngtk_gh::configureRelations(*h, relations);
    });
  }
  void dropGestureHandler(jsi::Runtime &, double tag) {
    post([tag = int(tag)] { rngtk_gh::dropGestureHandler(tag); });
  }
  void flushOperations(jsi::Runtime &) {}
  // react-native-worklets' UI runtime (the host's "worklets.uiRuntime"
  // service): _setGestureStateSync there, for GestureStateManager in
  // worklets. The UI runtime runs on the main thread, as the handlers do.
  bool installUIRuntimeBindings(jsi::Runtime &) {
    using RunOnUI = std::function<void(std::function<void(jsi::Runtime &)>)>;
    auto runOnUI = std::static_pointer_cast<RunOnUI>(host->service("worklets.uiRuntime"));
    if (!runOnUI) return false;
    (*runOnUI)([](jsi::Runtime &ui) {
      ui.global().setProperty(
          ui, "_setGestureStateSync",
          jsi::Function::createFromHostFunction(
              ui, jsi::PropNameID::forAscii(ui, "_setGestureStateSync"), 2,
              [](jsi::Runtime &, const jsi::Value &, const jsi::Value *args, size_t count) -> jsi::Value {
                if (count == 2 && args[0].isNumber() && args[1].isNumber()) {
                  if (GestureHandler *h = handlerFor(int(args[0].asNumber()))) {
                    h->setStateFromJS(State(int(args[1].asNumber())));
                  }
                }
                return jsi::Value::undefined();
              }));
    });
    return true;
  }

 private:
  static void post(std::function<void()> fn) {
    if (host) host->runAfterMounts(std::move(fn));
  }

  void installRuntimeBindings(jsi::Runtime &rt) {
    rt.global().setProperty(rt, "_RNGH_MODULE_ID", jsi::Value(kModuleId));
    rt.global().setProperty(
        rt, "_setGestureStateAsync",
        jsi::Function::createFromHostFunction(
            rt, jsi::PropNameID::forAscii(rt, "_setGestureStateAsync"), 2,
            [](jsi::Runtime &, const jsi::Value &, const jsi::Value *args, size_t count) -> jsi::Value {
              if (count == 2 && args[0].isNumber() && args[1].isNumber()) {
                int tag = int(args[0].asNumber()), state = int(args[1].asNumber());
                post([tag, state] {
                  if (GestureHandler *h = handlerFor(tag)) h->setStateFromJS(State(state));
                });
              }
              return jsi::Value::undefined();
            }));
    // The old API's dev check that a GestureDetector's child forms a view.
    rt.global().setProperty(
        rt, "_isViewFlatteningDisabled",
        jsi::Function::createFromHostFunction(
            rt, jsi::PropNameID::forAscii(rt, "_isViewFlatteningDisabled"), 1,
            [](jsi::Runtime &rt, const jsi::Value &, const jsi::Value *args, size_t count) -> jsi::Value {
              if (count < 1 || !args[0].isObject()) return jsi::Value::null();
              auto node = Bridging<std::shared_ptr<const ShadowNode>>::fromJs(rt, args[0]);
              if (!node) return jsi::Value::null();
              const char *name = node->getComponentName();
              bool text = !strcmp(name, "Paragraph") || !strcmp(name, "Text");
              return jsi::Value(node->getTraits().check(ShadowNodeTraits::FormsStackingContext) || text);
            }));
  }

  bool installed_ = false;
};

}  // namespace

// ---- RNGestureHandlerDetector -------------------------------------------------

class DetectorProps final : public ViewProps {
 public:
  DetectorProps() = default;
  DetectorProps(const PropsParserContext &context, const DetectorProps &source, const RawProps &raw)
      : ViewProps(context, source, raw), handlerTags(source.handlerTags), virtualChildren(source.virtualChildren) {
    folly::dynamic changed = raw.toDynamic();
    if (!changed.isObject()) return;
    if (changed.count("handlerTags")) handlerTags = changed["handlerTags"];
    if (changed.count("virtualChildren")) virtualChildren = changed["virtualChildren"];
  }
  folly::dynamic handlerTags = folly::dynamic::array();
  folly::dynamic virtualChildren = folly::dynamic::array();
};

extern const char DetectorComponentName[] = "RNGestureHandlerDetector";
extern const char ButtonComponentName[] = "RNGestureHandlerButton";
extern const char RootViewComponentName[] = "RNGestureHandlerRootView";

// The library's RNGestureHandlerDetectorShadowNode: the detector forms a
// view whose frame is its children's bounding box (they keep theirs,
// relative to it), so it adds no box of its own.
class DetectorShadowNode final
    : public ConcreteViewShadowNode<DetectorComponentName, DetectorProps, ViewEventEmitter> {
 public:
  DetectorShadowNode(const ShadowNodeFragment &fragment, const ShadowNodeFamily::Shared &family,
                     ShadowNodeTraits traits)
      : ConcreteViewShadowNode(fragment, family, traits) {
    initialize();
  }
  DetectorShadowNode(const ShadowNode &source, const ShadowNodeFragment &fragment)
      : ConcreteViewShadowNode(source, fragment) {
    previousLayoutMetrics_ = static_cast<const DetectorShadowNode &>(source).getLayoutMetrics();
    initialize();
  }

  void appendChild(const std::shared_ptr<const ShadowNode> &child) override {
    YogaLayoutableShadowNode::appendChild(unflatten(child));
  }
  void replaceChild(const ShadowNode &oldChild, const std::shared_ptr<const ShadowNode> &newChild,
                    size_t suggestedIndex = SIZE_MAX) override {
    YogaLayoutableShadowNode::replaceChild(oldChild, unflatten(newChild), suggestedIndex);
  }

  void layout(LayoutContext context) override {
    const auto &children = getChildren();
    bool anyNew = false;
    for (const auto &child : children) {
      auto yoga = std::static_pointer_cast<const DetectorShadowNode>(child);
      if (yoga->yogaNode_.getHasNewLayout()) anyNew = true;
    }
    YogaLayoutableShadowNode::layout(context);
    if (children.empty()) return;
    if (!anyNew && previousLayoutMetrics_) {
      setLayoutMetrics(*previousLayoutMetrics_);
      return;
    }
    Float minX = std::numeric_limits<Float>::infinity(), minY = minX;
    Float maxX = -minX, maxY = -minX;
    for (const auto &child : children) {
      const auto &f = std::static_pointer_cast<const YogaLayoutableShadowNode>(child)->getLayoutMetrics().frame;
      minX = std::min(minX, f.origin.x);
      minY = std::min(minY, f.origin.y);
      maxX = std::max(maxX, f.origin.x + f.size.width);
      maxY = std::max(maxY, f.origin.y + f.size.height);
    }
    auto metrics = getLayoutMetrics();
    metrics.frame.origin = facebook::react::Point{minX, minY};
    metrics.frame.size = facebook::react::Size{maxX - minX, maxY - minY};
    setLayoutMetrics(metrics);
    for (const auto &child : children) {
      auto yoga = std::static_pointer_cast<const YogaLayoutableShadowNode>(child);
      yoga->ensureUnsealed();
      auto mutableChild = std::const_pointer_cast<YogaLayoutableShadowNode>(yoga);
      auto m = yoga->getLayoutMetrics();
      m.frame.origin.x -= minX;
      m.frame.origin.y -= minY;
      mutableChild->setLayoutMetrics(m);
    }
  }

 private:
  void initialize() {
    ShadowNode::traits_.unset(ShadowNodeTraits::ForceFlattenView);
    const auto &children = getChildren();
    for (size_t i = 0; i < children.size(); i++) replaceChild(*children[i], children[i], i);
  }
  static std::shared_ptr<const ShadowNode> unflatten(const std::shared_ptr<const ShadowNode> &node) {
    auto clone = node->clone({});
    auto access = std::static_pointer_cast<DetectorShadowNode>(clone);
    access->traits_.set(ShadowNodeTraits::FormsView);
    access->traits_.set(ShadowNodeTraits::FormsStackingContext);
    return clone;
  }
  std::optional<LayoutMetrics> previousLayoutMetrics_;
};

using DetectorComponentDescriptor = ConcreteComponentDescriptor<DetectorShadowNode>;

namespace {

// What a detector has attached: handler tag -> view tag (0: itself).
std::map<GtkWidget *, std::map<int, int>> detectorAttachments;

// A detector's handler goes on the detector, or (virtualChildren) on that
// view; a native gesture on the detector's child (a button's, a scroll
// view's), mounted maybe a little later.
void attachToDetector(int tag, GtkWidget *detector, int viewTag, ActionType type, int tries) {
  GestureHandler *h = handlerFor(tag);
  if (!h || !detectorAttachments.count(detector)) return;
  GtkWidget *target = viewTag ? host->viewForTag(viewTag) : detector;
  if (target && !viewTag && h->name() == "NativeViewGestureHandler") target = gtk_widget_get_first_child(detector);
  auto it = attachments.find(tag);
  if (target && it != attachments.end() && it->second->detector == detector && it->second->view == target) return;
  if (target) {
    attach(tag, target, type, detector);
    return;
  }
  if (tries >= 60) return;
  struct Retry {
    int tag;
    GtkWidget *detector;
    int viewTag;
    ActionType type;
    int tries;
  };
  g_timeout_add_full(G_PRIORITY_DEFAULT, 16, [](gpointer data) -> gboolean {
    auto *r = static_cast<Retry *>(data);
    attachToDetector(r->tag, r->detector, r->viewTag, r->type, r->tries + 1);
    return G_SOURCE_REMOVE;
  }, new Retry{tag, detector, viewTag, type, tries}, [](gpointer data) { delete static_cast<Retry *>(data); });
}

void updateDetector(GtkWidget *widget, const ShadowView &, const ShadowView &view) {
  auto props = std::static_pointer_cast<const DetectorProps>(view.props);
  if (!props) return;
  std::map<int, int> want;
  for (auto &t : props->handlerTags) {
    if (t.isNumber()) want[int(t.asDouble())] = 0;
  }
  for (auto &child : props->virtualChildren) {
    if (!child.isObject() || !child.count("viewTag") || !child.count("handlerTags")) continue;
    int viewTag = int(child["viewTag"].asDouble());
    for (auto &t : child["handlerTags"]) {
      if (t.isNumber()) want[int(t.asDouble())] = viewTag;
    }
  }
  auto &have = detectorAttachments[widget];
  if (have.empty()) {
    // Forget the detector's attachments when it goes.
    g_object_weak_ref(G_OBJECT(widget), [](gpointer, GObject *gone) {
      auto *w = reinterpret_cast<GtkWidget *>(gone);
      for (auto &[tag, viewTag] : detectorAttachments[w]) {
        auto it = attachments.find(tag);
        if (it == attachments.end()) continue;
        Attachment &a = *it->second;
        if (a.detector && a.detector != w) continue;
        // `w` is being disposed: its weak pointers are going anyway.
        if (a.view == w) a.view = nullptr;
        if (a.detector == w) a.detector = nullptr;
        detach(tag);
      }
      detectorAttachments.erase(w);
      for (auto it = wanted.begin(); it != wanted.end();) {
        it = it->second.detector == w ? wanted.erase(it) : std::next(it);
      }
    }, nullptr);
  }
  for (auto &[tag, viewTag] : have) {
    if (want.count(tag) && want[tag] == viewTag) continue;
    auto it = attachments.find(tag);
    if (it != attachments.end() && it->second->detector == widget) detach(tag);
  }
  for (auto it = wanted.begin(); it != wanted.end();) {
    it = it->second.detector == widget ? wanted.erase(it) : std::next(it);
  }
  for (auto &[tag, viewTag] : want) {
    ActionType type = viewTag ? ACTION_VIRTUAL_DETECTOR : ACTION_NATIVE_DETECTOR;
    wanted.emplace(tag, Wanted{widget, viewTag, type});
    attachToDetector(tag, widget, viewTag, type, 0);
  }
  have = want;
}

// RNGestureHandlerButton's (and RNGestureHandlerRootView's) props, as JS
// sends them, merged.
class RawProps2 final : public ViewProps {
 public:
  RawProps2() = default;
  RawProps2(const PropsParserContext &context, const RawProps2 &source, const RawProps &raw)
      : ViewProps(context, source, raw), raw(source.raw) {
    folly::dynamic changed = raw.toDynamic();
    if (changed.isObject()) {
      for (auto &[k, v] : changed.items()) this->raw[k] = v;
    }
  }
  folly::dynamic raw = folly::dynamic::object();
};

void updateButton(GtkWidget *widget, const ShadowView &, const ShadowView &view) {
  auto props = std::static_pointer_cast<const RawProps2>(view.props);
  if (!props) return;
  Button *b = buttonFor(widget);
  if (!b) {
    auto owned = std::make_unique<Button>();
    b = owned.get();
    b->view = widget;
    buttons[widget] = std::move(owned);
    g_object_weak_ref(G_OBJECT(widget), [](gpointer, GObject *gone) {
      auto *w = reinterpret_cast<GtkWidget *>(gone);
      buttons.erase(w);
      // Its own gesture (Touchable's) goes with it.
      for (auto it = attachments.begin(); it != attachments.end();) {
        if (it->second->view != w || it->second->handler->actionType() != ACTION_NONE) {
          ++it;
          continue;
        }
        int tag = it->first;
        it->second->view = nullptr;
        ++it;
        dropGestureHandler(tag);
      }
    }, nullptr);
  }
  // The feedback's own values come back here; keep the style's.
  folly::dynamic base = props->raw;
  for (const char *key : {"opacity", "transform", "backgroundColor"}) {
    bool fromFeedback = b->applied.count(key) && base.count(key) && base[key] == b->applied[key];
    if (!fromFeedback) continue;
    if (b->props.count(key)) base[key] = b->props[key];
    else base.erase(key);
  }
  b->props = base;
  b->emitter = view.eventEmitter;
  // A button with a handlerTag (Touchable) runs its own native gesture
  // under that tag, as on Android; JS can relate other gestures to it.
  auto tag = numberIn(props->raw, "handlerTag");
  if (!tag) return;
  folly::dynamic config = folly::dynamic::object("enabled", boolIn(props->raw, "enabled").value_or(true))(
      "shouldCancelWhenOutside", boolIn(props->raw, "cancelOnLeave").value_or(true))(
      "hasLongPressHandler", boolIn(props->raw, "hasLongPressHandler").value_or(false))(
      "longPressDuration", numberIn(props->raw, "longPressDuration").value_or(-1));
  if (props->raw.count("gestureHitSlop") && props->raw["gestureHitSlop"].isObject()) {
    const auto &h = props->raw["gestureHitSlop"];
    auto edge = [&](const char *k) { return h.count(k) && h[k].isNumber() ? h[k] : folly::dynamic(); };
    config["hitSlop"] = folly::dynamic::array(edge("left"), edge("top"), edge("right"), edge("bottom"), nullptr, nullptr);
  }
  if (props->raw.count("gestureTestID")) config["testID"] = props->raw["gestureTestID"];
  int t = int(*tag);
  GestureHandler *h = handlerFor(t);
  if (!h) {
    createGestureHandler("NativeViewGestureHandler", t, config);
    h = handlerFor(t);
  } else {
    h->updateGestureConfig(config);
  }
  auto it = attachments.find(t);
  if (h && (it == attachments.end() || it->second->view != widget)) attach(t, widget, ACTION_NONE, nullptr);
}

}  // namespace

}  // namespace rngtk_gh

std::shared_ptr<const rngtk::Package> rngtk_gesture_handler_package() {
  using namespace rngtk_gh;
  auto package = std::make_shared<rngtk::Package>();
  package->name = "@curiosity26/react-native-gtk4-gesture-handler";
  package->turboModules.push_back(
      [](const std::string &name, const std::shared_ptr<CallInvoker> &js) -> std::shared_ptr<TurboModule> {
        return name == "RNGestureHandlerModule" ? std::make_shared<Module>(js) : nullptr;
      });
  rngtk::NativeComponent detector;
  detector.descriptor = concreteComponentDescriptorProvider<DetectorComponentDescriptor>();
  detector.update = updateDetector;
  package->components.push_back(detector);
  using ButtonShadowNode = ConcreteViewShadowNode<ButtonComponentName, RawProps2, ViewEventEmitter>;
  using RootShadowNode = ConcreteViewShadowNode<RootViewComponentName, RawProps2, ViewEventEmitter>;
  rngtk::NativeComponent button;
  button.descriptor = concreteComponentDescriptorProvider<ConcreteComponentDescriptor<ButtonShadowNode>>();
  button.update = updateButton;
  package->components.push_back(button);
  rngtk::NativeComponent root;
  root.descriptor = concreteComponentDescriptorProvider<ConcreteComponentDescriptor<RootShadowNode>>();
  package->components.push_back(root);
  package->setUp = [](rngtk::Host &h) {
    host = &h;
    h.addPointerObserver(observe);
    // A ScrollView's native gesture activates when the user scrolls it.
    h.addScrollObserver([](GtkWidget *scrollView) {
      std::vector<GestureHandler *> list;
      for (auto &[tag, a] : attachments) {
        if (a->view == scrollView) list.push_back(a->handler);
      }
      for (GestureHandler *handler : list) handler->onScroll();
    });
  };
  return package;
}
