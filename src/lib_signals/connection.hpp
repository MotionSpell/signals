#pragma once

namespace Signals {

template<typename ResultType, typename... Args>
class ConnectionList {
	public:
		IExecutor &executor;
		std::function<ResultType(Args...)> const callback;
		size_t const uid;

		explicit ConnectionList(IExecutor &executor, const std::function<ResultType(Args...)> &callback, const size_t uid) : executor(executor), callback(callback), uid(uid) {
		}
};

}
