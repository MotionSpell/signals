#include "mpegts_muxer.hpp"
#include "bit_writer.hpp"
#include "pes.hpp"
#include "lib_modules/utils/helper_dyn.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/attributes.hpp"
#include <cassert>
#include <string>

static auto const TS_PACKET_SIZE = 188;

using namespace Modules;
using namespace std;

uint32_t Crc32(SpanC data);

namespace {
static auto const PAT_INTERVAL_MS = 100;
static auto const PMT_INTERVAL_MS = 100;

static auto const PAT_PID = 0; // normative
static auto const PMT_PID = 4096; // implementation specific
static auto const BASE_PID = 256; // implementation specific
static auto const PCR_PID = BASE_PID; // implementation specific

// ISO/IEC 13818-1 Table 2-29
int codecToMpegStreamType(string codec) {
	if(codec == "h264_annexb")
		return 0x1b;
	else if(codec == "mpeg2video")
		return 0x02;
	else if(codec == "hevc_annexb")
		return 0x24;
	else if(codec == "mp3")
		return 0x03;
	else if(codec == "aac_adts" || codec == "aac_raw")
		return 0x0f;
	else
		throw runtime_error("unsupported codec: '" + codec + "'");
}

struct Stream {
	int streamType {}; // MPEG-2 specified
	vector<PesPacket> fifo;
};

class TsMuxer : public ModuleDynI {
	public:

		TsMuxer(KHost* host, TsMuxerConfig cfg)
			: m_host(host), m_cfg(cfg) {
			m_output = addOutput();
		}

		void process() override {
			m_streams.resize(getNumInputs() - 1);

			int id;
			auto data = popAny(id);

			assert(id < (int)m_streams.size());
			inputs[id]->updateMetadata(data);

			if(m_streams[id].streamType == 0)
				declareStream(m_streams[id], data);

			// if 'data' is a stream declaration, there's no actual data to process.
			if(!isDeclaration(data)) {
				auto streamId = m_streams[id].streamType == 0x03 ? 0xC0 : 0xE0;
				m_streams[id].fifo.push_back(createPesPacket(streamId, data));
			}

			while(mux()) {
			}
		}

		void flush() override {
		}

	private:
		KHost* const m_host;
		TsMuxerConfig const m_cfg;
		OutputDefault* m_output {};

		vector<Stream> m_streams;
		int m_patTimer = 0;
		int m_pmtTimer = 0;
		int64_t m_pcrOffset = INT64_MAX;
		uint8_t m_cc[8192] {};

		// total packet count. Used to compute PCR.
		int64_t m_packetCount = 0;

		Data popAny(int& inputIdx) {
			Data data;
			inputIdx = 0;
			while (!inputs[inputIdx]->tryPop(data)) {
				inputIdx++;
			}
			return data;
		}

		void declareStream(Stream& s, Data data) {
			if(!data->getMetadata())
				throw error("Can't declare stream without metadata");

			auto const metadata = safe_cast<const MetadataPkt>(data->getMetadata().get());

			enforce(metadata->bitrate >= 0, "bitrate must be specified for each ES");

			s.streamType = codecToMpegStreamType(metadata->codec);
		}

		// packet scheduling occurs here
		bool mux() {
			if(m_patTimer <= 0) {
				sendPat();
				m_patTimer = timeToPackets(PAT_INTERVAL_MS);
				return true;
			}

			// can't mux any further if we don't know the stream types
			for(auto& s : m_streams)
				if(s.streamType == 0)
					return false;

			if(m_pmtTimer <= 0) {
				sendPmt();
				m_pmtTimer = timeToPackets(PMT_INTERVAL_MS);
				return true;
			}

			int bestIdx = -1;

			{
				int idx = 0;
				int64_t bestTts = INT64_MAX;
				for(auto& s : m_streams) {
					// if one stream has no data, we can't compute the lowest TTS.
					if(s.fifo.empty())
						return false;

					auto tts = s.fifo.front().tts;
					if(tts < bestTts) {
						bestTts = tts;
						bestIdx = idx;
					}

					++idx;
				}
			}

			// we have an AU with a DTS
			if(bestIdx >= 0) {
				auto tts = m_streams[bestIdx].fifo.front().tts;

				// compute first pcr
				if(m_pcrOffset == INT64_MAX) {
					assert(tts != INT64_MAX);
					m_pcrOffset = tts;
				}

				// time to send?
				if(tts < pcr()) {
					auto& stream = m_streams[bestIdx];
					auto pkt = stream.fifo.front();
					stream.fifo.erase(stream.fifo.begin());
					sendPes(pkt, BASE_PID + bestIdx);
					return true;
				}
			}

			// nothing to send: send one NUL packet
			SpanC sp {};
			sendTsPacket(0x1FFF, sp, 0);
			return true;
		}

