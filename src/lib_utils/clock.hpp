#pragma once

#include "fraction.hpp"

struct IClock {
	virtual ~IClock() = default;
	static auto const Rate = 180000LL;
	virtual Fraction now() const = 0;
};

template<typename T>
static T rescale(T time, uint64_t srcScale, uint64_t dstScale) {
	simplifyFraction(srcScale, dstScale);
	return divUp<T>(time * dstScale, srcScale);
}

template<typename T>
static T timescaleToClock(T time, uint64_t timescale) {
	return rescale(time, timescale, IClock::Rate);
}

template<typename T>
static T clockToTimescale(T time, uint64_t timescale) {
	return rescale(time, IClock::Rate, timescale);
}

static inline int64_t fractionToTimescale(Fraction time, uint64_t timescale) {
	return rescale(time.num, time.den, timescale);
}

static inline int64_t fractionToClock(Fraction time) {
	return timescaleToClock(time.num, time.den);
}

