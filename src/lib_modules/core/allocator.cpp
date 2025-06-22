#include "allocator.hpp"
#include "lib_utils/queue.hpp"
#include <stdexcept>
#include <cassert>
#include <atomic>

namespace Modules {

void ensureAligned(void* p, size_t alignment) {
	if((uintptr_t(p) % alignment))
		throw std::logic_error("Invalid memory buffer alignment");
}

struct MemoryAllocator : IAllocator {
		MemoryAllocator(size_t maxBlocks) :
			maxBlocks(maxBlocks),
			curNumBlocks(maxBlocks) {
			if (maxBlocks == 0)
				throw std::runtime_error("Cannot create an allocator with 0 block.");
			allocatedBlockCount = 0;
			for (size_t i=0; i<maxBlocks; ++i) {
				eventQueue.push(Event{OneBufferIsFree});
			}
		}

		~MemoryAllocator() {
			assert(allocatedBlockCount == 0);
		}

		void* alloc(size_t size) override {
			Event block;
			if (!eventQueue.tryPop(block)) {
				if (curNumBlocks < maxBlocks) {
					eventQueue.push(Event{OneBufferIsFree});
					curNumBlocks++;
				}
				block = eventQueue.pop();
			}
			switch (block.type) {
			case OneBufferIsFree: {
				allocatedBlockCount++;
				return new uint8_t[size];
			}
			}
			return nullptr;
		}

		void free(void* p) override {
			delete[] (uint8_t*)p;
			allocatedBlockCount--;
			eventQueue.push(Event{OneBufferIsFree});
		}

	private:
		enum EventType {
			OneBufferIsFree,
		};
		struct Event {
			EventType type {};
		};

		const size_t maxBlocks;
		std::atomic_size_t curNumBlocks;
		Queue<Event> eventQueue;

		// Count of blocks 'in the wild'.
		// Only used for sanity-checking at destruction time.
		std::atomic<int> allocatedBlockCount;
};

std::unique_ptr<IAllocator> createMemoryAllocator(size_t maxBlocks) {
	return std::make_unique<MemoryAllocator>(maxBlocks);
}
}

