#pragma once

#include <cstdint>
#include <string>

inline
int64_t pgcd(int64_t a, int64_t b) {
	return b ? pgcd(b, a%b) : a;
}


template<typename T>
static void simplifyFraction(T& num, T& den) {
	// save sign and make num and den positive
	bool positive = true;
	if(num < 0) {
		num = -num;
		positive = !positive;
	}
	if(den < 0) {
		den = -den;
		positive = !positive;
	}

	auto const gcd = pgcd(num, den);
	num /= gcd;
	den /= gcd;

	// restore sign
	if(!positive)
		num = -num;
}

template<>
inline void simplifyFraction<uint64_t>(uint64_t& num, uint64_t& den) {
	auto const gcd = pgcd(num, den);
	num /= gcd;
	den /= gcd;
}

struct Fraction {
	Fraction(int64_t num = 1, int64_t den = 1) : num(num), den(den) {
		if(!den)
			return; // invalid value
		simplifyFraction(this->num, this->den);
	}
	template<typename T>
	inline explicit operator T() const {
		return (T)num / (T)den;
	}
	inline Fraction operator+(const Fraction &frac) const {
		auto newNum = num * frac.den + frac.num * den;
		auto newDen = den * frac.den;
		simplifyFraction(newNum, newDen);
		return Fraction(newNum, newDen);
	}
	inline Fraction operator-(const Fraction &frac) const {
		auto newNum = num * frac.den - frac.num * den;
		auto newDen = den * frac.den;
		simplifyFraction(newNum, newDen);
		return Fraction(newNum, newDen);
	}
	inline Fraction operator*(const Fraction &frac) const {
		auto newNum = num * frac.num;
		auto newDen = den * frac.den;
		simplifyFraction(newNum, newDen);
		return Fraction(newNum, newDen);
	}
	inline Fraction operator/(const Fraction &frac) const {
		auto newNum = num * frac.den;
		auto newDen = den * frac.num;
		simplifyFraction(newNum, newDen);
		return Fraction(newNum, newDen);
	}
	inline bool operator==(const Fraction& rhs) const {
		return num * rhs.den == rhs.num * den;
	}
	inline bool operator!=(const Fraction& rhs) const {
		return !(rhs == *this);
	}
	template <typename T>
	inline bool operator==(const T& rhs) const {
		return *this == Fraction(rhs);
	}
	inline bool operator<(const Fraction& rhs) const {
		return num * rhs.den < rhs.num * den;
	}
	inline bool operator>(const Fraction& rhs) const {
		return num * rhs.den > rhs.num * den;
	}
	inline bool operator<=(const Fraction& rhs) const {
		return !(*this > rhs);
	}
	inline bool operator>=(const Fraction& rhs) const {
		return !(*this < rhs);
	}
	Fraction inverse() const {
		return Fraction(den, num);
	}
	static std::string to_string(const Fraction &v) {
		char buffer[128];
		sprintf(buffer, "%lld/%lld", v.num, v.den);
		return buffer;
	}

	int64_t num; // holds the sign
	int64_t den; // should always be kept positive
};

template <typename T>
constexpr
int sign(T num) {
	return (T(0) < num) - (num < T(0));
}

template<typename T>
T divUp(T num, T divisor) {
	return (num + sign(num) * (divisor - 1)) / divisor;
}
