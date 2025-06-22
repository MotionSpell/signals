#include "tests/tests.hpp"
#include "../time.hpp"

namespace Tests {
template<>
inline std::string ToString<Fraction>(Fraction const& val) {
	std::stringstream ss;
	ss << val.num << "/" << val.den;
	return ss.str();
}
}

unittest("time: parseDate") {
	ASSERT_EQUALS(parseDate("1970-01-01T00:00:00Z"), 0);
	ASSERT_EQUALS(parseDate("1970-01-01T00:00:00.000Z"), 0);
	ASSERT_EQUALS(parseDate("1970-01-01T00:00:00.500Z"), Fraction(500, 1000));
	ASSERT_EQUALS(parseDate("1970-01-01T00:00:00.0Z"), 0);
	ASSERT_EQUALS(parseDate("1970-01-01T00:00:00.0-02:00"), 2*3600);
	ASSERT_EQUALS(parseDate("1970-01-01T02:00:00+02:00"), 0);

	//check timezone wrap up
	ASSERT_EQUALS(parseDate("1970-01-02T00:00:00+02:00"), 22*3600);

	ASSERT_THROWN(parseDate(""));
	ASSERT_THROWN(parseDate("1970-01-01"));
	ASSERT_THROWN(parseDate("19700-01-01T02:00:00+02"));
	ASSERT_THROWN(parseDate("19700-01-01T02:00:00+02:00"));
	ASSERT_THROWN(parseDate("1970-010-01T02:00:00+02:00"));
	ASSERT_THROWN(parseDate("1970-01-010T02:00:00+02:00"));
	ASSERT_THROWN(parseDate("1970-01-01T020:00:00+02:00"));
	ASSERT_THROWN(parseDate("1970-01-01T02:000:00+02:00"));
	ASSERT_THROWN(parseDate("1970-01-01T02:00:00+020:00"));
}
