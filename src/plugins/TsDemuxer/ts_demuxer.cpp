// MPEG-TS push demuxer.
//
// The design goal here is to discard the biggest amount of input
// data (which is often erroneous) while still getting the job done.
//
#include "ts_demuxer.hpp"

#include "lib_media/common/metadata.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp" // Error
#include "lib_utils/time_unwrapper.hpp"
#include "lib_utils/tools.hpp" // enforce

#include <vector>

using namespace std;
using namespace Modules;

#include "pes_stream.hpp"
#include "psi_stream.hpp"
#include "stream.hpp"

namespace {

auto const PTS_PERIOD = 1LL << 33;
auto const TS_PACKET_LEN = 188;
auto const PID_PAT = 0;
auto const MAX_PID = 8192;

struct TsDemuxer : ModuleS, PsiStream::Listener, PesStream::IRestamper {
  TsDemuxer(KHost *host, TsDemuxerConfig const &config)
      : m_host(host)
      , m_needsRestamp(config.timestampStartsAtZero) {
    m_unwrapper.WRAP_PERIOD = PTS_PERIOD;
    m_streams[PID_PAT] = make_unique<PsiStream>(PID_PAT, m_host, this);

    for(auto &pid : config.pids)
      if(pid.type != TsDemuxerConfig::NONE) {
        auto pess = make_unique<PesStream>(pid.pid, pid.type, this, m_host, addOutput());
        if(pid.pid == TsDemuxerConfig::ANY)
          m_streamsPending.push_back(std::move(pess));
        else
          m_streams[pid.pid] = std::move(pess);
      }
  }

  void processOne(Data data) override {
    auto buf = data->data();
    processRemainder(buf);
    processSpan(buf);
  }

  void processSpan(SpanC &buf) {
    bool syncing = true;
    auto syncFound = [&]() {
      if(*buf.ptr == SYNC_BYTE) {
        syncing = false;
        return true;
      }
      if(!syncing) {
        m_host->log(Warning, "Looking for sync byte");
        syncing = true;
      }
      buf += 1;
      return false;
    };

    while(buf.len > 0) {
      if(!syncFound())
        continue;

      if(buf.len < TS_PACKET_LEN) {
        m_host->log(Debug, "Truncated TS packet");
        assert(m_remainderSize == 0);
        memcpy(m_remainder, buf.ptr, buf.len);
        m_remainderSize += buf.len;
        return;
      }

      try {
        processTsPacket({buf.ptr, TS_PACKET_LEN});
        syncing = false;
      } catch(exception const &e) {
        m_host->log(Error, e.what());
      }
      buf += TS_PACKET_LEN;
    }
  }

  void processRemainder(SpanC &buf) {
    if(!m_remainderSize)
      return;

    assert(m_remainderSize < TS_PACKET_LEN);
    assert(m_remainder[0] == SYNC_BYTE);

    for(;;) {
      m_remainder[m_remainderSize] = *buf.ptr;
      m_remainderSize++;
      buf += 1;

      if(m_remainderSize == TS_PACKET_LEN)
        break;

      if(buf.len == 0)
        return; // early exit if remainder + data < TS_PACKET_LEN
    }

    assert(m_remainderSize == TS_PACKET_LEN);
    SpanC remBuf{m_remainder, m_remainderSize};
    processSpan(remBuf);
    m_remainderSize = 0;
  }

  void flush() override {
    if(m_remainderSize > 0) {
      m_host->log(Warning, format("Discarding %s remaining bytes", m_remainderSize).c_str());
      m_remainderSize = 0;
    }

    for(int i = 0; i < MAX_PID; ++i)
      if(m_streams[i])
        m_streams[i]->flush();
  }

  // PsiStream::Listener implementation
  void onPat(span<int> pmtPids) override {
    m_host->log(Debug, format("Found PAT (%s programs)", pmtPids.len).c_str());
    for(auto pid : pmtPids)
      m_streams[pid] = make_unique<PsiStream>(pid, m_host, this);
  }

  void onPmt(span<PsiStream::EsInfo> esInfo) override {
    m_host->log(Debug, format("Found PMT (%s streams)", esInfo.len).c_str());
    for(auto es : esInfo) {
      if(auto stream = findMatchingStream(es)) {
        stream->pid = es.pid;
        if(stream->setType(es.mpegStreamType))
          m_host->log(Debug, format("[%s] MPEG stream type %s", es.pid, es.mpegStreamType).c_str());
        else
          m_host->log(Warning, format("[%s] unknown MPEG stream type: %s", es.pid, es.mpegStreamType).c_str());
      }
    }
  }

  // PesStream::IRestamper implementation
  void restamp(int64_t &pts) override {
    pts = m_unwrapper.unwrap(pts);

    // make the timestamp start from zero
    if(m_needsRestamp) {
      if(m_ptsOrigin == INT64_MAX)
        m_ptsOrigin = pts;

      pts -= m_ptsOrigin;
    }
  }