		void sendPat() {
			uint8_t payload[64] {};
			auto w = BitWriter { payload };

			// ISO/IEC 13818-1 Table 2-24
			w.u(8, 0); // pointer field

			// ISO/IEC 13818-1 Table 2-25
			w.u(8, 0x0); // table id: PAT
			w.u(1, 0x1); // section syntax indicator
			w.u(1, 0x0); // zero bit
			w.u(2, 0x3); // reserved bits

			auto ws = w;
			w.u(12, 0); // section_length: unknown for the moment
			auto const sectionStart = w.offset();

			w.u(16, 0x01); // transport_stream_id (Table ID extension)
			w.u(2, 0x3); // reserved
			w.u(5, 0x0); // version_number
			w.u(1, 0x1); // current_next_indicator
			w.u(8, 0x00); // section_number
			w.u(8, 0x00); // last_section_number

			w.u(16, 0x0001); // program_number
			w.u(3, 0x7); // reserved bits
			w.u(13, PMT_PID); // program map PID

			// now we know section_length: write it back
			ws.u(12, w.offset() - sectionStart + 4);

			// compute and write the CRC (skip pointer_field)
			w.u(32, Crc32({ payload + 1, (size_t)w.offset() - 1 }));

			auto sp = SpanC { payload, (size_t)(w.offset()) };
			sendTsPacket(PAT_PID, sp, 1);
		}

		void sendPmt() {
			uint8_t payload[128] {};
			auto w = BitWriter { payload };

			// ISO/IEC 13818-1 Table 2-24
			w.u(8, 0x00); // pointer field

			// ISO/IEC 13818-1 Table 2-28
			w.u(8, 0x02); // table id: PMT
			w.u(1, 0x1); // section syntax indicator
			w.u(1, 0x0); // private bit
			w.u(2, 0x3); // reserved

			auto ws = w;
			w.u(12, 0); // section_length: unknown for the moment
			auto const sectionStart = w.offset();

			w.u(16, 0x01); // program_number (Table ID extension)
			w.u(2, 0x3); // reserved
			w.u(5, 0x0); // version_number
			w.u(1, 0x1); // current_section_indicator
			w.u(8, 0x00); // section_number
			w.u(8, 0x00); // last_section_number

			w.u(3, 0x7); // reserved
			w.u(13, PCR_PID); // PCR_PID
			w.u(4, 0xf); // reserved
			w.u(12, 0); // program_info_length

			for(int i = 0; i < (int)m_streams.size(); ++i) {
				w.u(8, m_streams[i].streamType); // stream type
				w.u(3, 0x7); // reserved
				w.u(13, BASE_PID + i); // PID
				w.u(4, 0xf); // reserved
				w.u(12, 0); // ES info length
			}

			// now we know section_length: write it back
			ws.u(12, (w.offset() - sectionStart + 4));

			// compute and write the CRC (skip pointer_field)
			w.u(32, Crc32({ payload + 1, (size_t)w.offset() - 1 }));

			auto sp = SpanC { payload, (size_t)(w.offset()) };
			sendTsPacket(PMT_PID, sp, 1);
		}

		void sendPes(PesPacket const& pkt, int pid) {
			auto pes = SpanC { pkt.data.data(), pkt.data.size() };

			// send the whole access unit in one burst
			sendTsPacket(pid, pes, true);

			while(pes.len > 0)
				sendTsPacket(pid, pes, false);

			// can only check the timings if we actually have a PCR
			assert(m_pcrOffset != INT64_MAX);

			auto removalDelay = pkt.dts - pcr();

			if(removalDelay < 0) {
				char msg[256];
				sprintf(msg, "[%d] PES packet sent too late: %.3fs late", pid, -removalDelay/double(IClock::Rate));
				m_host->log(Warning, msg);
				if(pid == PCR_PID) {
					m_pcrOffset += removalDelay * 2;
					sprintf(msg, "[%d] Resetting PCR", pid);
					m_host->log(Warning, msg);
				}
			} else if(removalDelay > IClock::Rate * 5) {
				char msg[256];
				sprintf(msg, "[%d] PES packet sent too early: %.3fs early", pid, removalDelay/double(IClock::Rate));
				m_host->log(Warning, msg);
			}
		}

