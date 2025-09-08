#pragma once

#include <cstddef> // size_t

namespace Modules {

/*user recommended values*/
static const size_t ALLOC_NUM_BLOCKS_DEFAULT = 10;

struct IAllocator {
  virtual ~IAllocator() = default;
  virtual void *alloc(size_t size) = 0;
  virtual void free(void *) = 0;
};

}

#include <memory>

namespace Modules {

std::unique_ptr<IAllocator> createMemoryAllocator(size_t maxBlocks);

template<typename T>
inline constexpr size_t getAlignmentOf() {
  struct AlignmentOf {
    char a;
    T data;
  };
  return sizeof(AlignmentOf) - sizeof(T);
}

void ensureAligned(void *p, size_t alignment);

template<typename T, typename... Args>
std::shared_ptr<T> alloc(std::shared_ptr<IAllocator> allocator, Args &&...args) {
  auto p = allocator->alloc(sizeof(T));

  ensureAligned(p, getAlignmentOf<T>());

  auto deleter = [allocator](T *p) {
    p->~T();
    allocator->free(p);
  };

  return std::shared_ptr<T>(new(p) T(std::forward<Args>(args)...), deleter);
}

}
