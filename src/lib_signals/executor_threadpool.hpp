#pragma once

#include "lib_utils/threadpool.hpp"

namespace Signals {

template<typename> class ExecutorThreadPool;
template<typename> class ExecutorThread;

//tasks occur in the pool
template< typename... Args>
class ExecutorThreadPool<void(Args...)> : public IExecutor<void(Args...)> {
	public:
		ExecutorThreadPool() : threadPool(std::make_shared<ThreadPool>()) {
		}

		ExecutorThreadPool(std::shared_ptr<ThreadPool> threadPool) : threadPool(threadPool) {
		}

		std::shared_future<void> operator() (const std::function<void(Args...)> &fn, Args... args) {
			return threadPool->submit(fn, args...);
		}

	private:
		std::shared_ptr<ThreadPool> threadPool;
};

//tasks occur in a thread
template< typename... Args>
class ExecutorThread<void(Args...)> : public IExecutor<void(Args...)> {
	public:
		ExecutorThread(const std::string &name) : threadPool(name, 1) {
		}

		std::shared_future<void> operator() (const std::function<void(Args...)> &fn, Args... args) {
			return threadPool.submit(fn, args...);
		}

	private:
		ThreadPool threadPool;
};

}