  private:
  void processTsPacket(const SpanC pkt) {
    BitReader r = {pkt};
    const int syncByte = r.u(8);
    (void)syncByte;
    assert(syncByte == SYNC_BYTE);
    const int transportErrorIndicator = r.u(1);
    const int payloadUnitStartIndicator = r.u(1);
    /*const int priority =*/r.u(1);
    const int packetId = r.u(13);
    const int scrambling = r.u(2);
    const int adaptationFieldControl = r.u(2);
    const int continuityCounter = r.u(4);

    if(packetId == 0x1FFF)
      return; // null packet

    auto stream = m_streams[packetId].get();
    if(!stream)
      return; // we're not interested in this PID

    // skip adaptation field if any
    if(adaptationFieldControl & 0b10) {
      auto length = r.u(8);
      if(length > 0) {
        /*const int discontinuity_indicator =*/r.u(1);
        stream->rap |= r.u(1);
        r.u(6);
        skip(r, length - 1, "adaptation_field length in TS header");
      }
    }

    if(stream->cc == -1)
      stream->cc = (continuityCounter + 15) % 16; // init

    if(transportErrorIndicator) {
      m_host->log(Error, format("[%s] Discarding TS packet with TEI=1", packetId).c_str());
      return;
    }

    if(scrambling) {
      m_host->log(Error, format("[%s] Discarding scrambled TS packet", packetId).c_str());
      return;
    }

    // AF: discontinuity_indicator:
    // After a continuity counter discontinuity in a transport packet which is designated as containing elementary
    // stream data, the first byte of elementary stream data in a transport stream packet of the same PID shall be the
    // first byte of an elementary stream access point.

    if(adaptationFieldControl & 0b01) {
      // TODO: In transport streams, duplicate packets may be sent as two, and only two, consecutive transport stream
      // packets of the same PID.
      if(continuityCounter == stream->cc) {
        m_host->log(Debug, format("[%s] Discarding duplicated packet (cc=%s)", packetId, continuityCounter).c_str());
        return;
      }

      if(continuityCounter != (stream->cc + 1) % 16)
        if(stream->reset())
          m_host->log(Warning,
                format("[%s] Discontinuity detected (curr_cc=%s, prev_cc=%s). Flushing and discarding until next PUSI.",
                      packetId, continuityCounter, stream->cc)
                      .c_str());
      // TODO: don't repeat until PUSI
    } else if(continuityCounter != stream->cc) {
      m_host->log(Warning,
            format("[%s] continuity_counter (curr=%s, prev=%s) shall not be incremented when the "
                   "adaptation_field_control(%s) of the packet equals '00' or '10'.",
                  packetId, continuityCounter, stream->cc, adaptationFieldControl)
                  .c_str());
    }

    stream->cc = continuityCounter;

    if(payloadUnitStartIndicator)
      stream->flush();

    if(adaptationFieldControl & 0b01)
      stream->push(r.payload(), payloadUnitStartIndicator);
  }

  PesStream *findMatchingStream(PsiStream::EsInfo es) {
    if(!m_streams[es.pid]) {
      for(auto &s : m_streamsPending) {
        if(auto stream = dynamic_cast<PesStream *>(s.get()))
          if(matches(stream, es)) {
            m_streams[es.pid] = std::move(s);
            break;
          }
      }
    }

    return dynamic_cast<PesStream *>(m_streams[es.pid].get());
  }

  static bool matches(PesStream *stream, PsiStream::EsInfo es) {
    if(stream->pid == TsDemuxerConfig::ANY) {
      auto meta = createMetadata(es.mpegStreamType);
      if(!meta)
        return false;
      switch(stream->type) {
      case TsDemuxerConfig::AUDIO:
        return meta->type == AUDIO_PKT;
      case TsDemuxerConfig::VIDEO:
        return meta->type == VIDEO_PKT;
      default: // invalid configuration
        assert(0);
        return false;
      }
    } else {
      return stream->pid == es.pid;
    }
  }

  KHost *const m_host;
  unique_ptr<Stream> m_streams[MAX_PID];
  vector<unique_ptr<Stream>> m_streamsPending; // User-provided yet-unmapped PIDs
  int64_t m_ptsOrigin = INT64_MAX;
  TimeUnwrapper m_unwrapper;
  bool m_needsRestamp;

  // incomplete packet from previous data: size < TS_PACKET_LEN and starts with SYNC_BYTE
  uint8_t m_remainder[TS_PACKET_LEN]{};
  unsigned m_remainderSize = 0;

  static auto const SYNC_BYTE = 0x47;
};

IModule *createObject(KHost *host, void *va) {
  auto config = (TsDemuxerConfig *)va;
  enforce(host, "TsDemuxer: host can't be NULL");
  enforce(config, "TsDemuxer: config can't be NULL");
  return createModuleWithSize<TsDemuxer>(384, host, *config).release();
}

auto const registered = Factory::registerModule("TsDemuxer", &createObject);
}
