#include "JsMessageQueueThread.h"

#include <pthread.h>

namespace rngtk {

JsMessageQueueThread::JsMessageQueueThread(std::string name)
    : name_(std::move(name)), thread_([this] { loop(); }) {}

JsMessageQueueThread::~JsMessageQueueThread() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
    queue_.clear();
  }
  cv_.notify_all();
  if (thread_.joinable()) {
    if (isOnThread()) {
      thread_.detach();  // the last reference dropped by a JS task
    } else {
      thread_.join();
    }
  }
}

void JsMessageQueueThread::loop() {
  pthread_setname_np(pthread_self(), name_.substr(0, 15).c_str());
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return quit_ || !queue_.empty(); });
      if (quit_) return;
      task = std::move(queue_.front());
      queue_.pop_front();
      busy_ = true;
    }
    if (task) task();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      busy_ = false;
    }
  }
}

void JsMessageQueueThread::runOnQueue(std::function<void()> &&runnable) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (quit_) return;
    queue_.push_back(std::move(runnable));
  }
  cv_.notify_one();
}

void JsMessageQueueThread::runOnQueueSync(std::function<void()> &&runnable) {
  if (isOnThread()) {
    runnable();
    return;
  }
  std::mutex doneMutex;
  std::condition_variable doneCv;
  bool done = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (quit_) return;  // a quit queue drops work; don't wait for it
    queue_.push_back([&] {
      runnable();
      std::lock_guard<std::mutex> l(doneMutex);
      done = true;
      doneCv.notify_one();
    });
  }
  cv_.notify_one();
  std::unique_lock<std::mutex> lock(doneMutex);
  doneCv.wait(lock, [&] { return done; });
}

void JsMessageQueueThread::quitSynchronous() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
    queue_.clear();
  }
  cv_.notify_all();
  if (thread_.joinable() && !isOnThread()) thread_.join();
}

bool JsMessageQueueThread::isIdle() {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.empty() && !busy_;
}

}  // namespace rngtk