		// send bytes from 'unit' and update its span.
		void sendTsPacket(int pid, SpanC& unit, int pusi) {
			auto const payload_flag = unit.len > 0;
			auto data = serializeTsPacket(pid, unit, pusi);

			// deliver it to the output
			data->set(PresentationTime { time() });
			m_output->post(data);

			m_packetCount++;

			if(payload_flag)
				m_cc[pid] = (m_cc[pid] + 1) % 16;

			// advance time
			m_patTimer--;
			m_pmtTimer--;
		}

		std::shared_ptr<DataRaw> serializeTsPacket(int pid, SpanC& unit, int pusi) const {
			auto buf = m_output->allocData<DataRaw>(TS_PACKET_SIZE);

			auto pkt = buf->buffer->data();
			auto const adaptation_field_flag = 1;
			auto w = BitWriter { pkt };

			w.u(8, 0x47); // sync byte

			w.u(1, 0); // TEI
			w.u(1, pusi); // PUSI
			w.u(1, 0); // priority
			w.u(13, pid); // PID

			w.u(2, 0); // scrambling control
			w.u(1, adaptation_field_flag); // adaptation_field_control: bit #0
			w.u(1, unit.len > 0 ? 1 : 0); // adaptation_field_control: bit #1
			w.u(4, m_cc[pid]); // continuity counter

			if(adaptation_field_flag)
				writeAdaptationField(w, pid == PCR_PID);

			// write the actual TS payload
			auto payloadStart = pkt.ptr + w.offset();

			while(unit.len && w.offset() < TS_PACKET_SIZE) {
				w.u(8, unit[0]);
				unit += 1;
			}

			auto payloadEnd = pkt.ptr + w.offset();

			// Insert stuffing bytes if needed.
			// (use the stuffing bytes at the end of the adaptation field).
			auto const stuffingByteCount = TS_PACKET_SIZE - w.offset();
			assert(stuffingByteCount >= 0);

			if(stuffingByteCount) {
				assert(adaptation_field_flag);
				pkt[4] += stuffingByteCount; // patch adaptation_field_length
				// move the payload to the end of the TS packet.
				memmove(payloadStart + stuffingByteCount, payloadStart, payloadEnd - payloadStart);
				memset(payloadStart, 0xFF, stuffingByteCount);
			}

			return buf;
		}

		void writeAdaptationField(BitWriter& w, bool pcrFlag) const {
			auto wafl = w;
			w.u(8, 0); // adaptation field length: unknown at the moment

			auto adaptationFieldStart = w;
			w.u(1, 0); // discontinuity indicator
			w.u(1, 1); // random Access indicator
			w.u(1, 0); // elementary stream priority indicator
			w.u(1, pcrFlag); // PCR flag
			w.u(1, 0); // OPCR flag
			w.u(1, 0); // Splicing point flag
			w.u(1, 0); // Transport private data flag
			w.u(1, 0); // Adaptation field extension flag

			if(pcrFlag) {
				auto const pcrBase = (uint64_t)(pcr() * 90000 / IClock::Rate);
				w.u(33, pcrBase & 0x1FFFFFFFF);
				w.u(6, -1); // reserved
				w.u(9, 0); // pcr 27Mhz (x300)
			}

			auto len = w.offset() - adaptationFieldStart.offset();
			wafl.u(8, len);
		}

		int64_t pcr() const {
			return m_pcrOffset + time();
		}

		int64_t time() const {
			return IClock::Rate * (m_packetCount * TS_PACKET_SIZE * 8) / m_cfg.muxRate;
		}

		int64_t timeToPackets(int64_t timeInMs) const {
			auto const pktFreq = Fraction(m_cfg.muxRate, TS_PACKET_SIZE * 8);
			return (int64_t)(pktFreq * timeInMs) / 1000;
		}
};

Modules::IModule* createObject(KHost* host, void* arg) {
	auto config = reinterpret_cast<TsMuxerConfig*>(arg);
	enforce(host, "TsMuxer: host can't be NULL");
	enforce(config, "TsMuxer: config can't be NULL");

	auto const BUFFER_SIZE = 2 * 1024 * 1024; // 2 Mb total
	return new ModuleDefault<TsMuxer>(BUFFER_SIZE/TS_PACKET_SIZE, host, *config);
}

auto const registered = Factory::registerModule("TsMuxer", &createObject);
}
