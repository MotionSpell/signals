#pragma once

#include "../common/attributes.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp" // Warning, Debug

#include <chrono>
#include <thread>

namespace Modules {

class Regulator : public ModuleS {
  public:
  Regulator(KHost *host, std::shared_ptr<IClock> clock_)
      : clock(clock_)
      , m_host(host) {
    m_output = addOutput();
  }

  void processOne(Data data) override {
    auto const timeTarget = data->get<PresentationTime>().time;
    auto const timeNow = fractionToClock(clock->now());
    auto const delayInMs = clockToTimescale(timeTarget - timeNow, 1000);
    if(delayInMs > 0) {
      if(delayInMs > REGULATION_TOLERANCE_IN_MS)
        m_host->log(Warning, format("will sleep for %s ms", delayInMs).c_str());
      std::this_thread::sleep_for(std::chrono::milliseconds(delayInMs));
    } else if(delayInMs < -REGULATION_TOLERANCE_IN_MS) {
      if(abs(delayInMs) > abs(m_lastDelayInMs)) {
        char msg[256];
        sprintf(msg, "late data (%.2fs)", -delayInMs * 1000.0 / IClock::Rate);
        m_host->log(Warning, msg);
      }
    }
    m_lastDelayInMs = delayInMs;
    m_output->post(data);
  }

  std::shared_ptr<IClock> const clock;

  static auto const REGULATION_TOLERANCE_IN_MS = 300;

  private:
  KHost *const m_host;
  KOutput *m_output;
  int64_t m_lastDelayInMs = 0;
};
}
