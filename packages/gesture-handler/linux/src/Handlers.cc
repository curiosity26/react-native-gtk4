// The gesture handlers, as the library's web ones (src/web/handlers): Tap,
// LongPress, Pan, Fling, Hover and Manual. Distances are in the root's
// logical pixels (React Native's points), times in ms.
#include "GestureHandler.h"

#include "Orchestrator.h"

#include <cmath>
#include <limits>

namespace rngtk_gh {

namespace {
constexpr double kMinInt = -9007199254740991.0;  // Number.MIN_SAFE_INTEGER
constexpr double kMaxInt = 9007199254740991.0;   // Number.MAX_SAFE_INTEGER
constexpr double kTouchSlop = 15;                // DEFAULT_TOUCH_SLOP

// ---- Tap ----------------------------------------------------------------------

class TapHandler final : public GestureHandler {
 public:
  using GestureHandler::GestureHandler;

  void updateGestureConfig(const folly::dynamic &c) override {
    GestureHandler::updateGestureConfig(c);
    if (auto v = numberIn(c, "numberOfTaps")) numberOfTaps_ = int(*v);
    if (auto v = numberIn(c, "maxDurationMs")) maxDurationMs_ = *v;
    if (auto v = numberIn(c, "maxDelayMs")) maxDelayMs_ = *v;
    if (auto v = numberIn(c, "maxDeltaX")) maxDeltaX_ = *v;
    if (auto v = numberIn(c, "maxDeltaY")) maxDeltaY_ = *v;
    if (auto v = numberIn(c, "maxDist")) maxDistSq_ = *v * *v;
    if (auto v = numberIn(c, "minPointers")) minPointers_ = int(*v);
  }

  void activate(bool force = false) override {
    GestureHandler::activate(force);
    end();
  }

 protected:
  void resetConfig() override {
    GestureHandler::resetConfig();
    maxDeltaX_ = maxDeltaY_ = maxDistSq_ = kMinInt;
    maxDurationMs_ = 500;
    maxDelayMs_ = 500;
    numberOfTaps_ = 1;
    minPointers_ = 1;
  }

  void onPointerDown(const AdaptedEvent &e) override {
    if (!isButtonInConfig(e.button)) return;
    tracker_.addToTracker(e);
    GestureHandler::onPointerDown(e);
    trySettingPosition(e);
    startX_ = lastX_ = e.x;
    startY_ = lastY_ = e.y;
    updateState(e);
  }
  void onPointerAdd(const AdaptedEvent &e) override {
    GestureHandler::onPointerAdd(e);
    tracker_.addToTracker(e);
    trySettingPosition(e);
    offsetX_ += lastX_ - startX_;
    offsetY_ += lastY_ - startY_;
    updateLastCoords();
    startX_ = lastX_;
    startY_ = lastY_;
    updateState(e);
  }
  void onPointerUp(const AdaptedEvent &e) override {
    GestureHandler::onPointerUp(e);
    updateLastCoords();
    tracker_.removeFromTracker(e.pointerId);
    updateState(e);
  }
  void onPointerRemove(const AdaptedEvent &e) override {
    GestureHandler::onPointerRemove(e);
    tracker_.removeFromTracker(e.pointerId);
    offsetX_ += lastX_ - startX_;
    offsetY_ += lastY_ - startY_;
    updateLastCoords();
    startX_ = lastX_;
    startY_ = lastY_;
    updateState(e);
  }
  void onPointerMove(const AdaptedEvent &e) override {
    trySettingPosition(e);
    tracker_.track(e);
    updateLastCoords();
    updateState(e);
    GestureHandler::onPointerMove(e);
  }
  void onPointerOutOfBounds(const AdaptedEvent &e) override {
    trySettingPosition(e);
    tracker_.track(e);
    updateLastCoords();
    updateState(e);
    GestureHandler::onPointerOutOfBounds(e);
  }
  void onCancel() override {
    resetProgress();
    clearTimeouts();
  }
  void resetProgress() override {
    clearTimeouts();
    tapsSoFar_ = 0;
    currentMaxPointers_ = 0;
  }

