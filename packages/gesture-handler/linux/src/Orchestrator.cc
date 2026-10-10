// The library's web GestureHandlerOrchestrator, in C++.
#include "Orchestrator.h"

#include <algorithm>

namespace rngtk_gh {

namespace {
template <typename T>
bool has(const std::vector<T> &v, const T &x) {
  return std::find(v.begin(), v.end(), x) != v.end();
}
template <typename T>
void erase(std::vector<T> &v, const T &x) {
  v.erase(std::remove(v.begin(), v.end(), x), v.end());
}
}  // namespace

void Orchestrator::scheduleFinishedHandlersCleanup() {
  if (handlingChange_ != 0 || cleanup_) return;
  // queueMicrotask on the web: once the current event is handled.
  cleanup_ = g_idle_add_full(G_PRIORITY_HIGH, [](gpointer self) -> gboolean {
    auto *o = static_cast<Orchestrator *>(self);
    o->cleanup_ = 0;
    o->cleanupFinishedHandlers();
    return G_SOURCE_REMOVE;
  }, this, nullptr);
}

void Orchestrator::cleanHandler(GestureHandler *handler) {
  handler->reset();
  handler->active = false;
  handler->awaiting = false;
  handler->activationIndex = std::numeric_limits<long>::max();
}

void Orchestrator::dropHandler(GestureHandler *handler) {
  cleanHandler(handler);
  removeHandlerFromOrchestrator(handler);
}

void Orchestrator::forget(GestureHandler *handler) {
  removeHandlerFromOrchestrator(handler);
  if (handlers_.empty() && awaiting_.empty() && cleanup_) {
    g_source_remove(cleanup_);
    cleanup_ = 0;
  }
}

bool Orchestrator::isHandlerRecorded(GestureHandler *handler) const { return has(handlers_, handler); }

void Orchestrator::removeHandlerFromOrchestrator(GestureHandler *handler) {
  erase(handlers_, handler);
  if (has(awaiting_, handler)) {
    erase(awaiting_, handler);
    awaitingTags_.erase(handler->tag());
  }
}

void Orchestrator::cleanupFinishedHandlers() {
  std::vector<GestureHandler *> keep;
  for (GestureHandler *h : handlers_) {
    if (isFinished(h->state()) && !h->awaiting) cleanHandler(h);
    else keep.push_back(h);
  }
  handlers_ = keep;
}

bool Orchestrator::hasOtherHandlerToWaitFor(GestureHandler *handler) {
  return std::any_of(handlers_.begin(), handlers_.end(), [&](GestureHandler *other) {
    return !isFinished(other->state()) && shouldHandlerWaitForOther(handler, other);
  });
}

bool Orchestrator::shouldBeCancelledByFinishedHandler(GestureHandler *handler) {
  return std::any_of(handlers_.begin(), handlers_.end(), [&](GestureHandler *other) {
    return shouldHandlerWaitForOther(handler, other) && other->state() == END;
  });
}

void Orchestrator::tryActivate(GestureHandler *handler) {
  if (shouldBeCancelledByFinishedHandler(handler)) {
    handler->cancel();
    return;
  }
  if (hasOtherHandlerToWaitFor(handler)) {
    addAwaitingHandler(handler);
    return;
  }
  State state = handler->state();
  if (state == CANCELLED || state == FAILED) return;
  if (shouldActivate(handler)) {
    makeActive(handler);
    return;
  }
  if (state == ACTIVE) {
    handler->fail();
    return;
  }
  if (state == BEGAN) handler->cancel();
}

bool Orchestrator::shouldActivate(GestureHandler *handler) {
  return !std::any_of(handlers_.begin(), handlers_.end(),
                      [&](GestureHandler *other) { return shouldHandlerBeCancelledBy(handler, other); });
}

void Orchestrator::cleanupAwaitingHandlers(GestureHandler *handler) {
  for (GestureHandler *other : awaiting_) {
    if (!other->awaiting && shouldHandlerWaitForOther(other, handler)) {
      cleanHandler(other);
      awaitingTags_.erase(other->tag());
    }
  }
  std::vector<GestureHandler *> keep;
  for (GestureHandler *other : awaiting_) {
    if (awaitingTags_.count(other->tag())) keep.push_back(other);
  }
  awaiting_ = keep;
}

void Orchestrator::onHandlerStateChange(GestureHandler *handler, State newState, State oldState,
                                        bool sendIfDisabled) {
  if (!handler->enabled() && !sendIfDisabled) return;
  handlingChange_++;
  if (isFinished(newState)) {
    for (GestureHandler *other : std::vector<GestureHandler *>(awaiting_)) {
      if (!shouldHandlerWaitForOther(other, handler) || !awaitingTags_.count(other->tag())) continue;
      if (newState != END) {
        tryActivate(other);
        continue;
      }
      other->cancel();
      if (other->state() == END) {
        // A discrete gesture that ended right after activating, cancelled
        // now: JS still needs BEGAN -> CANCELLED.
        other->sendEvent(CANCELLED, BEGAN);
      }
      other->awaiting = false;
    }
  }
  if (newState == ACTIVE) {
    tryActivate(handler);
  } else if (oldState == ACTIVE || oldState == END) {
    if (handler->active) {
      handler->sendEvent(newState, oldState);
    } else if (oldState == ACTIVE && (newState == CANCELLED || newState == FAILED)) {
      handler->sendEvent(newState, BEGAN);
    }
  } else if (oldState != UNDETERMINED || newState != CANCELLED) {
    handler->sendEvent(newState, oldState);
  }
  handlingChange_--;
  scheduleFinishedHandlersCleanup();
  if (!has(awaiting_, handler)) cleanupAwaitingHandlers(handler);
}

void Orchestrator::makeActive(GestureHandler *handler) {
  State current = handler->state();
  handler->active = true;
  handler->shouldResetProgress = true;
  handler->activationIndex = activationIndex_++;
  for (size_t i = handlers_.size(); i-- > 0;) {
    if (i < handlers_.size() && shouldHandlerBeCancelledBy(handlers_[i], handler)) handlers_[i]->cancel();
  }
  for (GestureHandler *other : awaiting_) {
    if (shouldHandlerBeCancelledBy(other, handler)) other->awaiting = false;
  }
  // A native gesture takes the touches from the JS responder (Pressable,
  // ScrollView), as RootViewGestureHandler's cancellation does on Android.
  if (handler->cancelsJSResponder() && handler->delegate()) handler->delegate()->cancelJSResponder();
  handler->sendEvent(ACTIVE, BEGAN);
  if (current != ACTIVE) {
    handler->sendEvent(END, ACTIVE);
    if (current != END) handler->sendEvent(UNDETERMINED, END);
  }
  if (!handler->awaiting) return;
  handler->awaiting = false;
  erase(awaiting_, handler);
}

void Orchestrator::addAwaitingHandler(GestureHandler *handler) {
  if (has(awaiting_, handler)) return;
  awaiting_.push_back(handler);
  awaitingTags_.insert(handler->tag());
  handler->awaiting = true;
  handler->activationIndex = activationIndex_++;
}

void Orchestrator::dropIdleHandlers() {
  for (size_t i = handlers_.size(); i-- > 0;) {
    GestureHandler *h = handlers_[i];
    if (h->state() == UNDETERMINED && h->tracker().trackedPointersCount() == 0) {
      removeHandlerFromOrchestrator(h);
    }
  }
}

void Orchestrator::recordHandlerIfNotPresent(GestureHandler *handler) {
  dropIdleHandlers();
  if (isHandlerRecorded(handler)) return;
  handler->active = false;
  handler->awaiting = false;
  handler->activationIndex = std::numeric_limits<long>::max();
  if (!handler->shouldBeginWithRecordedHandlers(handlers_)) handler->cancel();
  handlers_.push_back(handler);
}

bool Orchestrator::shouldHandlerWaitForOther(GestureHandler *handler, GestureHandler *other) {
  return handler != other &&
         (handler->shouldWaitForHandlerFailure(*other) || other->shouldRequireToWaitForFailure(*handler));
}

bool Orchestrator::canRunSimultaneously(GestureHandler *a, GestureHandler *b) {
  return a == b || a->shouldRecognizeSimultaneously(*b) || b->shouldRecognizeSimultaneously(*a);
}

bool Orchestrator::shouldHandlerBeCancelledBy(GestureHandler *handler, GestureHandler *other) {
  if (canRunSimultaneously(handler, other)) return false;
  if (handler->awaiting || handler->state() == ACTIVE) return handler->shouldBeCancelledByOther(*other);
  if (!PointerTracker::shareCommonPointers(handler->trackedPointersIDs(), other->trackedPointersIDs()) &&
      handler->view != other->view) {
    return checkOverlap(handler, other);
  }
  return true;
}

bool Orchestrator::checkOverlap(GestureHandler *handler, GestureHandler *other) {
  for (int id : handler->trackedPointersIDs()) {
    auto point = handler->tracker().lastAbsoluteCoords(id);
    if (point && handler->isPointerInBounds(*point) && other->isPointerInBounds(*point)) return true;
  }
  return false;
}

void Orchestrator::cancelMouseAndPenGestures(GestureHandler *current) {
  for (GestureHandler *h : std::vector<GestureHandler *>(handlers_)) {
    if (h->pointerType() != POINTER_MOUSE && h->pointerType() != POINTER_STYLUS) continue;
    if (h != current) h->cancel();
    else h->tracker().resetTracker();
  }
}

}  // namespace rngtk_gh
