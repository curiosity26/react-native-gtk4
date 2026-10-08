#include "GtkMessageQueueThread.h"

#include <condition_variable>

namespace rngtk {

namespace {

// The first queue is created with the host, on the GTK main thread. Later
// ones are created on whatever thread creates the JS instance (ReactHost's
// reload thread, on reload), so remember the main thread from the first.
std::thread::id mainThread() {
  static const std::thread::id id = std::this_thread::get_id();
  return id;
}

bool onMainThread() { return std::this_thread::get_id() == mainThread(); }

}  // namespace

GtkMessageQueueThread::GtkMessageQueueThread(std::function<void()> onDrained)
    : onDrained_(std::move(onDrained)) {
  mainThread();
}

// A pending dispatch owns a reference, so the queue outlives it.
GtkMessageQueueThread::~GtkMessageQueueThread() = default;

void GtkMessageQueueThread::runOnQueue(std::function<void()> &&runnable) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (quit_) return;
  queue_.push_back(std::move(runnable));
  if (sourceId_ == 0) {
    // g_idle_add is thread-safe and wakes the main context.
    sourceId_ = g_idle_add_full(
        G_PRIORITY_DEFAULT, dispatch,
        new std::shared_ptr<GtkMessageQueueThread>(shared_from_this()),
        [](gpointer data) {
          delete static_cast<std::shared_ptr<GtkMessageQueueThread> *>(data);
        });
  }
}

void GtkMessageQueueThread::runOnQueueSync(std::function<void()> &&runnable) {
  if (onMainThread()) {
    drain();
    runnable();
    return;
  }
  {
    // A quit queue drops work; don't wait for it.
    std::lock_guard<std::mutex> lock(mutex_);
    if (quit_) return;
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
  auto quit = [this] {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
    queue_.clear();
  };
  if (onMainThread()) {
    drain();
    quit();
    return;
  }
  // From another thread: quit between two tasks on the main thread, so no
  // JS is running when the caller goes on to destroy the runtime.
  runOnQueueSync(quit);
}

gboolean GtkMessageQueueThread::dispatch(gpointer data) {
  auto *self =
      static_cast<std::shared_ptr<GtkMessageQueueThread> *>(data)->get();
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
    std::lock_guard<std::mutex> lock(mutex_);
    if (quit_) return;
  }
  if (ranAny && onDrained_) onDrained_();
}

bool GtkMessageQueueThread::hasPendingWork() {
  std::lock_guard<std::mutex> lock(mutex_);
  return !queue_.empty();
}

}  // namespace rngtk
