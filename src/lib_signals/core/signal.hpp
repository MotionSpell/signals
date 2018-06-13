#pragma once

#include "executor.hpp"
#include "connection.hpp"
#include <functional>
#include <map>
#include <mutex>

namespace Signals {

template<typename> class ISignal;

template <typename Callback, typename... Args>
class ISignal<Callback(Args...)> {
	public:
		virtual size_t connect(const std::function<Callback(Args...)> &cb, IExecutor<Callback(Args...)> &executor) = 0;
		virtual size_t connect(const std::function<Callback(Args...)> &cb) = 0;
		virtual bool disconnect(size_t connectionId) = 0;
		virtual size_t getNumConnections() const = 0;
		virtual size_t emit(Args... args) = 0;
};

template<typename> class Signal;

template<typename Callback, typename... Args>
class Signal<Callback(Args...)> : public ISignal<Callback(Args...)> {
	private:
		typedef std::function<Callback(Args...)> CallbackType;
		typedef typename CallbackType::result_type ResultType;
		typedef ConnectionList<ResultType, Args...> ConnectionType;

	public:
		size_t connect(const CallbackType &cb, IExecutor<Callback(Args...)> &executor) {
			std::lock_guard<std::mutex> lg(callbacksMutex);
			const size_t connectionId = uid++;
			callbacks[connectionId] = std::make_unique<ConnectionType>(executor, cb, connectionId);
			return connectionId;
		}

		size_t connect(const CallbackType &cb) {
			return connect(cb, executor);
		}

		bool disconnect(size_t connectionId) {
			std::lock_guard<std::mutex> lg(callbacksMutex);
			return disconnectUnsafe(connectionId);
		}

		size_t getNumConnections() const {
			std::lock_guard<std::mutex> lg(callbacksMutex);
			return callbacks.size();
		}

		size_t emit(Args... args) {
			std::lock_guard<std::mutex> lg(callbacksMutex);
			for (auto &cb : callbacks) {
				cb.second->futures.push_back(cb.second->executor(cb.second->callback, args...));
			}
			return callbacks.size();
		}

		Signal() : defaultExecutor(new ExecutorAsync<Callback(Args...)>()), executor(*defaultExecutor.get()) {
		}

		Signal(IExecutor<Callback(Args...)> &executor) : executor(executor) {
		}

		virtual ~Signal() {
		}

	private:
		Signal(const Signal&) = delete;
		Signal& operator= (const Signal&) = delete;

		bool disconnectUnsafe(size_t connectionId) {
			auto conn = callbacks.find(connectionId);
			if (conn == callbacks.end())
				return false;
			callbacks.erase(connectionId);
			return true;
		}

		mutable std::mutex callbacksMutex;
		std::map<size_t, std::unique_ptr<ConnectionType>> callbacks; //protected by callbacksMutex
		size_t uid = 0;                              //protected by callbacksMutex

		std::unique_ptr<IExecutor<Callback(Args...)>> const defaultExecutor;
		IExecutor<Callback(Args...)> &executor;
};

}
