#pragma once

namespace Signals {

/* member function helper */
template<typename Class, typename MemberFunction>
class MemberFunctor {
	public:
		MemberFunctor(Class *object, MemberFunction function) : object(object), function(function) {
		}

		template<typename... Args>
		void operator()(Args... args) {
			(object->*function)(args...);
		}

	private:
		Class *object;
		MemberFunction function;
};

template<typename Class, typename... Args>
MemberFunctor<Class, void (Class::*)(Args...)>
BindMember(Class* objectPtr, void (Class::*memberFunction) (Args...)) {
	return MemberFunctor<Class, void (Class::*)(Args...)>(objectPtr, memberFunction);
}

template<typename Class, typename Function>
MemberFunctor<Class, void(Class::*)()>
Bind(Function func, Class* objectPtr) {
	return MemberFunctor<Class, void(Class::*)()>(objectPtr, func);
}

}
