#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <stdexcept>

struct Local {
	template<typename T>
	static std::string to_string(const std::vector<T>& val) {
		std::string ss;
		ss += "[";
		for (size_t i = 0; i < val.size(); ++i) {
			if (i > 0)
				ss += ", ";
			ss += to_string(val[i]);
		}
		ss += "]";
		return ss;
	}
	static std::string to_string(const char& c) {
		std::string s;
		return s + c;
	}
	static std::string to_string(const char* v) {
		return v;
	}
	static std::string to_string(char* v) {
		return v;
	}
	static std::string to_string(const std::string& v) {
		return v;
	}
	static std::string to_string(const void* v) {
		char buffer[128];
		snprintf(buffer, 128, "%p", v);
		return buffer;
	}
	static std::string to_string(double v) {
		char buffer[128];
		snprintf(buffer, 128, "%f", v);
		return buffer;
	}
	static std::string to_string(long unsigned v) {
		char buffer[128];
		snprintf(buffer, 128, "%lu", v);
		return buffer;
	}
	static std::string to_string(long v) {
		char buffer[128];
		snprintf(buffer, 128, "%ld", v);
		return buffer;
	}
	static std::string to_string(unsigned int v) {
		char buffer[128];
		snprintf(buffer, 128, "%u", v);
		return buffer;
	}
	static std::string to_string(int v) {
		char buffer[128];
		snprintf(buffer, 128, "%d", v);
		return buffer;
	}
	static std::string to_string(long long v) {
		char buffer[128];
		snprintf(buffer, 128, "%lld", v);
		return buffer;
	}
	static std::string to_string(long long unsigned v) {
		char buffer[128];
		snprintf(buffer, 128, "%llu", v);
		return buffer;
	}
};

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
				throw std::runtime_error("Invalid format specifier");

			if (fmt[i] == '%')
				r += '%';
			else if (fmt[i] == 's') {
				r += Local::to_string(firstArg);
				return r + format(fmt.substr(i + 1), args...);
			}
		} else {
			r += fmt[i];
		}
		++i;
	}
	return r;
}