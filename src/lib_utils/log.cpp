#include "log.hpp"
#include "clock.hpp"
#include <cassert>
#include <ctime>
#include <iostream>
#include "system_clock.hpp"
#include "format.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincon.h>
static HANDLE console = NULL;
static WORD console_attr_ori = 0;

#else /*_WIN32*/

#define RED    "\x1b[31m"
#define YELLOW "\x1b[33m"
#define GREEN  "\x1b[32m"
#define CYAN   "\x1b[36m"
#define WHITE  "\x1b[37m"
#define RESET  "\x1b[0m"
#endif /*_WIN32*/

static std::ostream& get(Level level) {
	switch (level) {
	case Info:
		return std::cout;
	default:
		return std::cerr;
	}
}

static std::string getTime() {
	char szOut[255];
	const std::time_t t = std::time(nullptr);
	const std::tm tm = *std::gmtime(&t);
	auto const size = strftime(szOut, sizeof szOut, "%Y/%m/%d %H:%M:%S", &tm);
	auto timeString = std::string(szOut, size);
	auto const now = (double)g_SystemClock->now();
	snprintf(szOut, sizeof szOut, "[%s][%.1f]", timeString.c_str(), now);
	return szOut;
}

struct ConsoleLogger : LogSink {
	void log(Level level, const char* msg) override {
		if ((level != Quiet) && (level <= m_logLevel))
			send(level, msg);
	}

	void setLevel(Level level) {
		m_logLevel = level;
	}

	void setColor(bool isColored) {
		m_color = isColored;
	}

	Level m_logLevel = Warning;
	bool m_color = true;
	bool m_syslog = false;

	std::string getColorBegin(Level level) {
		if (!m_color) return "";
#ifdef _WIN32
		if (console == NULL) {
			CONSOLE_SCREEN_BUFFER_INFO console_info;
			console = GetStdHandle(STD_ERROR_HANDLE);
			assert(console != INVALID_HANDLE_VALUE);
			if (console != INVALID_HANDLE_VALUE) {
				GetConsoleScreenBufferInfo(console, &console_info);
				console_attr_ori = console_info.wAttributes;
			}
		}
		switch (level) {
		case Error: SetConsoleTextAttribute(console, FOREGROUND_RED | FOREGROUND_INTENSITY); break;
		case Warning: SetConsoleTextAttribute(console, FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN); break;
		case Info: SetConsoleTextAttribute(console, FOREGROUND_INTENSITY | FOREGROUND_GREEN); break;
		case Debug: SetConsoleTextAttribute(console, FOREGROUND_GREEN); break;
		default: break;
		}
#else
		switch (level) {
		case Error: fprintf(stderr, RED); break;
		case Warning: fprintf(stderr, YELLOW); break;
		case Info: fprintf(stderr, GREEN); break;
		case Debug: fprintf(stderr, CYAN); break;
		default: break;
		}
#endif
		return "";
	}

	std::string getColorEnd(Level /*level*/) {
		if (!m_color) return "";
#ifdef _WIN32
		SetConsoleTextAttribute(console, console_attr_ori);
#else
		fprintf(stderr, RESET);
#endif
		return "";
	}

	void send(Level level, const char* msg) {
		get(level) << getColorBegin(level) << getTime() << " " << msg << getColorEnd(level) << std::endl;
	}
};

static ConsoleLogger consoleLogger;

#ifdef _WIN32
void setGlobalSyslog(bool enable) {
	if(enable)
		throw std::runtime_error("Syslog is not supported on this platform");
}
#else

#include <syslog.h>
struct SyslogLogger : LogSink {
	SyslogLogger() {
		openlog(nullptr, 0, LOG_USER);
	}
	~SyslogLogger() {
		closelog();
	}
	void log(Level level, const char* msg) override {
		static const int levelToSysLog[] = { 3, 4, 6, 7 };
		::syslog(levelToSysLog[level], "%s", msg);
	}
};

void setGlobalSyslog(bool enable) {
	static SyslogLogger syslogLogger;
	if(enable)
		g_Log = &syslogLogger;
	else
		g_Log = &consoleLogger;
}

#endif

struct CsvLogger : LogSink {
	CsvLogger(const char* path) : m_fp(fopen(path, "w")) {
		if(!m_fp)
			throw std::runtime_error("Can't open '" + std::string(path) + "' for writing");
	}
	~CsvLogger() {
		fclose(m_fp);
	}
	void log(Level level, const char* msg) override {
		fprintf(m_fp, "%d, \"%s\", \"%s\"\n", level, getTime().c_str(), msg);
	}
	FILE* const m_fp;
};

static
LogSink* getDefaultLogger() {
	if(auto path = std::getenv("SIGNALS_LOGPATH")) {
		static CsvLogger csvLogger(path);
		return &csvLogger;
	}
	return &consoleLogger;
}

LogSink* g_Log = getDefaultLogger();

Level getGlobalLogLevel() {
	return consoleLogger.m_logLevel;
}

void setGlobalLogLevel(Level level) {
	consoleLogger.setLevel(level);
}

void setGlobalLogColor(bool enable) {
	consoleLogger.setColor(enable);
}

