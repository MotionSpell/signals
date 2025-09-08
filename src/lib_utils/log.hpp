#pragma once

#include "log_sink.hpp"

extern LogSink *g_Log;

void setGlobalLogSyslog(const char *ident, const char *channel_name);
void setGlobalLogConsole(bool color_enable);
void setGlobalLogCSV(const char *path);
void setGlobalLogger(LogSink &logger);

Level getGlobalLogLevel();
void setGlobalLogLevel(Level level);

Level parseLogLevel(const char *slevel);