 private:
  void clearTimeouts() {
    cancelTimer(waitTimer_);
    cancelTimer(delayTimer_);
  }
  void startTap() {
    clearTimeouts();
    waitTimer_ = startTimer(int(maxDurationMs_), [this] {
      waitTimer_ = 0;
      fail();
    });
  }
  void endTap() {
    clearTimeouts();
    if (++tapsSoFar_ == numberOfTaps_ && currentMaxPointers_ >= minPointers_) {
      activate();
    } else {
      delayTimer_ = startTimer(int(maxDelayMs_), [this] {
        delayTimer_ = 0;
        fail();
      });
    }
  }
  void updateLastCoords() {
    Point p = tracker_.absoluteCoordsAverage();
    lastX_ = p.x;
    lastY_ = p.y;
  }
  void updateState(const AdaptedEvent &e) {
    currentMaxPointers_ = std::max(currentMaxPointers_, tracker_.trackedPointersCount());
    if (shouldFail()) {
      fail();
      return;
    }
    switch (state_) {
      case UNDETERMINED:
        if (e.eventType == EventType::Down) begin();
        startTap();
        break;
      case BEGAN:
        if (e.eventType == EventType::Up) endTap();
        if (e.eventType == EventType::Down) startTap();
        break;
      default: break;
    }
  }
  void trySettingPosition(const AdaptedEvent &e) {
    if (state_ != UNDETERMINED) return;
    offsetX_ = offsetY_ = 0;
    startX_ = e.x;
    startY_ = e.y;
  }
  bool shouldFail() const {
    double dx = lastX_ - startX_ + offsetX_;
    if (maxDeltaX_ != kMinInt && std::abs(dx) > maxDeltaX_) return true;
    double dy = lastY_ - startY_ + offsetY_;
    if (maxDeltaY_ != kMinInt && std::abs(dy) > maxDeltaY_) return true;
    return maxDistSq_ != kMinInt && dx * dx + dy * dy > maxDistSq_;
  }

  double maxDeltaX_ = kMinInt, maxDeltaY_ = kMinInt, maxDistSq_ = kMinInt;
  double maxDurationMs_ = 500, maxDelayMs_ = 500;
  int numberOfTaps_ = 1, minPointers_ = 1, currentMaxPointers_ = 1;
  double startX_ = 0, startY_ = 0, offsetX_ = 0, offsetY_ = 0, lastX_ = 0, lastY_ = 0;
  guint waitTimer_ = 0, delayTimer_ = 0;
  int tapsSoFar_ = 0;
};

// ---- LongPress ------------------------------------------------------------------

class LongPressHandler final : public GestureHandler {
 public:
  using GestureHandler::GestureHandler;

  void updateGestureConfig(const folly::dynamic &c) override {
    GestureHandler::updateGestureConfig(c);
    if (auto v = numberIn(c, "minDurationMs")) minDurationMs_ = *v;
    if (auto v = numberIn(c, "maxDist")) maxDistSq_ = *v * *v;
    if (auto v = numberIn(c, "numberOfPointers")) numberOfPointers_ = int(*v);
  }

 protected:
  folly::dynamic eventData() override {
    folly::dynamic d = GestureHandler::eventData();
    d["duration"] = int(now() - startTime_);
    return d;
  }
  void resetConfig() override {
    GestureHandler::resetConfig();
    minDurationMs_ = 500;
    maxDistSq_ = kDefaultMaxDistSq;
    numberOfPointers_ = 1;
  }
  void onStateChange(State, State) override { cancelTimer(activationTimer_); }

