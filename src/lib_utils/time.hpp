#pragma once

#include <cstdint>
#include <string>

#include "fraction.hpp"

Fraction getUTC();
uint64_t UTC2NTP(uint64_t absTimeUTCInMs);
void timeInMsToStr(uint64_t timestamp, char buffer[24], const char *msSeparator = ",");
std::string getDay();
std::string getTimeFromUTC();
Fraction parseDate(std::string s); // "2019-03-04T15:32:17", "2019-03-04T15:32:17.500Z", "2019-03-04T15:32:17.02+02:00"

struct IUtcClock {
  virtual Fraction getTime() = 0;
};

extern IUtcClock *g_UtcClock;
