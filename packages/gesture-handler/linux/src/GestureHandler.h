// react-native-gesture-handler's recognizers on Linux, ported from the
// library's web implementation (src/web: its TypeScript port of the
// Android orchestrator, on pointer events): the handler state machine, the
// pointer tracker with its velocity estimate, the handlers (Handlers.cc)
// and the orchestrator that settles them against each other
// (Orchestrator.h). Events come from the host's pointer input
// (rngtk::Host::addPointerObserver, GestureHandlerModule.cc), main thread.
#pragma once

#include <folly/dynamic.h>
#include <gtk/gtk.h>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rngtk_gh {

class Orchestrator;

// The library's State and PointerType values.
enum State { UNDETERMINED = 0, FAILED = 1, BEGAN = 2, CANCELLED = 3, ACTIVE = 4, END = 5 };
enum PointerType { POINTER_TOUCH = 0, POINTER_STYLUS = 1, POINTER_MOUSE = 2, POINTER_KEY = 3, POINTER_OTHER = 4 };
// ActionType: how a handler's events reach JS.
enum ActionType {
  ACTION_NONE = 0,
  ACTION_REANIMATED_WORKLET = 1,
  ACTION_NATIVE_ANIMATED_EVENT = 2,
  ACTION_JS_FUNCTION_OLD_API = 3,
  ACTION_JS_FUNCTION_NEW_API = 4,
  ACTION_NATIVE_DETECTOR = 5,
  ACTION_VIRTUAL_DETECTOR = 6,
};
// TouchEventType.
enum TouchEventType { TOUCHES_UNDETERMINED = 0, TOUCHES_DOWN = 1, TOUCHES_MOVE = 2, TOUCHES_UP = 3, TOUCHES_CANCEL = 4 };
// MouseButton flags.
enum MouseButton { MOUSE_LEFT = 1, MOUSE_RIGHT = 2, MOUSE_MIDDLE = 4, MOUSE_BUTTON_4 = 8, MOUSE_BUTTON_5 = 16 };

enum class EventType { Down, AdditionalPointerDown, Up, AdditionalPointerUp, Move, Enter, Leave, Cancel };

struct Point {
  double x = 0, y = 0;
};

// A pointer event for one handler: x/y in the surface root's coordinates
// (absolute), offsetX/Y in the handler's view.
struct AdaptedEvent {
  double x = 0, y = 0;
  double offsetX = 0, offsetY = 0;
  int pointerId = 0;
  EventType eventType = EventType::Move;
  PointerType pointerType = POINTER_MOUSE;
  double time = 0;  // ms
  int button = 0;   // MouseButton flag, 0 for none
  double wheelDeltaY = 0;
};

class VelocityTracker {
 public:
  void add(const AdaptedEvent &event);
  Point velocity() const;  // px/s
  void reset() { samples_.clear(); }

 private:
  std::vector<AdaptedEvent> samples_;  // newest last, at most kHistory
};

class PointerTracker {
 public:
  struct Element {
    Point absolute, relative;
    double timestamp = 0;
    double velocityX = 0, velocityY = 0;
  };
  void addToTracker(const AdaptedEvent &event);
  void removeFromTracker(int pointerId);
  void track(const AdaptedEvent &event);
  std::optional<Point> velocity(int pointerId) const;
  std::optional<Point> lastAbsoluteCoords(std::optional<int> pointerId = std::nullopt) const;
  std::optional<Point> lastRelativeCoords(std::optional<int> pointerId = std::nullopt) const;
  Point absoluteCoordsAverage() const;
  Point relativeCoordsAverage() const;
  int mappedTouchEventId(int pointerId) const;
  void resetTracker();
  int trackedPointersCount() const { return int(pointers_.size()); }
  std::vector<int> trackedPointersIDs() const;
  const std::map<int, Element> &trackedPointers() const { return pointers_; }
  static bool shareCommonPointers(const std::vector<int> &a, const std::vector<int> &b);

 private:
  VelocityTracker velocity_;
  std::map<int, Element> pointers_;
  std::vector<int> touchIds_ = std::vector<int>(20, -1);  // slot -> pointer id
  int lastMovedPointerId_ = -1;
  Point cachedAbsolute_, cachedRelative_;
};

// What a handler needs from the rest of the port: its view's geometry and
// a way to reach JS.
struct HandlerDelegate {
  virtual ~HandlerDelegate() = default;
  // The view's bounds in the root's coordinates.
  virtual graphene_rect_t viewBounds() const = 0;
  virtual void sendEvent(class GestureHandler &handler, folly::dynamic event, const char *kind) = 0;
  // A timer on the main loop (ms); returns an id for cancelTimer.
  virtual guint startTimer(int ms, std::function<void()> fn) = 0;
  virtual void cancelTimer(guint id) = 0;
  // The JS responder (Pressable, ScrollView) loses its touches.
  virtual void cancelJSResponder() = 0;
  // A button's native gesture: whether it shows pressed, and (a button the
  // library manages itself: Touchable) its events (onButtonPress...).
  virtual void buttonPressed(bool) {}
  virtual void buttonEvent(const char * /*name*/, folly::dynamic /*payload*/) {}
};

class GestureHandler {
 public:
  GestureHandler(int tag, std::string name, Orchestrator &orchestrator)
      : tag_(tag), name_(std::move(name)), orchestrator_(orchestrator) {}
  virtual ~GestureHandler();

