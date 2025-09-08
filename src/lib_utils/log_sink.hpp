#pragma once

enum Level { Quiet = -1, Error = 0, Warning, Info, Debug };

struct LogSink {
  void log(Level level, const char *msg) {
    if((level != Quiet) && (level <= m_logLevel))
      send(level, msg);
  }

  void setLevel(Level level) { m_logLevel = level; }

  Level m_logLevel = Warning;

  private:
  virtual void send(Level level, const char *msg) = 0;
};
