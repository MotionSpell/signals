#pragma once

#include "format.hpp"
#include <cstring>
#include <memory>
#include <typeinfo>
#include <vector>

// Runs a function at instantiation:
// Use to initialize C libraries at program startup.
// Example: auto g_InitAv = runAtStartup(&av_register_all);
struct DummyStruct {};

//Romain: template<typename T = int>
struct Fraction {
	Fraction(int num, int den) : num(num), den(den) {
	}
	operator double() const {
		return (double)num / den;
	}

	int num;
	int den;
};

template<class R, class... Args>
DummyStruct runAtStartup(R f(Args...), Args... argVal) {
	f(argVal...);
	return DummyStruct();
}

inline
const char *redirectStdToNul() {
#ifndef _WIN32
	return " > /dev/null 2>&1";
#else
	return " > nul 2>&1";
#endif
}

inline
std::vector<char> stringDup(const char *src) {
	const size_t srcStrLen = strlen(src) + 1;
	std::vector<char> data(srcStrLen);
	strncpy(data.data(), src, srcStrLen);
	return data;
}

inline
std::string string2hex(const uint8_t *extradata, size_t extradataSize) {
	static const char* const ab = "0123456789ABCDEF";
	std::string output;
	output.reserve(2 * extradataSize);
	for (size_t i = 0; i < extradataSize; ++i) {
		auto const c = extradata[i];
		output.push_back(ab[c >> 4]);
		output.push_back(ab[c & 15]);
	}
	return output;
}

template <typename T>
int sign(T num) {
	return (T(0) < num) - (num < T(0));
}

template<typename T>
T divUp(T num, T divisor) {
	return (num + sign(num) * (divisor - 1)) / divisor;
}

template<typename T>
std::vector<T> makeVector() {
	return std::vector<T>();
}

template<typename T>
std::vector<T> makeVector(T val) {
	std::vector<T> r;
	r.push_back(val);
	return r;
}

template<typename T, typename... Arguments>
std::vector<T> makeVector(T val, Arguments... args) {
	std::vector<T> r;
	r.push_back(val);
	auto const tail = makeVector(args...);
	r.insert(r.end(), tail.begin(), tail.end());
	return r;
}

template<typename Lambda, typename T, typename... Arguments>
std::vector<T> apply(Lambda func, std::vector<T>& input, Arguments... args) {
	std::vector<T> r;
	for(auto& element : input)
		r.push_back(func(element, args...));
	return r;
}

template<typename T>
std::unique_ptr<T> uptr(T* p) {
	return std::unique_ptr<T>(p);
}

template<typename T, typename U>
std::shared_ptr<T> safe_cast(std::shared_ptr<U> p) {
	if (!p)
		return nullptr;
	auto r = std::dynamic_pointer_cast<T>(p);
	if (!r)
		throw std::runtime_error(format("dynamic cast error: could not convert from %s to %s", typeid(U).name(), typeid(T).name()).c_str());
	return r;
}

template<typename T, typename U>
T* safe_cast(U* p) {
	if (!p)
		return nullptr;
	auto r = dynamic_cast<T*>(p);
	if (!r)
		throw std::runtime_error(format("dynamic cast error: could not convert from %s to %s", typeid(U).name(), typeid(T).name()).c_str());
	return r;
}

template<typename T>
struct NotVoidStruct {
	typedef T Type;
};

template<>
struct NotVoidStruct<void> {
	typedef int Type;
};

template <typename T> using NotVoid = typename NotVoidStruct<T>::Type;

template<typename T>
inline constexpr T operator | (T a, T b) {
	return static_cast<T>(static_cast<int>(a) | static_cast<int>(b));
}

template<typename T>
inline constexpr T operator & (T a, T b) {
	return static_cast<T>(static_cast<int>(a) & static_cast<int>(b));
}