  int tag() const { return tag_; }
  const std::string &name() const { return name_; }
  State state() const { return state_; }
  PointerTracker &tracker() { return tracker_; }
  const PointerTracker &tracker() const { return tracker_; }
  PointerType pointerType() const { return pointerType_; }
  bool enabled() const { return enabled_.value_or(true); }
  bool attached() const { return delegate_ != nullptr; }
  ActionType actionType() const { return actionType_; }
  virtual bool isContinuous() const { return false; }
  virtual bool isButton() const { return false; }
  bool shouldCancelWhenOutside() const { return shouldCancelWhenOutside_; }
  bool dispatchesAnimatedEvents() const { return forAnimated_; }
  bool dispatchesReanimatedEvents() const { return forReanimated_; }
  bool cancelsJSResponder() const { return cancelsJSResponder_; }
  HandlerDelegate *delegate() const { return delegate_; }
  // The view it's attached to (a GtkWidget), for overlap checks.
  void *view = nullptr;
  // The detector it reports through (NATIVE/VIRTUAL_DETECTOR), if any.
  void *hostDetector = nullptr;

  // Orchestrator's.
  bool active = false;
  bool awaiting = false;
  long activationIndex = 0;
  bool shouldResetProgress = false;

  void attach(HandlerDelegate *delegate, ActionType actionType);
  void detach();

  void begin();
  virtual void activate(bool force = false);
  void end();
  void fail(bool sendIfDisabled = false);
  void cancel(bool sendIfDisabled = false);
  void reset();
  // The JS state manager (_setGestureState*): manual activation.
  void setStateFromJS(State state);

  bool shouldWaitForHandlerFailure(GestureHandler &other);
  bool shouldRequireToWaitForFailure(GestureHandler &other);
  virtual bool shouldRecognizeSimultaneously(GestureHandler &other);
  virtual bool shouldBeCancelledByOther(GestureHandler &other);
  virtual bool shouldBeginWithRecordedHandlers(const std::vector<GestureHandler *> &) { return true; }

  void sendEvent(State newState, State oldState);
  std::vector<int> trackedPointersIDs() const { return tracker_.trackedPointersIDs(); }
  bool isPointerInBounds(Point p) const;

  void setGestureConfig(const folly::dynamic &config);
  virtual void updateGestureConfig(const folly::dynamic &config);
  bool isButtonInConfig(int button) const;

  // Input, from the event manager.
  virtual void onPointerDown(const AdaptedEvent &event);
  virtual void onPointerAdd(const AdaptedEvent &event);
  virtual void onPointerUp(const AdaptedEvent &event);
  virtual void onPointerRemove(const AdaptedEvent &event);
  virtual void onPointerMove(const AdaptedEvent &event);
  virtual void onPointerLeave(const AdaptedEvent &event);
  virtual void onPointerEnter(const AdaptedEvent &event);
  virtual void onPointerCancel(const AdaptedEvent &event);
  virtual void onPointerOutOfBounds(const AdaptedEvent &event);
  virtual void onPointerMoveOver(const AdaptedEvent &) {}
  virtual void onPointerMoveOut(const AdaptedEvent &) {}
  virtual void onWheel(const AdaptedEvent &) {}
  // A touchpad pinch at `focus` (root coordinates): Begin, Update (scale
  // since it began, rotation since the last one), End or Cancel.
  enum class PinchPhase { Begin, Update, End, Cancel };
  virtual void onTouchpadPinch(PinchPhase, Point, double /*scale*/, double /*angleDelta*/, double /*time*/) {}
  // The view it's on scrolled (a ScrollView's native gesture).
  virtual void onScroll() {}
  // What kind of view it's attached to: "Button" (an RNGestureHandlerButton),
  // "ScrollView", "Switch", or "".
  std::string viewRole;

  // Relations (configureRelations): handler tags.
  std::vector<int> waitFor, simultaneousWith, blocks;

 protected:
  virtual void onCancel() {}
  virtual void onReset() {}
  virtual void resetProgress() {}
  virtual void onStateChange(State, State) {}
  virtual void resetConfig();
  // The handler's event data (handlerData on the new API).
  virtual folly::dynamic eventData();

  void moveToState(State newState, bool sendIfDisabled = false);
  void tryToSendMoveEvent(bool out, const AdaptedEvent &event);
  void tryToSendTouchEvent(const AdaptedEvent &event);
  void sendTouchEvent(const AdaptedEvent &event);
  void cancelTouches();
  bool checkHitSlop() const;
  guint startTimer(int ms, std::function<void()> fn);
  void cancelTimer(guint &id);
  static double now();

  int tag_;
  std::string name_;
  Orchestrator &orchestrator_;
  HandlerDelegate *delegate_ = nullptr;
  ActionType actionType_ = ACTION_NONE;
  State state_ = UNDETERMINED;
  std::optional<State> lastSentState_;
  PointerTracker tracker_;
  PointerType pointerType_ = POINTER_MOUSE;
  std::optional<bool> enabled_;
  bool shouldCancelWhenOutside_ = false;
  bool manualActivation_ = false;
  bool needsPointerData_ = false;
  bool forAnimated_ = false, forReanimated_ = false;
  bool cancelsJSResponder_ = true;
  int mouseButton_ = 0;
  // left, top, right, bottom, width, height; NaN: not set.
  std::optional<std::array<double, 6>> hitSlop_;
  std::string testID_;
};

std::unique_ptr<GestureHandler> createHandler(const std::string &name, int tag, Orchestrator &orchestrator);

// Config helpers.
std::optional<double> numberIn(const folly::dynamic &config, const char *key);
std::optional<bool> boolIn(const folly::dynamic &config, const char *key);

}  // namespace rngtk_gh
