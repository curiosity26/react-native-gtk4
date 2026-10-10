// GestureHandler (the library's web GestureHandler.ts), its PointerTracker
// and VelocityTracker (a least-squares fit over the last 300 ms, as
// Android's VelocityTracker).
#include "GestureHandler.h"

#include "Orchestrator.h"

#include <algorithm>
#include <cmath>

namespace rngtk_gh {

// ---- Config ------------------------------------------------------------------

std::optional<double> numberIn(const folly::dynamic &config, const char *key) {
  if (!config.isObject()) return std::nullopt;
  auto it = config.find(key);
  if (it == config.items().end() || !it->second.isNumber()) return std::nullopt;
  return it->second.asDouble();
}

std::optional<bool> boolIn(const folly::dynamic &config, const char *key) {
  if (!config.isObject()) return std::nullopt;
  auto it = config.find(key);
  if (it == config.items().end() || !it->second.isBool()) return std::nullopt;
  return it->second.getBool();
}

// ---- VelocityTracker -----------------------------------------------------------

namespace {
constexpr size_t kHistory = 20;
constexpr double kHorizonMs = 300, kStoppedMs = 40;
constexpr size_t kMinSamples = 3;

// The least-squares polynomial fit of y(x), weighted (Android's
// VelocityTracker, via the library's LeastSquareSolver); false if
// degenerate.
bool solveLeastSquares(const std::vector<double> &x, const std::vector<double> &y,
                       const std::vector<double> &w, int degree, std::vector<double> &out) {
  size_t m = x.size(), n = size_t(degree) + 1;
  if (size_t(degree) > m) return false;
  std::vector<double> a(n * m), q(n * m), r(n * n);
  for (size_t h = 0; h < m; h++) {
    a[h] = w[h];
    for (size_t i = 1; i < n; i++) a[i * m + h] = a[(i - 1) * m + h] * x[h];
  }
  auto row = [&](std::vector<double> &mat, size_t i) { return &mat[i * m]; };
  auto dot = [&](const double *u, const double *v) {
    double s = 0;
    for (size_t h = 0; h < m; h++) s += u[h] * v[h];
    return s;
  };
  for (size_t j = 0; j < n; j++) {
    for (size_t h = 0; h < m; h++) q[j * m + h] = a[j * m + h];
    for (size_t i = 0; i < j; i++) {
      double d = dot(row(q, j), row(q, i));
      for (size_t h = 0; h < m; h++) q[j * m + h] -= d * q[i * m + h];
    }
    double norm = std::sqrt(dot(row(q, j), row(q, j)));
    if (norm < 1e-10) return false;
    for (size_t h = 0; h < m; h++) q[j * m + h] /= norm;
    for (size_t i = 0; i < n; i++) r[j * n + i] = i < j ? 0.0 : dot(row(q, j), row(a, i));
  }
  std::vector<double> wy(m);
  for (size_t h = 0; h < m; h++) wy[h] = y[h] * w[h];
  out.assign(n, 0);
  for (size_t i = n; i-- > 0;) {
    out[i] = dot(row(q, i), wy.data());
    for (size_t j = n - 1; j > i; j--) out[i] -= r[i * n + j] * out[j];
    out[i] /= r[i * n + i];
  }
  return true;
}
}  // namespace

void VelocityTracker::add(const AdaptedEvent &event) {
  samples_.push_back(event);
  if (samples_.size() > kHistory) samples_.erase(samples_.begin());
}

Point VelocityTracker::velocity() const {
  if (samples_.empty()) return {};
  std::vector<double> x, y, w, t;
  const AdaptedEvent &newest = samples_.back();
  const AdaptedEvent *previous = &newest;
  for (size_t i = samples_.size(); i-- > 0;) {
    const AdaptedEvent &sample = samples_[i];
    double age = newest.time - sample.time;
    double delta = std::abs(sample.time - previous->time);
    previous = &sample;
    if (age > kHorizonMs || delta > kStoppedMs) break;
    x.push_back(sample.x);
    y.push_back(sample.y);
    w.push_back(1);
    t.push_back(-age);
  }
  if (x.size() < kMinSamples) return {};
  std::vector<double> xFit, yFit;
  if (!solveLeastSquares(t, x, w, 2, xFit) || !solveLeastSquares(t, y, w, 2, yFit)) return {};
  return Point{xFit[1] * 1000, yFit[1] * 1000};
}

// ---- PointerTracker -------------------------------------------------------------

void PointerTracker::addToTracker(const AdaptedEvent &event) {
  if (pointers_.count(event.pointerId)) return;
  lastMovedPointerId_ = event.pointerId;
  pointers_[event.pointerId] = Element{{event.x, event.y}, {event.offsetX, event.offsetY}, event.time, 0, 0};
  for (int &slot : touchIds_) {
    if (slot < 0) {
      slot = event.pointerId;
      break;
    }
  }
  cachedAbsolute_ = absoluteCoordsAverage();
  cachedRelative_ = relativeCoordsAverage();
}

void PointerTracker::removeFromTracker(int pointerId) {
  pointers_.erase(pointerId);
  for (int &slot : touchIds_) {
    if (slot == pointerId) slot = -1;
  }
}

void PointerTracker::track(const AdaptedEvent &event) {
  auto it = pointers_.find(event.pointerId);
  if (it == pointers_.end()) return;
  lastMovedPointerId_ = event.pointerId;
  velocity_.add(event);
  Point v = velocity_.velocity();
  it->second.velocityX = v.x;
  it->second.velocityY = v.y;
  it->second.absolute = {event.x, event.y};
  it->second.relative = {event.offsetX, event.offsetY};
  cachedAbsolute_ = absoluteCoordsAverage();
  cachedRelative_ = relativeCoordsAverage();
}

std::optional<Point> PointerTracker::velocity(int pointerId) const {
  auto it = pointers_.find(pointerId);
  if (it == pointers_.end()) return std::nullopt;
  return Point{it->second.velocityX, it->second.velocityY};
}

std::optional<Point> PointerTracker::lastAbsoluteCoords(std::optional<int> pointerId) const {
  auto it = pointers_.find(pointerId.value_or(lastMovedPointerId_));
  if (it == pointers_.end()) return std::nullopt;
  return it->second.absolute;
}

std::optional<Point> PointerTracker::lastRelativeCoords(std::optional<int> pointerId) const {
  auto it = pointers_.find(pointerId.value_or(lastMovedPointerId_));
  if (it == pointers_.end()) return std::nullopt;
  return it->second.relative;
}

Point PointerTracker::absoluteCoordsAverage() const {
  if (pointers_.empty()) return cachedAbsolute_;
  Point sum;
  for (auto &[id, e] : pointers_) {
    sum.x += e.absolute.x;
    sum.y += e.absolute.y;
  }
  return Point{sum.x / pointers_.size(), sum.y / pointers_.size()};
}

Point PointerTracker::relativeCoordsAverage() const {
  if (pointers_.empty()) return cachedRelative_;
  Point sum;
  for (auto &[id, e] : pointers_) {
    sum.x += e.relative.x;
    sum.y += e.relative.y;
  }
  return Point{sum.x / pointers_.size(), sum.y / pointers_.size()};
}

int PointerTracker::mappedTouchEventId(int pointerId) const {
  for (size_t i = 0; i < touchIds_.size(); i++) {
    if (touchIds_[i] == pointerId) return int(i);
  }
  return -1;
}

void PointerTracker::resetTracker() {
  velocity_.reset();
  pointers_.clear();
  lastMovedPointerId_ = -1;
  std::fill(touchIds_.begin(), touchIds_.end(), -1);
}

std::vector<int> PointerTracker::trackedPointersIDs() const {
  std::vector<int> ids;
  for (auto &[id, e] : pointers_) ids.push_back(id);
  return ids;
}

bool PointerTracker::shareCommonPointers(const std::vector<int> &a, const std::vector<int> &b) {
  return std::any_of(a.begin(), a.end(), [&](int id) { return std::find(b.begin(), b.end(), id) != b.end(); });
}

// ---- GestureHandler -----------------------------------------------------------

GestureHandler::~GestureHandler() { orchestrator_.forget(this); }

double GestureHandler::now() { return double(g_get_monotonic_time()) / 1000.0; }

guint GestureHandler::startTimer(int ms, std::function<void()> fn) {
  return delegate_ ? delegate_->startTimer(ms, std::move(fn)) : 0;
}

void GestureHandler::cancelTimer(guint &id) {
  if (id && delegate_) delegate_->cancelTimer(id);
  id = 0;
}

void GestureHandler::attach(HandlerDelegate *delegate, ActionType actionType) {
  if (delegate_) detach();
  delegate_ = delegate;
  actionType_ = actionType;
  state_ = UNDETERMINED;
}

void GestureHandler::detach() {
  if (!delegate_) return;
  if (state_ == ACTIVE) cancel();
  else fail();
  orchestrator_.dropHandler(this);
  delegate_ = nullptr;
  actionType_ = ACTION_NONE;
  state_ = UNDETERMINED;
  view = nullptr;
  hostDetector = nullptr;
}

void GestureHandler::reset() {
  tracker_.resetTracker();
  onReset();
  resetProgress();
  state_ = UNDETERMINED;
}

void GestureHandler::moveToState(State newState, bool sendIfDisabled) {
  if (state_ == newState) return;
  State oldState = state_;
  state_ = newState;
  bool finished = newState == END || newState == FAILED || newState == CANCELLED;
  if (tracker_.trackedPointersCount() > 0 && needsPointerData_ && finished) cancelTouches();
  orchestrator_.onHandlerStateChange(this, newState, oldState, sendIfDisabled);
  onStateChange(newState, oldState);
  if (!enabled() && finished) state_ = UNDETERMINED;
}

void GestureHandler::begin() {
  if (!checkHitSlop()) return;
  if (state_ == UNDETERMINED) moveToState(BEGAN);
}

void GestureHandler::fail(bool sendIfDisabled) {
  if (state_ == ACTIVE || state_ == BEGAN) moveToState(FAILED, sendIfDisabled);
  resetProgress();
}

void GestureHandler::cancel(bool sendIfDisabled) {
  if (state_ == ACTIVE || state_ == UNDETERMINED || state_ == BEGAN) {
    onCancel();
    moveToState(CANCELLED, sendIfDisabled);
  }
}

void GestureHandler::activate(bool force) {
  if ((!manualActivation_ || force) && state_ == BEGAN) moveToState(ACTIVE);
}

void GestureHandler::end() {
  if (state_ == BEGAN || state_ == ACTIVE) moveToState(END);
  resetProgress();
}

void GestureHandler::setStateFromJS(State state) {
  switch (state) {
    case ACTIVE: activate(true); break;
    case BEGAN: begin(); break;
    case END: end(); break;
    case FAILED: fail(); break;
    case CANCELLED: cancel(); break;
    default: break;
  }
}

static bool contains(const std::vector<int> &v, int tag) {
  return std::find(v.begin(), v.end(), tag) != v.end();
}

bool GestureHandler::shouldWaitForHandlerFailure(GestureHandler &other) {
  return &other != this && contains(waitFor, other.tag());
}

bool GestureHandler::shouldRequireToWaitForFailure(GestureHandler &other) {
  return &other != this && contains(blocks, other.tag());
}

bool GestureHandler::shouldRecognizeSimultaneously(GestureHandler &other) {
  return &other == this || contains(simultaneousWith, other.tag());
}

bool GestureHandler::shouldBeCancelledByOther(GestureHandler &other) {
  if (&other == this) return false;
  // An active native handler (a scroll view's) cancels the others.
  return other.name() == "NativeViewGestureHandler" && other.active && !other.isButton();
}

bool GestureHandler::isPointerInBounds(Point p) const {
  if (!delegate_) return false;
  graphene_rect_t b = delegate_->viewBounds();
  return p.x >= b.origin.x && p.y >= b.origin.y && p.x <= b.origin.x + b.size.width &&
         p.y <= b.origin.y + b.size.height;
}

// ---- Events to JS -------------------------------------------------------------

folly::dynamic GestureHandler::eventData() {
  Point absolute = tracker_.absoluteCoordsAverage();
  Point relative = tracker_.relativeCoordsAverage();
  return folly::dynamic::object("x", relative.x)("y", relative.y)("absoluteX", absolute.x)(
      "absoluteY", absolute.y);
}

static bool usesDetector(ActionType a) {
  return a == ACTION_NATIVE_DETECTOR || a == ACTION_VIRTUAL_DETECTOR;
}

void GestureHandler::sendEvent(State newState, State oldState) {
  if (actionType_ == ACTION_NONE || !delegate_) return;
  bool isStateChange = lastSentState_ != newState;
  folly::dynamic data = eventData();
  data["numberOfPointers"] = tracker_.trackedPointersCount();
  data["pointerType"] = int(pointerType_);
  if (isStateChange) {
    lastSentState_ = newState;
    folly::dynamic event = folly::dynamic::object("handlerTag", tag_)("state", int(newState))(
        "oldState", int(oldState));
    if (usesDetector(actionType_)) {
      event["handlerData"] = data;
    } else {
      for (auto &[k, v] : data.items()) event[k] = v;
    }
    delegate_->sendEvent(*this, std::move(event), "stateChange");
  }
  if (state_ != ACTIVE) return;
  folly::dynamic event = folly::dynamic::object("handlerTag", tag_)("state", int(newState));
  if (usesDetector(actionType_)) {
    event["handlerData"] = data;
  } else {
    for (auto &[k, v] : data.items()) event[k] = v;
  }
  delegate_->sendEvent(*this, std::move(event), "update");
}

static folly::dynamic pointerData(int id, Point absolute, graphene_rect_t view) {
  return folly::dynamic::object("id", id)("x", absolute.x - view.origin.x)("y", absolute.y - view.origin.y)(
      "absoluteX", absolute.x)("absoluteY", absolute.y);
}

void GestureHandler::sendTouchEvent(const AdaptedEvent &event) {
  if (!enabled() || !delegate_) return;
  const auto &pointers = tracker_.trackedPointers();
  if (pointers.empty() || !pointers.count(event.pointerId)) return;
  graphene_rect_t view = delegate_->viewBounds();
  folly::dynamic all = folly::dynamic::array(), changed = folly::dynamic::array();
  for (auto &[id, e] : pointers) all.push_back(pointerData(tracker_.mappedTouchEventId(id), e.absolute, view));
  if (event.eventType != EventType::Cancel) {
    changed.push_back(pointerData(tracker_.mappedTouchEventId(event.pointerId), {event.x, event.y}, view));
  } else {
    changed = all;
  }
  int type = TOUCHES_UNDETERMINED;
  switch (event.eventType) {
    case EventType::Down:
    case EventType::AdditionalPointerDown: type = TOUCHES_DOWN; break;
    case EventType::Up:
    case EventType::AdditionalPointerUp: type = TOUCHES_UP; break;
    case EventType::Move: type = TOUCHES_MOVE; break;
    case EventType::Cancel: type = TOUCHES_CANCEL; break;
    default: break;
  }
  int count = int(all.size());
  if (event.eventType == EventType::Up || event.eventType == EventType::AdditionalPointerUp) count--;
  int state = state_;
  // An awaiting handler that's active reports BEGAN (Android).
  if (awaiting && state == ACTIVE) state = BEGAN;
  folly::dynamic touch = folly::dynamic::object("handlerTag", tag_)("state", state)("eventType", type)(
      "changedTouches", changed)("allTouches", all)("numberOfTouches", count)("pointerType", int(pointerType_));
  delegate_->sendEvent(*this, std::move(touch), "touch");
}

void GestureHandler::cancelTouches() {
  if (!delegate_ || tracker_.trackedPointers().empty()) return;
  AdaptedEvent cancel;
  cancel.eventType = EventType::Cancel;
  cancel.pointerId = tracker_.trackedPointers().begin()->first;
  sendTouchEvent(cancel);
}

void GestureHandler::tryToSendTouchEvent(const AdaptedEvent &event) {
  if (needsPointerData_) sendTouchEvent(event);
}

void GestureHandler::tryToSendMoveEvent(bool out, const AdaptedEvent &event) {
  if ((out && shouldCancelWhenOutside_) || !enabled()) return;
  tryToSendTouchEvent(event);
  if (active) sendEvent(state_, state_);
}

// ---- Input --------------------------------------------------------------------

void GestureHandler::onPointerDown(const AdaptedEvent &event) {
  orchestrator_.recordHandlerIfNotPresent(this);
  pointerType_ = event.pointerType;
  if (pointerType_ == POINTER_TOUCH) orchestrator_.cancelMouseAndPenGestures(this);
  tryToSendTouchEvent(event);
}
void GestureHandler::onPointerAdd(const AdaptedEvent &event) { tryToSendTouchEvent(event); }
void GestureHandler::onPointerUp(const AdaptedEvent &event) { tryToSendTouchEvent(event); }
void GestureHandler::onPointerRemove(const AdaptedEvent &event) { tryToSendTouchEvent(event); }
void GestureHandler::onPointerMove(const AdaptedEvent &event) { tryToSendMoveEvent(false, event); }
void GestureHandler::onPointerOutOfBounds(const AdaptedEvent &event) { tryToSendMoveEvent(true, event); }
void GestureHandler::onPointerEnter(const AdaptedEvent &event) { tryToSendTouchEvent(event); }

void GestureHandler::onPointerLeave(const AdaptedEvent &event) {
  if (shouldCancelWhenOutside_) {
    if (state_ == ACTIVE) cancel();
    else if (state_ == BEGAN) fail();
    return;
  }
  tryToSendTouchEvent(event);
}

void GestureHandler::onPointerCancel(const AdaptedEvent &) {
  if (orchestrator_.isHandlerRecorded(this)) cancel();
}

// ---- Config -------------------------------------------------------------------

void GestureHandler::resetConfig() {
  testID_.clear();
  manualActivation_ = false;
  shouldCancelWhenOutside_ = false;
  mouseButton_ = 0;
  hitSlop_.reset();
  needsPointerData_ = false;
  forAnimated_ = forReanimated_ = false;
  cancelsJSResponder_ = true;
}

void GestureHandler::setGestureConfig(const folly::dynamic &config) {
  resetConfig();
  folly::dynamic c = config.isObject() ? config : folly::dynamic::object();
  if (!c.count("enabled")) c["enabled"] = true;
  updateGestureConfig(c);
}

void GestureHandler::updateGestureConfig(const folly::dynamic &config) {
  bool wasEnabled = enabled();
  if (auto e = boolIn(config, "enabled")) enabled_ = *e;
  else if (!enabled_) enabled_ = true;
  if (config.count("hitSlop")) {
    const folly::dynamic &h = config["hitSlop"];
    if (h.isNumber()) {
      double v = h.asDouble();
      hitSlop_ = std::array<double, 6>{v, v, v, v, NAN, NAN};
    } else if (h.isArray() && h.size() == 6) {
      std::array<double, 6> a;
      for (size_t i = 0; i < 6; i++) a[i] = h[i].isNumber() ? h[i].asDouble() : NAN;
      hitSlop_ = a;
    } else {
      hitSlop_.reset();
    }
  }
  if (config.count("testID") && config["testID"].isString()) testID_ = config["testID"].getString();
  if (auto v = boolIn(config, "dispatchesAnimatedEvents")) forAnimated_ = *v;
  if (auto v = boolIn(config, "dispatchesReanimatedEvents")) forReanimated_ = *v;
  if (auto v = boolIn(config, "manualActivation")) manualActivation_ = *v;
  if (auto v = numberIn(config, "mouseButton")) mouseButton_ = int(*v);
  if (auto v = boolIn(config, "needsPointerData")) needsPointerData_ = *v;
  if (auto v = boolIn(config, "shouldCancelWhenOutside")) shouldCancelWhenOutside_ = *v;
  if (auto v = boolIn(config, "cancelsJSResponder")) cancelsJSResponder_ = *v;
  // The old API's relations come with the config.
  auto tags = [&](const char *key, std::vector<int> &out) {
    if (!config.count(key) || !config[key].isArray()) return;
    out.clear();
    for (auto &t : config[key]) {
      if (t.isNumber()) out.push_back(int(t.asDouble()));
    }
  };
  tags("waitFor", waitFor);
  tags("simultaneousHandlers", simultaneousWith);
  tags("blocksHandlers", blocks);
  (void)wasEnabled;
  if (enabled()) return;
  if (state_ == ACTIVE) fail(true);
  else if (state_ != UNDETERMINED) cancel(true);
  orchestrator_.dropHandler(this);
}

bool GestureHandler::checkHitSlop() const {
  if (!hitSlop_ || !delegate_) return true;
  graphene_rect_t r = delegate_->viewBounds();
  double width = r.size.width, height = r.size.height;
  const auto &s = *hitSlop_;
  double left = 0, top = 0, right = width, bottom = height;
  if (!std::isnan(s[0])) left = -s[0];
  if (!std::isnan(s[2])) right = width + s[2];
  if (!std::isnan(s[1])) top = -s[1];
  if (!std::isnan(s[3])) bottom = height + s[3];
  if (!std::isnan(s[4])) {
    if (!std::isnan(s[0])) right = left + s[4];
    else if (!std::isnan(s[2])) left = right - s[4];
  }
  if (!std::isnan(s[5])) {
    if (!std::isnan(s[1])) bottom = top + s[5];
    else if (!std::isnan(s[3])) top = bottom - s[5];
  }
  auto last = tracker_.lastAbsoluteCoords();
  if (!last) return false;
  double x = last->x - r.origin.x, y = last->y - r.origin.y;
  return x >= left && x <= right && y >= top && y <= bottom;
}

bool GestureHandler::isButtonInConfig(int button) const {
  return !button || (!mouseButton_ && button == MOUSE_LEFT) || (mouseButton_ && (button & mouseButton_));
}

}  // namespace rngtk_gh
