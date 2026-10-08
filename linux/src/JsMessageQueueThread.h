// The JS thread: a MessageQueueThread on a dedicated std::thread, where
// Hermes and React Native's RuntimeScheduler run. GTK stays on the main
// thread; mount transactions, commands and native-driven animation reach
// the widgets through GtkMountingManager, which hands them to the main
// loop in order. See docs/architecture.md.
#pragma once

#include <cxxreact/MessageQueueThread.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace rngtk {

class JsMessageQueueThread : public facebook::react::MessageQueueThread {
 public:
  explicit JsMessageQueueThread(std::string name = "rngtk-js");
  // Joins the thread (quitting it first if needed).
  ~JsMessageQueueThread() override;

  void runOnQueue(std::function<void()> &&runnable) override;
  // Runs inline when called on the JS thread.
  void runOnQueueSync(std::function<void()> &&runnable) override;
  // Lets the running task finish, drops the rest, joins. From any thread
  // but the JS thread itself (ReactHost calls it from the main thread or
  // its reload thread).
  void quitSynchronous() override;

  // No task running or queued (self-tests wait for this).
  bool isIdle();
  bool isOnThread() const {
    return std::this_thread::get_id() == thread_.get_id();
  }

 private:
  void loop();

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> queue_;
  bool quit_{false};
  bool busy_{false};
  std::string name_;
  std::thread thread_;  // last: starts after the members above
};

}  // namespace rngtk
