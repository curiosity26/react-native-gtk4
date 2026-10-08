// MessageQueueThread that runs React Native's JS work on the GTK main loop.
//
// For now the JS thread is the GTK main thread: mount transactions arrive on
// the thread that owns the widgets, and nothing needs to hop threads.
//
// Reloads tear the queue down from another thread (ReactHost's reload
// thread), so pending main-loop dispatches hold a reference to the queue
// and quitSynchronous() waits for the task the main thread is running.
// Create it with std::make_shared.
#pragma once

#include <cxxreact/MessageQueueThread.h>
#include <glib.h>

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace rngtk {

class GtkMessageQueueThread
    : public facebook::react::MessageQueueThread,
      public std::enable_shared_from_this<GtkMessageQueueThread> {
 public:
  // Called after each batch of queued work, on the main thread.
  explicit GtkMessageQueueThread(std::function<void()> onDrained = nullptr);
  ~GtkMessageQueueThread() override;

  void runOnQueue(std::function<void()> &&runnable) override;
  void runOnQueueSync(std::function<void()> &&runnable) override;
  void quitSynchronous() override;

  // Runs everything queued so far (main thread only).
  void drain();
  bool hasPendingWork();

 private:
  static gboolean dispatch(gpointer data);

  std::function<void()> onDrained_;
  std::mutex mutex_;
  std::deque<std::function<void()>> queue_;
  guint sourceId_{0};
  bool quit_{false};
};

}  // namespace rngtk
