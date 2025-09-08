#include "lib_utils/queue.hpp"

#include "tests/tests.hpp"

#include <thread>

using namespace Tests;

namespace {

unittest("[DISABLED] thread-safe queue with non-pointer types") {
  Queue<int> queue;
  const int val = 1;

  queue.push(val);
  auto data = queue.pop();
  ASSERT(data == val);

  queue.push(val);
  auto res = queue.tryPop(data);
  ASSERT((res == true) && (data == val));
  res = queue.tryPop(data);
  ASSERT(res == false);

  queue.clear();
  res = queue.tryPop(data);
  ASSERT(res == false);
}

unittest("thread-safe queue can be cleared while a blocking pop() is waiting") {
  Queue<int> queue;
  auto f = [&]() {
    auto data = queue.pop();
    ASSERT_EQUALS(7, data);
  };
  std::thread tf(f);
  queue.clear();
  queue.push(7);
  tf.join();
}

unittest("thread-safe queue can be cleared while several blocking pop() are waiting") {
  Queue<int> queue;
  auto f = [&]() {
    auto data = queue.pop();
    ASSERT_EQUALS(9, data);
  };
  std::thread tf1(f);
  std::thread tf2(f);
  std::thread tf3(f);
  queue.clear();
  queue.push(9);
  queue.push(9);
  queue.push(9);
  tf1.join();
  tf2.join();
  tf3.join();
}

}
