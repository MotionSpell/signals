#pragma once

#include <functional>

namespace Signals {

struct IExecutor {
  virtual ~IExecutor() {}
  virtual void call(const std::function<void()> &fn) = 0;
};

// synchronous calls
class ExecutorSync : public IExecutor {
  public:
  void call(const std::function<void()> &fn) override { fn(); }
};
}
