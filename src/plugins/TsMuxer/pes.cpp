#include "pes.hpp"

#include "lib_media/common/attributes.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_utils/tools.hpp" // safe_cast

#include "bit_writer.hpp"

using namespace Modules;
using namespace std;

namespace {
// ISO/IEC 13818-1 Table 2-18 : Stream_id assignments
bool isVideoStreamId(int streamId) { return streamId >= 0xE0 && streamId <= 0xEF; }

template<size_t N>
int indexOf(const int (&haystack)[N], int needle) {
  for(int i = 0; i < (int)N; ++i)
    if(haystack[i] == needle)
      return i;

  return -1;
};

void insertAdtsHeadersIfNeeded(BitWriter &w, Data data) {
  auto meta = safe_cast<const MetadataPkt>(data->getMetadata());
  assert(meta);

  if(meta->codec == "aac_raw") {
    auto audio = safe_cast<const MetadataPktAudio>(meta);

    // ISO/IEC 13818-1 Table 35
    static const int frequencies[] = {
          96000,
          88200,
          64000,
          48000,
          44100,
          32000,
          24000,
          22050,
          16000,
          12000,
          11025,
          8000,
    };

    const int frequencyIndex = indexOf(frequencies, audio->sampleRate);

    // ADTS fixed header
    w.u(12, 0xfff); // syncword
    w.u(1, 0); // ID
    w.u(2, 0); // layer
    w.u(1, 1); // protection_absent
    w.u(2, 0x02); // profile object type: AAC-LC
    w.u(4, frequencyIndex); // sampling_frequency_index
    w.u(1, 0); // private_bit
    w.u(3, audio->numChannels); // channel_configuration
    w.u(1, 0); // original_copy
    w.u(1, 0); // home

    // adts_variable_header
    w.u(1, 0); // copyright_identification_bit
    w.u(1, 0); // copyright_identification_start
    w.u(13, data->data().len + 7); // frame_length
    w.u(11, 0x7ff); // adts_buffer_fullness
    w.u(2, 0); // number_of_raw_data_blocks_in_frame
  }
}
}

PesPacket createPesPacket(int streamId, Data data) {
  auto au = data->data();

  auto const pts = data->get<PresentationTime>().time * 90000LL / IClock::Rate;
  auto const dts = data->get<DecodingTime>().time * 90000LL / IClock::Rate;

  PesPacket pkt;
  pkt.data.resize(au.len + 256);
  pkt.dts = dts;
  pkt.tts = dts - IClock::Rate * 3;

  auto w = BitWriter{{pkt.data.data(), pkt.data.size()}};

  // PES packet
  w.u(24, 0x000001); // start_code_prefix
  w.u(8, streamId); // stream_id
  auto pplW = w;
  w.u(16, 0x0000); // PES_packet_length: don't know at the moment

  auto pesPacketStart = w.offset();

  w.u(2, 0x2); // marker_bits
  w.u(2, 0x0); // scrambling control
  w.u(1, 0x0); // priority
  w.u(1, 0x0); // data_alignment_indicator
  w.u(1, 0x0); // copyrighted
  w.u(1, 0x0); // original

  bool ptsFlag = true;
  bool dtsFlag = isVideoStreamId(streamId);

  // PTS_DTS_indicator
  w.u(1, ptsFlag);
  w.u(1, dtsFlag);

  w.u(1, 0x0); // ESCR_flag
  w.u(1, 0x0); // ES_rate_flag
  w.u(1, 0x0); // DSM_trick_mode_flag
  w.u(1, 0x0); // Additional_copy_info_flag
  w.u(1, 0x0); // CRC_flag
  w.u(1, 0x0); // extension_flag

  w.u(8, (int(ptsFlag) + int(dtsFlag)) * 5); // PES_header_length

  if(ptsFlag) {
    w.u(4, 0b0010); // reserved
    w.u(3, (pts >> 30) & 0b111); // PTS[32..30]
    w.u(1, 1); // marker_bit
    w.u(15, (pts >> 15) & 0b111111111111111); // PTS[29..15]
    w.u(1, 1); // marker_bit
    w.u(15, pts & 0b111111111111111); // PTS[14..0]
    w.u(1, 1); // marker_bit
  }

  if(dtsFlag) {
    w.u(4, 0b0010); // reserved
    w.u(3, (dts >> 30) & 0b111); // DTS[32..30]
    w.u(1, 1); // marker_bit
    w.u(15, (dts >> 15) & 0b111111111111111); // DTS[29..15]
    w.u(1, 1); // marker_bit
    w.u(15, dts & 0b111111111111111); // DTS[14..0]
    w.u(1, 1); // marker_bit
  }

  insertAdtsHeadersIfNeeded(w, data);

  for(auto b : au)
    w.u(8, b);

  // now we know the PES_packet_length: write it
  auto const PES_packet_length = w.offset() - pesPacketStart;

  if(PES_packet_length < 0x10000 && !isVideoStreamId(streamId))
    pplW.u(16, PES_packet_length);

  pkt.data.resize((size_t)w.offset());
  return pkt;
}
