#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
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
	virtual size_t emit(Args... args) = 0;

	virtual IExecutor<Callback(Args...)>& getExecutor() const = 0;

	//TODO write test based on the shared_ptr reflector
	/**
	 * Some implementations (such as MSVC std::launch) don't destroy the copy of
	 * 'Args... args' they hold. When using reference counted Args, they'only be
	 * released when calling this function or when getting the results().
	 */
	virtual void flushAvailableResults() = 0;
};

template<typename, typename> class ProtoSignal;

template<typename Result, typename Callback, typename... Args>
class ProtoSignal<Result, Callback(Args...)> : public ISignal<Callback(Args...)> {
protected:
	typedef std::function<Callback(Args...)> CallbackType;

private:
	typedef typename Result::ResultValue ResultValue;
	typedef typename CallbackType::result_type ResultType;
	typedef ConnectionList<ResultType, Args...> ConnectionType;
	typedef std::map<size_t, ConnectionType*> ConnectionManager;

public:
	size_t connect(const CallbackType &cb, IExecutor<Callback(Args...)> &executor) {
		std::lock_guard<std::mutex> lg(callbacksMutex);
		const size_t connectionId = uid++;
		callbacks[connectionId] = new ConnectionType(executor, cb, connectionId);
		return connectionId;
	}

	size_t connect(const CallbackType &cb) {
		return connect(cb, executor);
	}

	bool disconnect(size_t connectionId) {
		std::lock_guard<std::mutex> lg(callbacksMutex);
		return disconnectUnsafe(connectionId);
	}

	size_t emit(Args... args) {
		std::lock_guard<std::mutex> lg(callbacksMutex);
		result.clear();
		for (auto &cb : callbacks) {
			cb.second->futures.push_back(cb.second->executor(cb.second->callback, args...));
		}
		return callbacks.size();
	}

	void flushAvailableResults() {
		std::lock_guard<std::mutex> lg(callbacksMutex);
		fillResultsUnsafe(false, false);
	}

	ResultValue results(bool sync = true, bool single = false) {
		std::lock_guard<std::mutex> lg(callbacksMutex);
		fillResultsUnsafe(sync, single);
		return result.get();
	}

	IExecutor<Callback(Args...)>& getExecutor() const {
		return executor;
	}

protected:
	ProtoSignal() : uid(0), defaultExecutor(new ExecutorAsync<Callback(Args...)>()), executor(*defaultExecutor.get()) {
	}

	ProtoSignal(IExecutor<Callback(Args...)> &executor) : uid(0), executor(executor) {
	}

	virtual ~ProtoSignal() {
		while (!callbacks.empty()) { //delete still connected callbacks
			auto& cb = *callbacks.begin();
			bool res = disconnectUnsafe(cb.first);
			assert(res);
		}
	}

private:
	ProtoSignal(const ProtoSignal&) = delete;
	ProtoSignal& operator= (const ProtoSignal&) = delete;

	bool disconnectUnsafe(size_t connectionId) {
		auto conn = callbacks.find(connectionId);
		if (conn == callbacks.end())
			return false;
		delete conn->second;
		callbacks.erase(connectionId);
		return true;
	}

	void fillResultsUnsafe(bool sync = true, bool single = false) {
		for (auto &cb : callbacks) {
			for (auto f = cb.second->futures.begin(); f != cb.second->futures.end();) {
				if (!sync && (f->wait_for(std::chrono::nanoseconds(0)) == std::future_status::timeout)) {
					++f;
				} else {
					assert(f->valid());
					result.set(f->get());
					f = cb.second->futures.erase(f);
					if (single) {
						return;
					}
				}
			}
		}
	}

	std::mutex callbacksMutex;
	ConnectionManager callbacks; //protected by callbacksMutex
	Result result;               //protected by callbacksMutex
	size_t uid;                  //protected by callbacksMutex
	
	std::unique_ptr<IExecutor<Callback(Args...)>> const defaultExecutor;
	IExecutor<Callback(Args...)> &executor;
};

}
