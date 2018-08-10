#pragma once

#include <string>
#include <cstdint>
#include <sstream>
#include <stdexcept>

template<typename T>
std::string toString(T const& val) {
	std::stringstream ss;
	ss << val;
	return ss.str();
}

inline std::string toString(uint8_t val) {
	return std::to_string((int)val);
}

template<typename T, typename MustBeIterable = decltype(T::begin())>
std::string toString(T const& val) {
	std::string ss;
	ss += "[";
	for (size_t i = 0; i < val.size(); ++i) {
		if (i > 0)
			ss += ", ";
		ss += toString(val[i]);
	}
	ss += "]";
	return ss;
}

inline std::string format(const std::string& format) {
	return format;
}

template<typename T, typename... Arguments>
std::string format(const std::string& fmt, const T& firstArg, Arguments... args) {
	std::string r;
	size_t i = 0;
	while (i < fmt.size()) {
		if (fmt[i] == '%') {
			++i;
			if (i >= fmt.size())
				throw std::runtime_error("Invalid fmt specifier");

			if (fmt[i] == '%')
				r += '%';
			else if (fmt[i] == 's') {
				r += toString(firstArg);
				return r + format(fmt.substr(i + 1), args...);
			}
		} else {
			r += fmt[i];
		}
		++i;
	}
	return r;
}
