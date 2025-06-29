// A stream parsing PES data.
// Outputs the payload of PES packets.
#pragma once

#include "stream.hpp"
#include "lib_utils/format.hpp"
#include "lib_media/common/attributes.hpp"
#include <vector>
#include <cstring> // memcpy
#include <utility> // exchange

Metadata createMetadata(int mpegStreamType) {
	auto make = [](Modules::StreamType majorType, const char* codecName) {
		auto meta = make_shared<MetadataPkt>(majorType);
		meta->codec = codecName;
		return meta;
	};

	// remap MPEG-2 stream_type to internal codec names
	// (cf. ISO/IEC 13818-1 Table 2-29)
	switch(mpegStreamType & 0xff) {
	case 0x01: return make(VIDEO_PKT, "mpeg2video");
	case 0x02: return make(VIDEO_PKT, "mpeg2video");
	case 0x03: return make(AUDIO_PKT, "mp1");
	case 0x04: return make(AUDIO_PKT, "mp2");
	case 0x0f: return make(AUDIO_PKT, "aac_adts");
	case 0x11: return make(AUDIO_PKT, "aac_latm");
	case 0x1b: return make(VIDEO_PKT, "h264_annexb");
	case 0x24: return make(VIDEO_PKT, "hevc_annexb");
	case 0x81: return make(AUDIO_PKT, "ac3");
	case 0x84: return make(AUDIO_PKT, "eac3");
	case 0x06: { /*private*/
		auto const descriptor_tag = mpegStreamType >> 8;
		switch (descriptor_tag) {
		case 0x6A: return make(AUDIO_PKT, "ac3");
		case 0x7A: return make(AUDIO_PKT, "eac3");
		default: return nullptr; // unknown stream type
		}
	}
	default: return nullptr; // unknown stream type
	}
}

struct PesStream : Stream {
		struct IRestamper {
			virtual void restamp(int64_t& time) = 0;
		};

		struct MyVector {
				MyVector(int reservedSize) {
					m_vector.resize(reservedSize);
				}
				MyVector(MyVector &&src) : m_vector(std::move(src.m_vector)), m_size(std::exchange(src.m_size, 0)) {}
				size_t size() const {
					return m_size;
				}
				bool empty() const {
					return m_size == 0;
				}
				void insert(uint8_t const * const ptr, size_t len) {
					auto const sz = size();
					resize(sz + len);
					memcpy(&m_vector[sz], ptr, len);
				}
				void resize(size_t size) {
					m_size = size;
					// only resize to a bigger size
					if (size > m_vector.size())
						m_vector.resize(size);
				}
				uint8_t& operator[] (int i) {
					return m_vector[i];
				}
				const uint8_t* data() const {
					return m_vector.data();
				}
				void clear() {
					// don't reset the vector
					m_size = 0;
				}

			private:
				std::vector<uint8_t> m_vector;
				size_t m_size = 0;
		};

		PesStream(int pid_, int type_, IRestamper* restamper_, KHost* host, OutputDefault* output_) :
			Stream(pid_, host), type(type_), m_restamper(restamper_), m_output(output_), m_pesBuffer(256*1024) {
			if(type == TsDemuxerConfig::VIDEO)
				m_output->setMetadata(make_shared<MetadataPkt>(VIDEO_PKT));
			else
				m_output->setMetadata(make_shared<MetadataPkt>(AUDIO_PKT));
		}

		void push(SpanC data, bool pusi) override {
			// if we missed the start of the PES packet ...
			if(!pusi && m_pesBuffer.empty())
				return; // ... discard the rest

			m_pesBuffer.insert(data.ptr, data.len);

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
			if(m_pesBuffer.empty())
				return; // nothing to flush

			try {
				BitReader r = {SpanC(m_pesBuffer.data(), m_pesBuffer.size())};
				if(m_pesBuffer.size() < 3)
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
					dts |= r.u(3); // DTS [32..30]
					/*auto marker_bit0 =*/ r.u(1);
					dts <<= 15;
					dts |= r.u(15); // DTS [29..15]
					/*auto marker_bit1 =*/ r.u(1);
					dts <<= 15;
					dts |= r.u(15); // DTS [14..0]
					/*auto marker_bit2 =*/ r.u(1);
				}

				// skip extra remaining headers
				while(r.byteOffset() < PES_header_data_end)
					r.u(8);

				auto pesPayloadSize = m_pesBuffer.size() - r.byteOffset();
				auto buf = m_output->allocData<DataRaw>(pesPayloadSize);

				if(PTS_DTS_indicator & 0b10) {
					m_restamper->restamp(pts);
					int64_t presentationTime = timescaleToClock(pts, 90000); // PTS are in 90kHz units
					buf->set(PresentationTime {presentationTime});
				}
				{
					m_restamper->restamp(dts);
					int64_t decodingTime = timescaleToClock(dts, 90000); // DTS are in 90kHz units
					buf->set(DecodingTime {decodingTime});
				}
				buf->set(CueFlags{ discontinuity, rap, true });
				memcpy(buf->buffer->data().ptr, m_pesBuffer.data()+r.byteOffset(), pesPayloadSize);
				m_output->post(buf);

				m_pesBuffer.clear();
				discontinuity = false;
				rap = false;
			} catch (const std::runtime_error &e) {
				m_pesBuffer.clear();
				throw(e);
			}
		}

		bool reset() override {
			if(m_pesBuffer.empty())
				return false;

			m_pesBuffer.clear();
			discontinuity = true;
			return true;
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
		IRestamper * const m_restamper;
		OutputDefault * const m_output = nullptr;
		MyVector m_pesBuffer;
		bool discontinuity = false;
};