  void onPointerDown(const AdaptedEvent &e) override {
    if (!isButtonInConfig(e.button)) return;
    tracker_.addToTracker(e);
    GestureHandler::onPointerDown(e);
    startX_ = e.x;
    startY_ = e.y;
    tryBegin();
    tryActivate();
  }
  void onPointerAdd(const AdaptedEvent &e) override {
    GestureHandler::onPointerAdd(e);
    tracker_.addToTracker(e);
    if (tracker_.trackedPointersCount() > numberOfPointers_) {
      fail();
      return;
    }
    Point p = tracker_.absoluteCoordsAverage();
    startX_ = p.x;
    startY_ = p.y;
    tryActivate();
  }
  void onPointerMove(const AdaptedEvent &e) override {
    GestureHandler::onPointerMove(e);
    tracker_.track(e);
    checkDistanceFail();
  }
  void onPointerOutOfBounds(const AdaptedEvent &e) override {
    GestureHandler::onPointerOutOfBounds(e);
    tracker_.track(e);
    checkDistanceFail();
  }
  void onPointerUp(const AdaptedEvent &e) override {
    GestureHandler::onPointerUp(e);
    tracker_.removeFromTracker(e.pointerId);
    if (state_ == ACTIVE) end();
    else fail();
  }
  void onPointerRemove(const AdaptedEvent &e) override {
    GestureHandler::onPointerRemove(e);
    tracker_.removeFromTracker(e.pointerId);
    if (tracker_.trackedPointersCount() < numberOfPointers_ && state_ != ACTIVE) fail();
  }

 private:
  // The web's DEFAULT_MAX_DIST_DP * SCALING_FACTOR, a squared distance.
  static constexpr double kDefaultMaxDistSq = 10 * 10;

  void tryBegin() {
    if (state_ != UNDETERMINED) return;
    startTime_ = now();
    begin();
  }
  void tryActivate() {
    if (tracker_.trackedPointersCount() != numberOfPointers_) return;
    if (minDurationMs_ > 0) {
      cancelTimer(activationTimer_);
      activationTimer_ = startTimer(int(minDurationMs_), [this] {
        activationTimer_ = 0;
        activate();
      });
    } else if (minDurationMs_ == 0) {
      activate();
    }
  }
  void checkDistanceFail() {
    Point p = tracker_.absoluteCoordsAverage();
    double dx = p.x - startX_, dy = p.y - startY_;
    if (dx * dx + dy * dy <= maxDistSq_) return;
    if (state_ == ACTIVE) cancel();
    else fail();
  }

  double minDurationMs_ = 500, maxDistSq_ = kDefaultMaxDistSq;
  int numberOfPointers_ = 1;
  double startX_ = 0, startY_ = 0, startTime_ = 0;
  guint activationTimer_ = 0;
};

// ---- Pan ------------------------------------------------------------------------

class PanHandler final : public GestureHandler {
 public:
  using GestureHandler::GestureHandler;
  bool isContinuous() const override { return true; }

  void updateGestureConfig(const folly::dynamic &c) override {
    GestureHandler::updateGestureConfig(c);
    if (auto v = numberIn(c, "minPointers")) minPointers_ = int(*v);
    if (auto v = numberIn(c, "maxPointers")) maxPointers_ = int(*v);
    auto custom = [&](const char *key, double &out, bool square = false) {
      if (auto v = numberIn(c, key)) {
        out = square ? *v * *v : *v;
        hasCustomActivationCriteria_ = true;
      }
    };
    custom("minVelocity", minVelocitySq_, true);
    custom("minVelocityX", minVelocityX_);
    custom("minVelocityY", minVelocityY_);
    if (auto v = numberIn(c, "activateAfterLongPress")) activateAfterLongPress_ = *v;
    custom("activeOffsetXStart", activeOffsetXStart_);
    custom("activeOffsetXEnd", activeOffsetXEnd_);
    custom("failOffsetXStart", failOffsetXStart_);
    custom("failOffsetXEnd", failOffsetXEnd_);
    custom("activeOffsetYStart", activeOffsetYStart_);
    custom("activeOffsetYEnd", activeOffsetYEnd_);
    custom("failOffsetYStart", failOffsetYStart_);
    custom("failOffsetYEnd", failOffsetYEnd_);
    if (auto v = boolIn(c, "enableTrackpadTwoFingerGesture")) enableTrackpad_ = *v;
    if (auto v = numberIn(c, "minDist")) {
      minDist_ = *v;
      minDistSq_ = *v * *v;
    } else if (!minDist_ && hasCustomActivationCriteria_) {
      minDistSq_ = kMaxInt;
    }
  }

