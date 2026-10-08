#include "GtkMessageQueueThread.h"

#include <condition_variable>

namespace rngtk {

GtkMessageQueueThread::GtkMessageQueueThread(std::function<void()> onDrained)
    : mainThread_(std::this_thread::get_id()),
      onDrained_(std::move(onDrained)) {}

GtkMessageQueueThread::~GtkMessageQueueThread() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (sourceId_ != 0) g_source_remove(sourceId_);
}

void GtkMessageQueueThread::runOnQueue(std::function<void()> &&runnable) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (quit_) return;
  queue_.push_back(std::move(runnable));
  if (sourceId_ == 0) {
    // g_idle_add is thread-safe and wakes the main context.
    sourceId_ = g_idle_add_full(G_PRIORITY_DEFAULT, dispatch, this, nullptr);
  }
}

void GtkMessageQueueThread::runOnQueueSync(std::function<void()> &&runnable) {
  if (std::this_thread::get_id() == mainThread_) {
    drain();
    runnable();
    return;
  }
  std::mutex doneMutex;
  std::condition_variable doneCv;
  bool done = false;
  runOnQueue([&]() {
    runnable();
    std::lock_guard<std::mutex> lock(doneMutex);
    done = true;
    doneCv.notify_one();
  });
  std::unique_lock<std::mutex> lock(doneMutex);
  doneCv.wait(lock, [&] { return done; });
}

void GtkMessageQueueThread::quitSynchronous() {
  if (std::this_thread::get_id() == mainThread_) drain();
  std::lock_guard<std::mutex> lock(mutex_);
  quit_ = true;
  queue_.clear();
}

gboolean GtkMessageQueueThread::dispatch(gpointer data) {
  auto *self = static_cast<GtkMessageQueueThread *>(data);
  {
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->sourceId_ = 0;
  }
  self->drain();
  return G_SOURCE_REMOVE;
}

void GtkMessageQueueThread::drain() {
  bool ranAny = false;
  for (;;) {
    std::function<void()> task;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.empty()) break;
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    if (task) task();
    ranAny = true;
  }
  if (ranAny && onDrained_) onDrained_();
}

bool GtkMessageQueueThread::hasPendingWork() {
  std::lock_guard<std::mutex> lock(mutex_);
  return !queue_.empty();
}

}  // namespace rngtk
