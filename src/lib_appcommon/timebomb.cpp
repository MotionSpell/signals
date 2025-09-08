#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>

namespace {
inline time_t time_when_compiled() {
  // from: http://stackoverflow.com/questions/1765014/convert-string-from-date-into-a-time-t
  std::string datestr = __DATE__;
  std::string timestr = __TIME__;
  std::istringstream iss_date(datestr);
  std::string str_month;
  int day;
  int year;

  iss_date >> str_month >> day >> year;

  int month = 0;
  if(str_month == "Jan")
    month = 1;
  else if(str_month == "Feb")
    month = 2;
  else if(str_month == "Mar")
    month = 3;
  else if(str_month == "Apr")
    month = 4;
  else if(str_month == "May")
    month = 5;
  else if(str_month == "Jun")
    month = 6;
  else if(str_month == "Jul")
    month = 7;
  else if(str_month == "Aug")
    month = 8;
  else if(str_month == "Sep")
    month = 9;
  else if(str_month == "Oct")
    month = 10;
  else if(str_month == "Nov")
    month = 11;
  else if(str_month == "Dec")
    month = 12;
  else
    throw "Wrong month. Abort.";

  for(std::string::size_type pos = timestr.find(':'); pos != std::string::npos; pos = timestr.find(':', pos)) {
    timestr[pos] = ' ';
  }

  std::istringstream iss_time(timestr);
  int hour, min, sec;
  iss_time >> hour >> min >> sec;

  tm t;
  memset(&t, 0, sizeof(t));
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_year = year - 1900;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_sec = sec;

  return mktime(&t);
}
}

bool checkTimebomb(const double evaluation_period_in_days) {
  time_t current_time = time(NULL);
  time_t build_time = time_when_compiled();
  const double diff_time = difftime(current_time, build_time);
  const double evaluation_period = evaluation_period_in_days * 24.0 * 60.0 * 60.0; // in seconds
  if(diff_time > evaluation_period) {
    std::cerr << "Evaluation period has expired." << std::endl;
    return false;
  }
  return true;
}
