#pragma once

#include <atomic>
#include <cassert>

/* Copyright 2016 Facebook, Inc., https://github.com/facebook/folly/blob/master/folly/ProducerConsumerQueue.h */
template<class T>
struct QueueLockFree {
		typedef T value_type;

		QueueLockFree(const QueueLockFree&) = delete;
		QueueLockFree& operator = (const QueueLockFree&) = delete;

		// size must be >= 2.
		//
		// Also, note that the number of usable slots in the queue at any
		// given time is actually (size-1), so if you start with an empty queue,
		// isFull() will return true after size-1 insertions.
		explicit QueueLockFree(uint32_t size)
			: size_(size)
			, records_(reinterpret_cast<T*>(new uint8_t[sizeof(T) * size]))
			, readIndex_(0)
			, writeIndex_(0) {
			assert(size >= 2);
			if (!records_) {
				throw std::bad_alloc();
			}
		}

		~QueueLockFree() {
			// We need to destruct anything that may still exist in our queue.
			// (No real synchronization needed at destructor time: only one
			// thread can be doing this.)
			size_t read = readIndex_;
			size_t end = writeIndex_;
			while (read != end) {
				records_[read].~T();
				if (++read == size_) {
					read = 0;
				}
			}

			delete[] (uint8_t*)records_;
		}

		template<class ...Args>
		bool write(Args&&... recordArgs) {
			auto const currentWrite = writeIndex_.load(std::memory_order_relaxed);
			auto nextRecord = currentWrite + 1;
			if (nextRecord == size_) {
				nextRecord = 0;
			}
			if (nextRecord != readIndex_.load(std::memory_order_acquire)) {
				new (&records_[currentWrite]) T(std::forward<Args>(recordArgs)...);
				writeIndex_.store(nextRecord, std::memory_order_release);
				return true;
			}

			// queue is full
			return false;
		}

		// move (or copy) the value at the front of the queue to given variable
		bool read(T& record) {
			auto const currentRead = readIndex_.load(std::memory_order_relaxed);
			if (currentRead == writeIndex_.load(std::memory_order_acquire)) {
				// queue is empty
				return false;
			}

			auto nextRecord = currentRead + 1;
			if (nextRecord == size_) {
				nextRecord = 0;
			}
			record = std::move(records_[currentRead]);
			records_[currentRead].~T();
			readIndex_.store(nextRecord, std::memory_order_release);
			return true;
		}

	private:
		const uint32_t size_;
		T* const records_;

		std::atomic<unsigned int> readIndex_;
		std::atomic<unsigned int> writeIndex_;
};
