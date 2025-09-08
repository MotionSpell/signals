// A generic stream that receives data
// corresponding to one single PID.
#pragma once

#include <cassert>

struct Stream {
  Stream(int pid_, KHost *host)
      : pid(pid_)
      , m_host(host) {}
  virtual ~Stream() = default;

  // send data for processing
  virtual void push(SpanC data, bool pusi) = 0;

  // tell the stream when the payload unit is finished (e.g PUSI=1 or EOS)
  virtual void flush() = 0;

  // handle error cases by discarding pending data
  // returns false when the operation is void
  virtual bool reset() { return false; };

  int pid = TsDemuxerConfig::ANY;
  KHost *const m_host; // for logs
  int cc = -1; // continuity counter
  bool rap = false;
};

// Helper class for stream implementations
// Keep it inlined.
struct BitReader {
  SpanC src;

  int u(int n) {
    const int firstByte = m_pos / 8;
    const int lastByte = (m_pos + n - 1) / 8;
    m_pos += n;

    uint64_t acc = 0;

    for(int k = firstByte; k <= lastByte; ++k) {
      acc <<= 8;
      acc |= src[k];
    }

    auto mask = ((1u << n) - 1);
    auto shift = m_pos % 8 ? 8 - m_pos % 8 : 0;
    return (acc >> shift) & mask;
  }

  int byteOffset() const {
    assert(m_pos % 8 == 0);
    return m_pos / 8;
  }

  int remaining() const { return (int)src.len - m_pos / 8; }

  SpanC payload() const {
    assert(m_pos % 8 == 0);
    auto r = src;
    r += m_pos / 8;
    return r;
  }

  bool empty() const { return size_t(m_pos / 8) >= src.len; }

  int m_pos = 0;
};
