#include "time.hpp"
#include "format.hpp"
#include <cassert>
#include <cstdio>
#include <ctime>

#define NTP_SEC_1900_TO_1970 2208988800ul

#ifdef _WIN32
#include <sys/timeb.h>
#include <winsock2.h>
int gettimeofday(struct timeval *tp, void * /*tz*/) {
	struct _timeb timebuffer;
	_ftime(&timebuffer);
	tp->tv_sec = (long)(timebuffer.time);
	tp->tv_usec = timebuffer.millitm * 1000;
	return 0;
}
#else
#include <sys/time.h>
#endif

static void getNTP(uint32_t *sec, uint32_t *frac) {
	uint64_t frac_part;
	struct timeval now;
	gettimeofday(&now, nullptr);
	if (sec) {
		*sec = (uint32_t)(now.tv_sec) + NTP_SEC_1900_TO_1970;
	}
	if (frac) {
		frac_part = now.tv_usec * 0xFFFFFFFFULL;
		frac_part /= 1000000;
		*frac = (uint32_t)(frac_part);
	}
}

Fraction getUTC() {
	const uint64_t unit = 1000;
	uint32_t sec, frac;
	getNTP(&sec, &frac);
	uint64_t currentTime = sec - NTP_SEC_1900_TO_1970;
	currentTime *= unit;
	double msec = (frac*double(unit)) / 0xFFFFFFFF;
	currentTime += (uint64_t)msec;
	return Fraction(currentTime, unit);
}

uint64_t UTC2NTP(uint64_t absTimeUTCInMs) {
	const uint64_t sec = NTP_SEC_1900_TO_1970 + absTimeUTCInMs / 1000;
	const uint64_t msec = absTimeUTCInMs - 1000 * (absTimeUTCInMs / 1000);
	const uint64_t frac = (msec * 0xFFFFFFFF) / 1000;
	return (sec << 32) + frac;
}

void timeInMsToStr(uint64_t timestamp, char buffer[24], const char *msSeparator) {
	const uint64_t p = timestamp;
	const uint64_t h = (uint64_t)(p / 3600000);
	const uint8_t m = (uint8_t)(p / 60000 - 60 * h);
	const uint8_t s = (uint8_t)(p / 1000 - 3600 * h - 60 * m);
	const uint16_t u = (uint16_t)(p - 3600000 * h - 60000 * m - 1000 * s);
	auto const len = snprintf(buffer, 24, "%02d:%02d:%02d%s%03d", (int)h, (int)m, (int)s, msSeparator, (int)u);
	if (len < 0 || len >= 24)
		throw std::runtime_error(format("Failure in formatting in timeInMsToStr() (len=%s, max=%s)", len, 24));
}

std::string getDay() {
	char day[255];
	const std::time_t t = std::time(nullptr);
	std::tm tm = *std::gmtime(&t);
	strftime(day, 255, "%a %b %d %Y", &tm);
	return day;
}

std::string getTimeFromUTC() {
	char time[24];
	auto const t = int64_t(getUTC() * 1000);
	timeInMsToStr(((t / 3600000) % 24) * 3600000 + (t % 3600000), time);
	return time;
}

// portable 'timegm'
static time_t pTimegm(struct tm * t) {
	auto const MONTHSPERYEAR = 12;
	static const int cumulatedDays[MONTHSPERYEAR] =
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

	long year = 1900 + t->tm_year + t->tm_mon / MONTHSPERYEAR;
	time_t r = (year - 1970) * 365 + cumulatedDays[t->tm_mon % MONTHSPERYEAR];
	r += (year - 1968) / 4;
	r -= (year - 1900) / 100;
	r += (year - 1600) / 400;
	if ((year % 4) == 0
	    && ((year % 100) != 0 || (year % 400) == 0)
	    && (t->tm_mon % MONTHSPERYEAR) < 2)
		r--;
	r += t->tm_mday - 1;
	r *= 24;
	r += t->tm_hour;
	r *= 60;
	r += t->tm_min;
	r *= 60;
	r += t->tm_sec;

	if (t->tm_isdst == 1)
		r -= 3600;

	return r;
}

int64_t parseDate(std::string s) {
	int year, month, day, hour, minute, second;
	int ret = sscanf(s.c_str(), "%04d-%02d-%02dT%02d:%02d:%02d",
	    &year,
	    &month,
	    &day,
	    &hour,
	    &minute,
	    &second);
	if(ret != 6)
		throw std::runtime_error("Invalid date '" + s + "'");

	tm date {};
	date.tm_year = year - 1900;
	date.tm_mon = month - 1;
	date.tm_mday = day;
	date.tm_hour = hour;
	date.tm_min = minute;
	date.tm_sec = second;

	return pTimegm(&date);
}

struct UtcClock : IUtcClock {
	Fraction getTime() override {
		return getUTC();
	}
};

static UtcClock utcClock;
IUtcClock* g_UtcClock = &utcClock;
