#include "log.hpp"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <string>
#include <stdexcept>

#ifdef _WIN32
void setGlobalLogSyslog(const char */*ident*/, const char* /*channel_name*/) {
	throw std::runtime_error("Syslog is not supported on this platform");
}
#else

#include <syslog.h>

struct SyslogLogger : LogSink {
	SyslogLogger(const char *ident, const char *channel_name): identLog(ident), channel_name(channel_name) {
		openlog(identLog.c_str(), LOG_PID, LOG_USER|LOG_SYSLOG);
	}
	~SyslogLogger() {
		closelog();
	}
	void send(Level level, const char* msg) override {
		static const int levelToSysLog[] = {LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG};

		rapidjson::StringBuffer s;
		rapidjson::Writer<rapidjson::StringBuffer> writer(s);
		writer.StartObject();
		writer.Key("message");
		writer.String(msg);
		writer.Key("tabatha_channel_name");
		writer.String(channel_name.c_str());
		writer.EndObject();

		::syslog(levelToSysLog[level], "%s", s.GetString());
	}
	const std::string identLog; // must exist throughtout the object lifetime
	const std::string channel_name;
};

void setGlobalLogSyslog(const char *ident, const char *channel_name) {
	static SyslogLogger syslogLogger(ident, channel_name);
	g_Log = &syslogLogger;
}

#endif
