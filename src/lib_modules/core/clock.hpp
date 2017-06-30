#pragma once

#include "lib_utils/tools.hpp"
#include <chrono>
#include <cstdint>

namespace Modules {

struct IClock {
	static auto const Rate = 180000ULL;
	virtual uint64_t now() const = 0;
	virtual double getSpeed() const = 0;
};

class Clock : public IClock {
public:
	Clock(double speed) : timeStart(std::chrono::high_resolution_clock::now()), speed(speed) {}
	uint64_t now() const override;
	double getSpeed() const override;

private:
	std::chrono::time_point<std::chrono::high_resolution_clock> const timeStart;
	double const speed;
};

extern IClock* const g_DefaultClock;

template<typename T>
static T convertToTimescale(T time, uint64_t timescaleSrc, uint64_t timescaleDst) {
	return divUp<T>(time * timescaleDst, timescaleSrc);
}

template<typename T>
static T timescaleToClock(T time, uint64_t timescale) {
	return convertToTimescale(time, timescale, IClock::Rate);
}

template<typename T>
static T clockToTimescale(T time, uint64_t timescale) {
	return convertToTimescale(time, IClock::Rate, timescale);
}

}