  void activate(bool force = false) override {
    if (state_ != ACTIVE) resetProgress();
    GestureHandler::activate(force);
  }

 protected:
  folly::dynamic eventData() override {
    folly::dynamic d = GestureHandler::eventData();
    d["translationX"] = translationX();
    d["translationY"] = translationY();
    d["velocityX"] = velocityX_;
    d["velocityY"] = velocityY_;
    return d;
  }
  void resetConfig() override {
    GestureHandler::resetConfig();
    activeOffsetXStart_ = -kMaxInt;
    activeOffsetXEnd_ = kMinInt;
    failOffsetXStart_ = kMinInt;
    failOffsetXEnd_ = kMaxInt;
    activeOffsetYStart_ = kMaxInt;
    activeOffsetYEnd_ = kMinInt;
    failOffsetYStart_ = kMinInt;
    failOffsetYEnd_ = kMaxInt;
    minVelocityX_ = minVelocityY_ = minVelocitySq_ = kMaxInt;
    minDist_.reset();
    minDistSq_ = kTouchSlop * kTouchSlop;
    minPointers_ = 1;
    maxPointers_ = 10;
    activateAfterLongPress_ = 0;
    enableTrackpad_ = false;
    hasCustomActivationCriteria_ = false;
  }

  void onPointerDown(const AdaptedEvent &e) override {
    if (!isButtonInConfig(e.button)) return;
    tracker_.addToTracker(e);
    GestureHandler::onPointerDown(e);
    updateLastCoords();
    startX_ = lastX_;
    startY_ = lastY_;
    tryBegin(e);
    checkBegan();
  }
  void onPointerAdd(const AdaptedEvent &e) override {
    tracker_.addToTracker(e);
    GestureHandler::onPointerAdd(e);
    tryBegin(e);
    offsetX_ += lastX_ - startX_;
    offsetY_ += lastY_ - startY_;
    updateLastCoords();
    startX_ = lastX_;
    startY_ = lastY_;
    if (tracker_.trackedPointersCount() > maxPointers_) {
      if (state_ == ACTIVE) cancel();
      else fail();
    } else {
      checkBegan();
    }
  }
  void onPointerUp(const AdaptedEvent &e) override {
    GestureHandler::onPointerUp(e);
    if (state_ == ACTIVE) {
      Point p = tracker_.absoluteCoordsAverage();
      lastX_ = p.x;
      lastY_ = p.y;
    }
    tracker_.removeFromTracker(e.pointerId);
    if (tracker_.trackedPointersCount() == 0) cancelTimer(activationTimer_);
    if (state_ == ACTIVE) {
      end();
    } else {
      resetProgress();
      fail();
    }
  }
  void onPointerRemove(const AdaptedEvent &e) override {
    GestureHandler::onPointerRemove(e);
    tracker_.removeFromTracker(e.pointerId);
    offsetX_ += lastX_ - startX_;
    offsetY_ += lastY_ - startY_;
    updateLastCoords();
    startX_ = lastX_;
    startY_ = lastY_;
    if (!(state_ == ACTIVE && tracker_.trackedPointersCount() < minPointers_)) checkBegan();
  }
  void onPointerMove(const AdaptedEvent &e) override {
    tracker_.track(e);
    updateLastCoords();
    updateVelocity(e.pointerId);
    checkBegan();
    GestureHandler::onPointerMove(e);
  }
  void onPointerOutOfBounds(const AdaptedEvent &e) override {
    if (shouldCancelWhenOutside_) return;
    tracker_.track(e);
    updateLastCoords();
    updateVelocity(e.pointerId);
    checkBegan();
    if (state_ == ACTIVE) GestureHandler::onPointerOutOfBounds(e);
  }
  // Two-finger scrolling on a touchpad (enableTrackpadTwoFingerGesture):
  // smooth scroll events, which a mouse wheel's notches aren't.
  void onWheel(const AdaptedEvent &e) override {
    if (wheelIsMouse_ || !enableTrackpad_) return;
    if (state_ == UNDETERMINED) {
      wheelIsMouse_ = std::fmod(e.wheelDeltaY, 120) == 0 && e.wheelDeltaY != 0;
      if (wheelIsMouse_) {
        scheduleWheelEnd();
        return;
      }
      orchestrator_.recordHandlerIfNotPresent(this);
      tracker_.addToTracker(e);
      updateLastCoords();
      startX_ = lastX_;
      startY_ = lastY_;
      begin();
      activate();
    }
    tracker_.track(e);
    updateLastCoords();
    updateVelocity(e.pointerId);
    tryToSendMoveEvent(false, e);
    scheduleWheelEnd();
  }
  void onCancel() override { cancelTimer(activationTimer_); }
  void onReset() override { cancelTimer(activationTimer_); }
  void resetProgress() override {
    if (state_ == ACTIVE) return;
    startX_ = lastX_;
    startY_ = lastY_;
  }

