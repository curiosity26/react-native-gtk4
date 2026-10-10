// The gesture orchestrator (the library's web GestureHandlerOrchestrator, a
// port of Android's): which recorded handlers may activate, which wait for
// others to fail, which cancel each other; relations from configureRelations
// (waitFor, simultaneousHandlers, blocksHandlers). Native handlers that
// activate cancel the others unless they recognize simultaneously.
#pragma once

#include "GestureHandler.h"

#include <set>
#include <vector>

namespace rngtk_gh {

class Orchestrator {
 public:
  void recordHandlerIfNotPresent(GestureHandler *handler);
  bool isHandlerRecorded(GestureHandler *handler) const;
  void removeHandlerFromOrchestrator(GestureHandler *handler);
  void dropHandler(GestureHandler *handler);
  void onHandlerStateChange(GestureHandler *handler, State newState, State oldState,
                            bool sendIfDisabled);
  void cancelMouseAndPenGestures(GestureHandler *current);
  // A handler is going away.
  void forget(GestureHandler *handler);
  const std::vector<GestureHandler *> &handlers() const { return handlers_; }

 private:
  void scheduleFinishedHandlersCleanup();
  void cleanupFinishedHandlers();
  void cleanHandler(GestureHandler *handler);
  bool hasOtherHandlerToWaitFor(GestureHandler *handler);
  bool shouldBeCancelledByFinishedHandler(GestureHandler *handler);
  void tryActivate(GestureHandler *handler);
  bool shouldActivate(GestureHandler *handler);
  void cleanupAwaitingHandlers(GestureHandler *handler);
  void makeActive(GestureHandler *handler);
  void addAwaitingHandler(GestureHandler *handler);
  void dropIdleHandlers();
  bool shouldHandlerWaitForOther(GestureHandler *handler, GestureHandler *other);
  bool canRunSimultaneously(GestureHandler *a, GestureHandler *b);
  bool shouldHandlerBeCancelledBy(GestureHandler *handler, GestureHandler *other);
  bool checkOverlap(GestureHandler *handler, GestureHandler *other);
  static bool isFinished(State state) { return state == END || state == FAILED || state == CANCELLED; }

  std::vector<GestureHandler *> handlers_;
  std::vector<GestureHandler *> awaiting_;
  std::set<int> awaitingTags_;
  int handlingChange_ = 0;
  long activationIndex_ = 0;
  guint cleanup_ = 0;
};

}  // namespace rngtk_gh
