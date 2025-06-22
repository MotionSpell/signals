#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

template<typename T>
class Queue {
	public:
		void push(T data) {
			std::lock_guard<std::mutex> lock(mutex);
			dataQueue.push(std::move(data));
			dataAvailable.notify_one();
		}

		bool tryPop(T &value) {
			std::lock_guard<std::mutex> lock(mutex);
			if (dataQueue.empty()) {
				return false;
			}
			value = std::move(dataQueue.front());
			dataQueue.pop();
			return true;
		}

		T pop() {
			std::unique_lock<std::mutex> lock(mutex);
			while (dataQueue.empty())
				dataAvailable.wait(lock);
			T p;
			std::swap(p, dataQueue.front());
			dataQueue.pop();
			return p;
		}

		void clear() {
			std::lock_guard<std::mutex> lock(mutex);
			std::queue<T> emptyQueue;
			std::swap(emptyQueue, dataQueue);
		}

	private:
		mutable std::mutex mutex;
		std::queue<T> dataQueue;
		std::condition_variable dataAvailable;
};