 private:
  double translationX() const { return lastX_ - startX_ + offsetX_; }
  double translationY() const { return lastY_ - startY_ + offsetY_; }
  void updateLastCoords() {
    Point p = tracker_.absoluteCoordsAverage();
    lastX_ = p.x;
    lastY_ = p.y;
  }
  void updateVelocity(int pointerId) {
    auto v = tracker_.velocity(pointerId);
    velocityX_ = v ? v->x : 0;
    velocityY_ = v ? v->y : 0;
  }
  void scheduleWheelEnd() {
    cancelTimer(wheelEndTimer_);
    wheelEndTimer_ = startTimer(30, [this] {
      wheelEndTimer_ = 0;
      if (state_ == ACTIVE) {
        end();
        reset();
      }
      wheelIsMouse_ = false;
    });
  }
  bool shouldActivate() const {
    double dx = translationX();
    if (activeOffsetXStart_ != kMaxInt && dx < activeOffsetXStart_) return true;
    if (activeOffsetXEnd_ != kMinInt && dx > activeOffsetXEnd_) return true;
    double dy = translationY();
    if (activeOffsetYStart_ != kMaxInt && dy < activeOffsetYStart_) return true;
    if (activeOffsetYEnd_ != kMinInt && dy > activeOffsetYEnd_) return true;
    if (minDistSq_ != kMaxInt && dx * dx + dy * dy >= minDistSq_) return true;
    if (minVelocityX_ != kMaxInt && std::abs(velocityX_) >= std::abs(minVelocityX_)) return true;
    if (minVelocityY_ != kMaxInt && std::abs(velocityY_) >= std::abs(minVelocityY_)) return true;
    return minVelocitySq_ != kMaxInt &&
           velocityX_ * velocityX_ + velocityY_ * velocityY_ >= minVelocitySq_;
  }
  bool shouldFail() {
    double dx = translationX(), dy = translationY();
    if (activateAfterLongPress_ > 0 && dx * dx + dy * dy > kTouchSlop * kTouchSlop) {
      cancelTimer(activationTimer_);
      return true;
    }
    if (failOffsetXStart_ != kMinInt && dx < failOffsetXStart_) return true;
    if (failOffsetXEnd_ != kMaxInt && dx > failOffsetXEnd_) return true;
    if (failOffsetYStart_ != kMinInt && dy < failOffsetYStart_) return true;
    return failOffsetYEnd_ != kMaxInt && dy > failOffsetYEnd_;
  }
  void tryBegin(const AdaptedEvent &e) {
    if (state_ == UNDETERMINED && tracker_.trackedPointersCount() >= minPointers_) {
      resetProgress();
      offsetX_ = offsetY_ = velocityX_ = velocityY_ = 0;
      begin();
      if (activateAfterLongPress_ > 0) {
        activationTimer_ = startTimer(int(activateAfterLongPress_), [this] {
          activationTimer_ = 0;
          activate();
        });
      }
    } else {
      updateVelocity(e.pointerId);
    }
  }
  void checkBegan() {
    if (state_ != BEGAN) return;
    if (shouldFail()) fail();
    else if (shouldActivate()) activate();
  }

