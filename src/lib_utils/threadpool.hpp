#pragma once

#include <cassert>
#include <functional>
#include <thread>

#include "queue.hpp"

class ThreadPool {
  public:
  ThreadPool(const std::string &name = "", int threadCount = std::thread::hardware_concurrency())
      : name(name) {
    for(int i = 0; i < threadCount; ++i) {
      threads.push_back(std::thread(&ThreadPool::run, this));
    }
  }

  ~ThreadPool() {
    workQueue.clear(); // speedup exit
    for(auto &t : threads) {
      (void)t;
      workQueue.push(nullptr);
    }
    for(auto &t : threads) {
      t.join();
    }
  }

  void submit(std::function<void()> f) {
    assert(f);

    workQueue.push(f);
  }

  private:
  ThreadPool(const ThreadPool &) = delete;

  void run() {
    while(auto task = workQueue.pop()) {
      try {
        task();
      } catch(...) {
        // should not occur
      }
    }
  }

  Queue<std::function<void(void)>> workQueue;
  std::vector<std::thread> threads;
  std::string name;
};
