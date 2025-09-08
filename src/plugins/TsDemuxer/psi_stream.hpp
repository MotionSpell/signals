// A stream parsing PSI tables (i.e PAT, PMT)
#pragma once

#include <vector>

#include "stream.hpp"

auto const TABLE_ID_PAT = 0;
auto const TABLE_ID_PMT = 2;

static void skip(BitReader &r, int byteCount, const char *what) {
  if(r.remaining() < byteCount)
    throw runtime_error(
          format("Error while skipping \"%s\": %s bytes remains out of %s", what, r.remaining(), byteCount));
  for(int i = 0; i < byteCount; ++i)
    r.u(8);
}

struct PsiStream : Stream {
  struct EsInfo {
    int pid, mpegStreamType;
  };

  struct Listener {
    virtual void onPat(span<int> pmtPids) = 0;
    virtual void onPmt(span<EsInfo> esInfo) = 0;
  };

  PsiStream(int pid_, KHost *host, Listener *listener_)
      : Stream(pid_, host)
      , m_host(host)
      , listener(listener_) {}

  void push(SpanC data, bool pusi) override {
    BitReader r = {data};
    if(pusi) {
      int pointerField = r.u(8);
      skip(r, pointerField, "pointer_field before PSI section");
    }

    r = BitReader{r.payload()};

    auto const PSI_HEADER_SIZE = 8;
    if(r.remaining() < PSI_HEADER_SIZE)
      throw runtime_error("Truncated PSI header");

    auto const table_id = r.u(8);
    /*auto const section_syntax_indicator =*/r.u(1);
    auto const zero_bit = r.u(1);
    if(zero_bit != 0)
      m_host->log(Warning, "Zero-bit in PSI header is not zero");

    /*auto const reserved1 =*/r.u(2);
    auto /*const*/ section_length = r.u(12);

    auto sectionStart = r.byteOffset();
    if(r.remaining() < section_length)
      throw runtime_error("Invalid section_length in PSI header");

    /*auto const table_id_extension =*/r.u(16);
    /*auto const reserved2 =*/r.u(2);
    /*auto const version_number =*/r.u(5);
    /*auto const current_next_indicator =*/r.u(1);
    /*auto const section_number =*/r.u(8);
    /*auto const last_section_number =*/r.u(8);

    assert(PSI_HEADER_SIZE == r.byteOffset());

    auto const crcSize = 4;

    switch(table_id) {
    case TABLE_ID_PAT: {
      while(r.byteOffset() < sectionStart + section_length - crcSize) {
        auto const program_number = r.u(16);
        /*auto const reserved3 =*/r.u(3);
        if(program_number == 0) {
          /*auto const network_pid = */ r.u(13);
        } else {
          auto const program_map_pid = r.u(13);

          int pids[] = {program_map_pid};
          listener->onPat(pids);
        }
      }

      break;
    }
    case TABLE_ID_PMT: {
      /*auto const reserved3 =*/r.u(3);
      /*auto const pcr_pid =*/r.u(13);
      /*auto const reserved4 =*/r.u(4);
      auto const program_info_length = r.u(12);

      skip(r, program_info_length, "program_info_length in PSI header");

      vector<EsInfo> info;

      while(r.byteOffset() < sectionStart + section_length - crcSize) {
        // Elementary stream info
        auto stream_type = r.u(8);
        /*auto const reserved5 =*/r.u(3);
        auto const pid = r.u(13);
        /*auto const reserved6 =*/r.u(4);
        auto const es_info_length = r.u(12);
        auto const finalPos = r.m_pos + es_info_length * 8;

        while(r.m_pos < finalPos) {
          auto descriptor_tag = r.u(8);
          auto descriptor_length = r.u(8);
          if(descriptor_tag == 0x6A /*AC-3*/ || descriptor_tag == 0x7A /*EAC-3*/) {
            stream_type |= (descriptor_tag << 8);
          }
          skip(r, descriptor_length, "descriptor in PSI header");
        }

        assert(finalPos >= r.m_pos);
        r.u(finalPos - r.m_pos);

        info.push_back({pid, stream_type});
      }

      listener->onPmt({info.data(), info.size()});
      break;
    } break;
    }

    /*auto const crc32 = r.u(crcSize * 8);*/
  }

  void flush() override {}

  bool reset() override { return false; }

  private:
  KHost *const m_host;
  Listener *const listener;
};