  double velocityX_ = 0, velocityY_ = 0;
  std::optional<double> minDist_;
  double minDistSq_ = kTouchSlop * kTouchSlop;
  double activeOffsetXStart_ = -kMaxInt, activeOffsetXEnd_ = kMinInt;
  double failOffsetXStart_ = kMinInt, failOffsetXEnd_ = kMaxInt;
  double activeOffsetYStart_ = kMaxInt, activeOffsetYEnd_ = kMinInt;
  double failOffsetYStart_ = kMinInt, failOffsetYEnd_ = kMaxInt;
  double minVelocityX_ = kMaxInt, minVelocityY_ = kMaxInt, minVelocitySq_ = kMaxInt;
  int minPointers_ = 1, maxPointers_ = 10;
  double startX_ = 0, startY_ = 0, offsetX_ = 0, offsetY_ = 0, lastX_ = 0, lastY_ = 0;
  double activateAfterLongPress_ = 0;
  guint activationTimer_ = 0, wheelEndTimer_ = 0;
  bool enableTrackpad_ = false, wheelIsMouse_ = false;
  bool hasCustomActivationCriteria_ = false;
};

// ---- Fling ----------------------------------------------------------------------

class FlingHandler final : public GestureHandler {
 public:
  using GestureHandler::GestureHandler;

  void updateGestureConfig(const folly::dynamic &c) override {
    GestureHandler::updateGestureConfig(c);
    if (auto v = numberIn(c, "direction"); v && *v) direction_ = int(*v);
    if (auto v = numberIn(c, "numberOfPointers"); v && *v) numberOfPointersRequired_ = int(*v);
  }

  void activate(bool force = false) override {
    GestureHandler::activate(force);
    end();
  }

 protected:
  void resetConfig() override {
    GestureHandler::resetConfig();
    numberOfPointersRequired_ = 1;
    direction_ = 1;  // RIGHT
  }
  void onPointerDown(const AdaptedEvent &e) override {
    if (!isButtonInConfig(e.button)) return;
    tracker_.addToTracker(e);
    keyPointer_ = e.pointerId;
    GestureHandler::onPointerDown(e);
    newPointerAction();
  }
  void onPointerAdd(const AdaptedEvent &e) override {
    tracker_.addToTracker(e);
    GestureHandler::onPointerAdd(e);
    newPointerAction();
  }
  void onPointerMove(const AdaptedEvent &e) override {
    pointerMoveAction(e);
    GestureHandler::onPointerMove(e);
  }
  void onPointerOutOfBounds(const AdaptedEvent &e) override {
    pointerMoveAction(e);
    GestureHandler::onPointerOutOfBounds(e);
  }
  void onPointerUp(const AdaptedEvent &e) override {
    GestureHandler::onPointerUp(e);
    onUp(e);
    keyPointer_ = -1;
  }
  void onPointerRemove(const AdaptedEvent &e) override {
    GestureHandler::onPointerRemove(e);
    onUp(e);
  }
  void onCancel() override { cancelTimer(delayTimer_); }
  void onReset() override { cancelTimer(delayTimer_); }

