#pragma once

#include <memory>
#include <typeinfo>
#include <stdexcept> //runtime_error
#include <string>

using std::make_unique;
using std::make_shared;

template<class T>
std::unique_ptr<T> uptr(T *p) {
	return std::unique_ptr<T>(p);
}

[[noreturn]] inline void throw_dynamic_cast_error(const char* typeNameFrom, const char* typeNameTo) {
	throw std::runtime_error("dynamic cast error: could not convert from " + std::string(typeNameFrom) + " to " + std::string(typeNameTo));
}


template<class T, class U>
std::shared_ptr<T> safe_cast(std::shared_ptr<U> p) {
	if (!p)
		return nullptr;
	auto r = std::dynamic_pointer_cast<T>(p);
	if (!r)
		throw_dynamic_cast_error(typeid(U).name(), typeid(T).name());
	return r;
}

template<typename T, typename U>
T* safe_cast(U *p) {
	if (!p)
		return nullptr;
	auto r = dynamic_cast<T*>(p);
	if (!r)
		throw_dynamic_cast_error(typeid(U).name(), typeid(T).name());

	return r;
}

template<typename T>
constexpr T operator | (T a, T b) {
	return static_cast<T>(static_cast<int>(a) | static_cast<int>(b));
}

template<typename T>
constexpr T operator & (T a, T b) {
	return static_cast<T>(static_cast<int>(a) & static_cast<int>(b));
}

inline
void enforce(bool condition, const char* msg) {
	if (!condition)
		throw std::runtime_error(msg);
}
