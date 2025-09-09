#pragma once

#include <cstdint>
#include <cstdlib> // for std::abs

struct TimeUnwrapper {
  int64_t unwrap(int64_t time) {
    if(!m_init) {
      m_init = true;
      m_when = time;
    }

    const int64_t candidates[] = {
          (m_when / WRAP_PERIOD) * WRAP_PERIOD + time - WRAP_PERIOD,
          (m_when / WRAP_PERIOD) * WRAP_PERIOD + time,
          (m_when / WRAP_PERIOD) * WRAP_PERIOD + time + WRAP_PERIOD,
    };

    int64_t best = INT64_MAX;
    int64_t bestDist = INT64_MAX;
    for(auto cand : candidates) {
      auto dist = std::abs(cand - m_when);
      if(dist < bestDist) {
        bestDist = dist;
        best = cand;
      }
    }

    time = best;

    if(time > m_when)
      m_when = time;

    return time;
  }

  bool m_init = false;
  int64_t m_when = 0;

  int64_t WRAP_PERIOD = 1LL << 33;
};
