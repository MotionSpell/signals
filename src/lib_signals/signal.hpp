#pragma once

#include <functional>

namespace Signals {

struct IExecutor;

template<typename Arg>
struct ISignal {
  virtual ~ISignal() = default;
  virtual int connect(const std::function<void(Arg)> &cb, IExecutor *executor = nullptr) = 0;
  virtual void disconnect(int connectionId) = 0;
  virtual void disconnectAll() = 0;
  virtual void emit(Arg arg) = 0;
};

}
