// A stream parsing PES data.
// Outputs the payload of PES packets.
#pragma once

#include "stream.hpp"
#include "lib_utils/format.hpp"
#include "lib_media/common/attributes.hpp"
#include <vector>
#include <string.h> // memcpy

Metadata createMetadata(int mpegStreamType) {
	auto make = [](Modules::StreamType majorType, const char* codecName) {
		auto meta = make_shared<MetadataPkt>(majorType);
		meta->codec = codecName;
		return meta;
	};

	// remap MPEG-2 stream_type to internal codec names
	switch(mpegStreamType) {
	case 0x01: return make(VIDEO_PKT, "mpeg2video");
	case 0x02: return make(VIDEO_PKT, "mpeg2video");
	case 0x03: return make(AUDIO_PKT, "mp1");
	case 0x04: return make(AUDIO_PKT, "mp2");
	case 0x0f: return make(AUDIO_PKT, "aac");
	case 0x11: return make(AUDIO_PKT, "aac_latm");
	case 0x1b: return make(VIDEO_PKT, "h264");
	case 0x24: return make(VIDEO_PKT, "hevc");
	default: return nullptr; // unknown stream type
	}
}

struct PesStream : Stream {

		struct Restamper {
			virtual void restamp(int64_t& time) = 0;
		};

		PesStream(int pid_, int type_, Restamper* restamper_, KHost* host, OutputDefault* output_) :
			Stream(pid_, host),
			type(type_),
			m_restamper(restamper_),
			m_output(output_) {
			if(type == TsDemuxerConfig::VIDEO)
				m_output->setMetadata(make_shared<MetadataPkt>(VIDEO_PKT));
			else
				m_output->setMetadata(make_shared<MetadataPkt>(AUDIO_PKT));
		}

		void push(SpanC data, bool pusi) override {
			// if we missed the start of the PES packet ...
			if(!pusi && m_pesBuffer.empty())
				return; // ... discard the rest

			for(auto b : data)
				m_pesBuffer.push_back(b);

			// try to early-parse PES_packet_length
			if(m_pesBuffer.size() >= 6) {
				auto PES_packet_length = (m_pesBuffer[4] << 8) + m_pesBuffer[5];
				if(PES_packet_length > 0 && (int)m_pesBuffer.size() >= PES_packet_length + 6) {
					m_pesBuffer.resize(6 + PES_packet_length);
					flush();
				}
			}
		}

		void flush() override {
			auto pesBuffer = move(m_pesBuffer);

			if(pesBuffer.empty())
				return; // nothing to flush

			BitReader r = {SpanC(pesBuffer.data(), pesBuffer.size())};
			if(pesBuffer.size() < 3)
				throw runtime_error(format("[%s] truncated PES packet", pid));

			auto const start_code_prefix = r.u(24);
			if(start_code_prefix != 0x000001)
				throw runtime_error(format("[%s] invalid PES start code (%s)", pid, start_code_prefix));

			/*auto const stream_id =*/ r.u(8);
			/*auto const pes_packet_length =*/ r.u(16);

			// optional PES header
			auto const markerBits = r.u(2);
			if(markerBits != 0x2)
				throw runtime_error("invalid PES header");

			auto const scramblingControl = r.u(2); // 00 implies not scrambled
			/*auto const Priority =*/ r.u(1);
			/*auto const Data_alignment_indicator =*/ r.u(1);
			/*auto const copyrighted =*/ r.u(1);
			/*auto const original =*/ r.u(1);
			auto const PTS_DTS_indicator = r.u(2); // 11 = both present, 01 is forbidden, 10 = only PTS, 00 = no PTS or DTS
			/*auto const ESCR_flag =*/ r.u(1);
			/*auto const ES_rate_flag =*/ r.u(1);
			/*auto const DSM_trick_mode_flag =*/ r.u(1);
			/*auto const Additional_copy_info_flag =*/ r.u(1);
			/*auto const CRC_flag =*/ r.u(1);
			/*auto const extension_flag =*/ r.u(1);
			auto const PES_header_data_length = r.u(8);
			auto const PES_header_data_end = r.byteOffset() + PES_header_data_length;

			if(scramblingControl)
				throw runtime_error("discarding scrambled PES packet");

			if(PES_header_data_length > r.remaining())
				throw runtime_error("Invalid PES_header_data_length");

			int64_t pts = 0;

			if(PTS_DTS_indicator & 0b10) {
				/*auto const reservedBits =*/ r.u(4); // 0b0010
				pts |= r.u(3); // PTS [32..30]
				/*auto marker_bit0 =*/ r.u(1);
				pts <<= 15;
				pts |= r.u(15); // PTS [29..15]
				/*auto marker_bit1 =*/ r.u(1);
				pts <<= 15;
				pts |= r.u(15); // PTS [14..0]
				/*auto marker_bit2 =*/ r.u(1);
			}

			int64_t dts = pts;

			if(PTS_DTS_indicator & 0b01) {
				dts = 0;
				/*auto const reservedBits =*/ r.u(4); // 0b0010
				dts |= r.u(3); // PTS [32..30]
				/*auto marker_bit0 =*/ r.u(1);
				dts <<= 15;
				dts |= r.u(15); // PTS [29..15]
				/*auto marker_bit1 =*/ r.u(1);
				dts <<= 15;
				dts |= r.u(15); // PTS [14..0]
				/*auto marker_bit2 =*/ r.u(1);
			}

			// skip extra remaining headers
			while(r.byteOffset() < PES_header_data_end)
				r.u(8);

			auto pesPayloadSize = pesBuffer.size() - r.byteOffset();
			auto buf = m_output->getBuffer(pesPayloadSize);
			buf->resize(pesPayloadSize);

			if(PTS_DTS_indicator & 0b10) {
				m_restamper->restamp(pts);
				int64_t mediaTime = timescaleToClock(pts, 90000); // PTS are in 90kHz units
				buf->setMediaTime(mediaTime);
			}
			{
				m_restamper->restamp(dts);
				int64_t decodingTime = timescaleToClock(dts, 90000); // DTS are in 90kHz units
				buf->set(DecodingTime {decodingTime});
			}
			buf->set(CueFlags{});
			memcpy(buf->data().ptr, pesBuffer.data()+r.byteOffset(),pesPayloadSize);
			m_output->emit(buf);
		}

		bool setType(int mpegStreamType) {
			auto meta = createMetadata(mpegStreamType);
			if(!meta)
				return false;

			m_output->setMetadata(meta);
			return true;
		}

		int type;
	private:
		Restamper* const m_restamper;
		OutputDefault* m_output = nullptr;
		vector<uint8_t> m_pesBuffer;
};

