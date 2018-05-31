#pragma once

#include <cmath>
#include <cstring>
#include <iostream>
#include <iomanip>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#define FORMAT(i, max) std::setw(1+(std::streamsize)log10(max)) << i

namespace Tools {

class Profiler {
	public:
		Profiler(const std::string &name, std::ostream &os = std::cout) : name(name), os(os) {
#ifdef _WIN32
			QueryPerformanceCounter(&startTime);
#else
			gettimeofday(&startTime, nullptr);
#endif
		}

		~Profiler() {
			os << "[" << name.c_str() << "] " << FORMAT(elapsedInUs(), maxDurationInSec*unit) << " us" << std::endl;
		}

		uint64_t elapsedInUs() {
#ifdef _WIN32
			LARGE_INTEGER stopTime;
			QueryPerformanceCounter(&stopTime);
			LARGE_INTEGER countsPerSecond;
			QueryPerformanceFrequency(&countsPerSecond);
			return (uint64_t)((unit * (stopTime.QuadPart - startTime.QuadPart)) / countsPerSecond.QuadPart);
#else
			struct timeval stopTime;
			gettimeofday(&stopTime, nullptr);
			return ((uint64_t)stopTime.tv_sec * 1000000 + stopTime.tv_usec) - ((uint64_t)startTime.tv_sec * 1000000 + startTime.tv_usec);
#endif
		}

	private:
		Profiler& operator= (const Profiler&) = delete;

		std::string name;
#ifdef _WIN32
		LARGE_INTEGER startTime;
#else
		struct timeval startTime;
#endif
		const int unit = 1000000;
		const int maxDurationInSec = 100;
		std::ostream &os;
};

}
