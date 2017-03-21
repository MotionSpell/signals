#include "time.hpp"
#include <cstdio>
#include <ctime>
#include <iostream>

#define NTP_SEC_1900_TO_1970 2208988800ul

#ifdef _WIN32 
#include <sys/timeb.h>
#include <Winsock2.h>
int gettimeofday(struct timeval *tp, void *tz) {
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

uint64_t getUTCInMs() {
	uint64_t current_time;
	double msec;
	uint32_t sec, frac;
	getNTP(&sec, &frac);
	current_time = sec - NTP_SEC_1900_TO_1970;
	current_time *= 1000;
	msec = (frac*1000.0) / 0xFFFFFFFF;
	current_time += (uint64_t)msec;
	return current_time;
}

uint64_t UTC2NTP(uint64_t absTimeUTCInMs) {
	const uint64_t sec = NTP_SEC_1900_TO_1970 + absTimeUTCInMs / 1000;
	const uint64_t msec = absTimeUTCInMs - 1000 * (absTimeUTCInMs / 1000);
	const uint64_t frac = (msec * 0xFFFFFFFF) / 1000;
	return (sec << 32) + frac;
}

void timeInMsToStr(uint64_t timestamp, char buffer[24], const char *msSeparator) {
	uint64_t p = timestamp;
	uint64_t h = (uint64_t)(p / 3600000);
	uint8_t m = (uint8_t)(p / 60000 - 60 * h);
	uint8_t s = (uint8_t)(p / 1000 - 3600 * h - 60 * m);
	uint16_t u = (uint16_t)(p - 3600000 * h - 60000 * m - 1000 * s);
	sprintf(buffer, "%02u:%02u:%02u%s%03u", (unsigned)h, (unsigned)m, (unsigned)s, msSeparator, (unsigned)u);
}

std::string getDay() {
	char day[255];
	const std::time_t t = std::time(nullptr);
	std::tm tm = *std::localtime(&t);
	auto const size = strftime(day, 255, "%a %b %d %Y", &tm);
	return day;
}

std::string getTime() {
	char time[24];
	auto const t = getUTCInMs();
	timeInMsToStr(((t / 3600000) % 24) * 3600000 + (t % 3600000), time);
	return time;
}
