#pragma once

#include "signal.hpp"
#include "executor.hpp" // ExecutorSync

#include "lib_utils/small_map.hpp"
#include <memory>
#include <mutex>

namespace Signals {

template<typename Arg>
class Signal : public ISignal<Arg> {
	public:
		typedef std::function<void(Arg)> CallbackType;

		int connect(const CallbackType &cb, IExecutor* executor = nullptr) {
			if(!executor)
				executor = &this->executor;
			std::lock_guard<std::mutex> lg(callbacksMutex);
			const int connectionId = uid++;
			callbacks[connectionId] = {executor, cb};
			return connectionId;
		}

		void disconnect(int connectionId) {
			std::lock_guard<std::mutex> lg(callbacksMutex);
			auto conn = callbacks.find(connectionId);
			if (conn != callbacks.end())
				callbacks.erase(conn);
		}

		void disconnectAll() {
			std::lock_guard<std::mutex> lg(callbacksMutex);
			callbacks.clear();
		}

		void emit(Arg arg) {
			std::lock_guard<std::mutex> lg(callbacksMutex);
			for (auto &cb : callbacks)
				cb.value.executor->call(std::bind(cb.value.callback, arg));
		}

		Signal() : defaultExecutor(new ExecutorSync()), executor(*defaultExecutor.get()) {
		}

	private:
		Signal(const Signal&) = delete;
		Signal& operator= (const Signal&) = delete;

		struct ConnectionType {
			IExecutor* executor;
			std::function<void(Arg)> callback;
		};

		mutable std::mutex callbacksMutex;
		SmallMap<int, ConnectionType> callbacks;  //protected by callbacksMutex
		int uid = 0;                              //protected by callbacksMutex

		std::unique_ptr<IExecutor> const defaultExecutor;
		IExecutor &executor;
};

}
