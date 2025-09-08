#include "lib_utils/clock.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log.hpp"
#include "lib_utils/tools.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "libav.hpp"
#include "pcm.hpp"
#include "picture_allocator.hpp"

namespace {

Level avLogLevel(int level) {
  switch(level) {
  case AV_LOG_QUIET:
  case AV_LOG_PANIC:
  case AV_LOG_FATAL:
    return Error;
  case AV_LOG_ERROR:
  case AV_LOG_WARNING:
    return Warning;
  case AV_LOG_INFO:
    return Info;
  case AV_LOG_VERBOSE:
    return Debug;
  case AV_LOG_DEBUG:
  case AV_LOG_TRACE:
    return Quiet;
  default:
    assert(0);
    return Debug;
  }
}

const char *avlogLevelName(int level) {
  switch(level) {
  case AV_LOG_QUIET:
    return "quiet";
  case AV_LOG_PANIC:
    return "panic";
  case AV_LOG_FATAL:
    return "fatal";
  case AV_LOG_ERROR:
    return "error";
  case AV_LOG_WARNING:
    return "warning";
  case AV_LOG_INFO:
    return "info";
  case AV_LOG_VERBOSE:
    return "verbose";
  case AV_LOG_DEBUG:
    return "debug";
  case AV_LOG_TRACE:
    return "trace";
  default:
    assert(0);
    return "unknown";
  }
}

// cygwin does not have vsnprintf in std=c++11 mode.
// To be removed when cygwin is fixed
#if defined(__CYGWIN__)
void vsnprintf(char *buffer, size_t size, const char *fmt, va_list vl) { strncpy(buffer, size, fmt); }
#endif

void avLog(void *avcl, int level, const char *fmt, va_list vl) {
  char buffer[1280];
  vsnprintf(buffer, sizeof(buffer) - 1, fmt, vl);

  // remove trailing end of line
  {
    auto const N = strlen(buffer);
    if(N > 0 && buffer[N - 1] == '\n')
      buffer[N - 1] = 0;
  }

  AVClass *avc = avcl ? *(AVClass **)avcl : NULL;
  if(avc) {
    g_Log->log(avLogLevel(level),
          format("[libav-log::%s] [%s @ %s] %s", avlogLevelName(level), avc->item_name(avcl), avcl, buffer).c_str());
  } else {
    g_Log->log(avLogLevel(level), format("[libav-log::%s] %s", avlogLevelName(level), buffer).c_str());
  }
}

int do_ffmpeg_static_initialization() {
  av_log_set_callback(&avLog);
  return 0;
}

auto g_InitFfmpeg = do_ffmpeg_static_initialization();

}