 private:
  void startFling() {
    begin();
    maxPointersSimultaneously_ = 1;
    delayTimer_ = startTimer(800, [this] {
      delayTimer_ = 0;
      fail();
    });
  }
  bool tryEndFling() {
    auto v = tracker_.velocity(keyPointer_);
    if (!v) return false;
    double magnitude = std::hypot(v->x, v->y);
    double ux = magnitude > 0.1 ? v->x / magnitude : 0, uy = magnitude > 0.1 ? v->y / magnitude : 0;
    auto aligned = [&](int direction, double dx, double dy, double cosine) {
      double m = std::hypot(dx, dy);
      return (direction & direction_) == direction && ux * dx / m + uy * dy / m > cosine;
    };
    const double axial = std::cos(30.0 / 2 * M_PI / 180), diagonal = std::cos(60.0 / 2 * M_PI / 180);
    bool isAligned = aligned(1, 1, 0, axial) || aligned(2, -1, 0, axial) || aligned(4, 0, -1, axial) ||
                     aligned(8, 0, 1, axial) || aligned(5, 1, -1, diagonal) || aligned(9, 1, 1, diagonal) ||
                     aligned(6, -1, -1, diagonal) || aligned(10, -1, 1, diagonal);
    if (maxPointersSimultaneously_ == numberOfPointersRequired_ && isAligned && magnitude > 700) {
      cancelTimer(delayTimer_);
      activate();
      return true;
    }
    return false;
  }
  void newPointerAction() {
    if (state_ == UNDETERMINED) startFling();
    if (state_ != BEGAN) return;
    tryEndFling();
    maxPointersSimultaneously_ = std::max(maxPointersSimultaneously_, tracker_.trackedPointersCount());
  }
  void pointerMoveAction(const AdaptedEvent &e) {
    tracker_.track(e);
    if (state_ == BEGAN) tryEndFling();
  }
  void onUp(const AdaptedEvent &e) {
    if (state_ == BEGAN && !tryEndFling()) fail();
    tracker_.removeFromTracker(e.pointerId);
  }

  int numberOfPointersRequired_ = 1, direction_ = 1;
  int maxPointersSimultaneously_ = 0, keyPointer_ = -1;
  guint delayTimer_ = 0;
};

// ---- Hover ----------------------------------------------------------------------

class HoverHandler final : public GestureHandler {
 public:
  using GestureHandler::GestureHandler;
  bool isContinuous() const override { return true; }

 protected:
  void onPointerMoveOver(const AdaptedEvent &e) override {
    orchestrator_.recordHandlerIfNotPresent(this);
    tracker_.addToTracker(e);
    if (state_ == UNDETERMINED) {
      begin();
      activate();
    }
  }
  void onPointerMoveOut(const AdaptedEvent &e) override {
    tracker_.removeFromTracker(e.pointerId);
    end();
  }
  void onPointerMove(const AdaptedEvent &e) override {
    tracker_.track(e);
    GestureHandler::onPointerMove(e);
  }
};

// ---- Manual ---------------------------------------------------------------------

class ManualHandler final : public GestureHandler {
 public:
  using GestureHandler::GestureHandler;
  bool isContinuous() const override { return true; }

 protected:
  void onPointerDown(const AdaptedEvent &e) override {
    tracker_.addToTracker(e);
    GestureHandler::onPointerDown(e);
    begin();
  }
  void onPointerAdd(const AdaptedEvent &e) override {
    tracker_.addToTracker(e);
    GestureHandler::onPointerAdd(e);
  }
  void onPointerMove(const AdaptedEvent &e) override {
    tracker_.track(e);
    GestureHandler::onPointerMove(e);
  }
  void onPointerOutOfBounds(const AdaptedEvent &e) override {
    tracker_.track(e);
    GestureHandler::onPointerOutOfBounds(e);
  }
  void onPointerUp(const AdaptedEvent &e) override {
    GestureHandler::onPointerUp(e);
    tracker_.removeFromTracker(e.pointerId);
  }
  void onPointerRemove(const AdaptedEvent &e) override {
    GestureHandler::onPointerRemove(e);
    tracker_.removeFromTracker(e.pointerId);
  }
};

}  // namespace

std::unique_ptr<GestureHandler> createHandler(const std::string &name, int tag, Orchestrator &orchestrator) {
  std::unique_ptr<GestureHandler> h;
  if (name == "TapGestureHandler") h = std::make_unique<TapHandler>(tag, name, orchestrator);
  else if (name == "LongPressGestureHandler") h = std::make_unique<LongPressHandler>(tag, name, orchestrator);
  else if (name == "PanGestureHandler") h = std::make_unique<PanHandler>(tag, name, orchestrator);
  else if (name == "FlingGestureHandler") h = std::make_unique<FlingHandler>(tag, name, orchestrator);
  else if (name == "HoverGestureHandler") h = std::make_unique<HoverHandler>(tag, name, orchestrator);
  else if (name == "ManualGestureHandler") h = std::make_unique<ManualHandler>(tag, name, orchestrator);
  return h;
}

}  // namespace rngtk_gh
