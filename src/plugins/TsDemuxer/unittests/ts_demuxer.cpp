#include "tests/tests.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_utils/tools.hpp" // safe_cast
#include "../ts_demuxer.hpp"
#include <cstring> // memcpy

using namespace Tests;
using namespace Modules;

std::shared_ptr<DataBase> createPacket(SpanC span) {
	auto N = span.len;
	auto pkt = make_shared<DataRaw>(N);
	memcpy(pkt->buffer->data().ptr, span.ptr, N);
	return pkt;
}

namespace {
struct BitWriter {
	Span dst;

	void u(int n, int val) {
		for(int i=0; i < n; ++i) {
			int bit = (val >> (n-1-i)) & 1;
			putBit(bit);
		}
	}

	void seek(int offset) {
		m_pos = offset*8;
	}


	void putBit(int bit) {
		auto bitIndex = m_pos%8;
		auto byteIndex = m_pos/8;
		auto mask = (1 << (7-bitIndex));
		if(bit)
			dst[byteIndex] |= mask;
		else
			dst[byteIndex] &= ~mask;
		m_pos++;
	}

	int m_pos = 0;
};

static void writeSimplePes(BitWriter& w) {
	// PES packet
	w.u(24, 0x000001); // start_code_prefix
	w.u(8, 0x0); // stream_id
	w.u(16, 0x0000); // PES_packet_length

	w.u(2, 0x2); // optional_PES_header
	w.u(2, 0x0); // scrambling control
	w.u(1, 0x0); // priority
	w.u(1, 0x0); // Data_alignment_indicator
	w.u(1, 0x0); // copyrighted
	w.u(1, 0x0); // original

	w.u(2, 0b10); // PTS_DTS_indicator
	w.u(1, 0x0); // ESCR_flag
	w.u(1, 0x0); // ES_rate_flag
	w.u(1, 0x0); // DSM_trick_mode_flag
	w.u(1, 0x0); // Additional_copy_info_flag
	w.u(1, 0x0); // CRC_flag
	w.u(1, 0x0); // extension_flag

	w.u(8, 0x5); // PES_header_length

	// PTS
	w.u(4, 0b0010); // reserved
	w.u(3, 0); // PTS[32..30]
	w.u(1, 0); // marker_bit
	w.u(15, 0); // PTS[29..15]
	w.u(1, 0); // marker_bit
	w.u(15, 0); // PTS[14..0]
	w.u(1, 0); // marker_bit
}

std::shared_ptr<DataBase> getTestTs() {
	uint8_t tsPackets[2 * 188] {};
	BitWriter w { {tsPackets, sizeof tsPackets} };

	{
		w.seek(0);
		w.u(8, 0x47); // sync byte
		w.u(1, 0); // TEI
		w.u(1, 1); // PUSI
		w.u(1, 0); // priority
		w.u(13, 120); // PID
		w.u(2, 0); // scrambling control
		w.u(2, 0b11); // adaptation field control
		w.u(4, 0); // continuity counter

		// adaptation field
		w.u(8, 3); // adaptation field length
		for(int i=0; i < 3; ++i)
			w.u(8, 0x99); // adaptation field raw byte

		writeSimplePes(w);
	}

	{
		w.seek(188);
		w.u(8, 0x47); // sync byte
		w.u(1, 0); // TEI
		w.u(1, 1); // PUSI
		w.u(1, 0); // priority
		w.u(13, 120); // PID
		w.u(2, 0); // scrambling control
		w.u(2, 0b01); // adaptation field control
		w.u(4, 1); // continuity counter

		writeSimplePes(w);
	}

	return createPacket(tsPackets);
}

struct FrameCounter : ModuleS {
	void processOne(Data data) override {
		++frameCount;
		totalLength += (int)data->data().len;
	}
	int frameCount = 0;
	int totalLength = 0;
};

}

unittest("TsDemuxer: pins with proper metadata are created based on config, not input data") {
	TsDemuxerConfig cfg;
	cfg.pids = {};
	cfg.pids.push_back(TsDemuxerConfig::ANY_AUDIO());
	cfg.pids.push_back(TsDemuxerConfig::ANY_VIDEO());

	auto demux = loadModule("TsDemuxer", &NullHost, &cfg);
	ASSERT_EQUALS(2, demux->getNumOutputs());

	auto meta0 = demux->getOutput(0)->getMetadata();
	ASSERT(meta0);
	ASSERT_EQUALS(AUDIO_PKT, meta0->type);

	auto meta1 = demux->getOutput(1)->getMetadata();
	ASSERT(meta1);
	ASSERT_EQUALS(VIDEO_PKT, meta1->type);
}

unittest("TsDemuxer: PES demux") {
	TsDemuxerConfig cfg;
	cfg.pids = {};
	cfg.pids.push_back({ 120, 1 });

	auto demux = loadModule("TsDemuxer", &NullHost, &cfg);
	auto rec = createModule<FrameCounter>();
	ConnectOutputToInput(demux->getOutput(0), rec->getInput(0));

	auto frame = getTestTs();
	demux->getInput(0)->push(frame);
	demux->flush();

	ASSERT_EQUALS(2, rec->frameCount);
	ASSERT_EQUALS(336, rec->totalLength);
}

unittest("TsDemuxer: PES demux should not wait for next AU") {
	uint8_t tsPackets[188] {};
	BitWriter w { {tsPackets, sizeof tsPackets} };

	{
		// TS packet
		w.seek(0);
		w.u(8, 0x47); // sync byte
		w.u(1, 0); // TEI
		w.u(1, 1); // PUSI
		w.u(1, 0); // priority
		w.u(13, 222); // PID
		w.u(2, 0); // scrambling control
		w.u(2, 0b01); // adaptation field control: only payload
		w.u(4, 0); // continuity counter

		// PES packet
		w.u(24, 0x000001); // start_code_prefix
		w.u(8, 0x0); // stream_id
		w.u(16, 0x0008); // PES_packet_length: make it non-zero: 8 bytes

		w.u(2, 0x2); // marker_bits
		w.u(2, 0x0); // scrambling control
		w.u(1, 0x0); // priority
		w.u(1, 0x0); // Data_alignment_indicator
		w.u(1, 0x0); // copyrighted
		w.u(1, 0x0); // original

		w.u(2, 0x0); // PTS_DTS_indicator
		w.u(1, 0x0); // ESCR_flag
		w.u(1, 0x0); // ES_rate_flag
		w.u(1, 0x0); // DSM_trick_mode_flag
		w.u(1, 0x0); // Additional_copy_info_flag
		w.u(1, 0x0); // CRC_flag
		w.u(1, 0x0); // extension_flag

		w.u(8, 0x0); // PES_header_data_length
	}

	TsDemuxerConfig cfg;
	cfg.pids = {};
	cfg.pids.push_back({ 222, 1 });

	auto demux = loadModule("TsDemuxer", &NullHost, &cfg);
	auto rec = createModule<FrameCounter>();
	ConnectOutputToInput(demux->getOutput(0), rec->getInput(0));

	auto frame = createPacket(tsPackets);
	demux->getInput(0)->push(frame);
	// don't flush

	ASSERT_EQUALS(1, rec->frameCount);
	ASSERT_EQUALS(5, rec->totalLength);
}

unittest("TsDemuxer: two pins, one PID") {
	TsDemuxerConfig cfg;
	cfg.pids = {};
	cfg.pids.push_back({ 130, 1 });
	cfg.pids.push_back({ 120, 1 });

	auto demux = loadModule("TsDemuxer", &NullHost, &cfg);
	auto rec130 = createModule<FrameCounter>();
	auto rec120 = createModule<FrameCounter>();
	ConnectOutputToInput(demux->getOutput(0), rec130->getInput(0));
	ConnectOutputToInput(demux->getOutput(1), rec120->getInput(0));

	auto frame = getTestTs();
	demux->getInput(0)->push(frame);

	// don't flush

	ASSERT_EQUALS(0, rec130->frameCount);
	ASSERT_EQUALS(1, rec120->frameCount);
}

unittest("TsDemuxer: two pins, two PIDs") {

	uint8_t tsPackets[2 * 188] {};
	BitWriter w { {tsPackets, sizeof tsPackets} };

	{
		w.seek(0);
		w.u(8, 0x47); // sync byte
		w.u(1, 0); // TEI
		w.u(1, 1); // PUSI
		w.u(1, 0); // priority
		w.u(13, 666); // PID
		w.u(2, 0); // scrambling control
		w.u(2, 0b01); // adaptation field control
		w.u(4, 5); // continuity counter

		writeSimplePes(w);
	}

	{
		w.seek(188);
		w.u(8, 0x47); // sync byte
		w.u(1, 0); // TEI
		w.u(1, 1); // PUSI
		w.u(1, 0); // priority
		w.u(13, 777); // PID
		w.u(2, 0); // scrambling control
		w.u(2, 0b01); // adaptation field control
		w.u(4, 6); // continuity counter

		writeSimplePes(w);
	}

	TsDemuxerConfig cfg;
	cfg.pids = {};
	cfg.pids.push_back({ 666, 1 });
	cfg.pids.push_back({ 777, 1 });

	auto demux = loadModule("TsDemuxer", &NullHost, &cfg);
	ASSERT_EQUALS(2, demux->getNumOutputs());

	auto pid0 = createModule<FrameCounter>();
	auto pid1 = createModule<FrameCounter>();
	ConnectOutputToInput(demux->getOutput(0), pid0->getInput(0));
	ConnectOutputToInput(demux->getOutput(1), pid1->getInput(0));

	demux->getInput(0)->push(createPacket(tsPackets));

	demux->flush();

	ASSERT_EQUALS(1, pid0->frameCount);
	ASSERT_EQUALS(1, pid1->frameCount);
}

unittest("TsDemuxer: get codec from PMT") {

	uint8_t tsPackets[2 * 188] {};
	BitWriter w { {tsPackets, sizeof tsPackets} };

	// PAT
	{
		w.seek(0 * 188);
		w.u(8, 0x47); // sync byte
		w.u(1, 0); // TEI
		w.u(1, 1); // PUSI
		w.u(1, 0); // priority
		w.u(13, 0); // PID=0: PAT
		w.u(2, 0); // scrambling control
		w.u(2, 0b01); // adaptation field control
		w.u(4, 0); // continuity counter

		w.u(8, 2); // pointer field
		w.u(8, 0x77); // garbage byte
		w.u(8, 0x77); // garbage byte

		w.u(8, 0x0); // table id: PAT
		w.u(1, 0x1); // section syntax indicator
		w.u(1, 0x0); // private bit
		w.u(2, 0x3); // reserved bits
		w.u(12, 0x11); // section length

		w.u(16, 0x01); // transport_stream_id (Table ID extension)
		w.u(2, 0x3); // reserved
		w.u(5, 0x0); // version_number
		w.u(1, 0x1); // current_next_indicator
		w.u(8, 0x00); // section_number
		w.u(8, 0x00); // last_section_number

		// actual PAT data
		w.u(16, 0x0000); // program_number (NIT)
		w.u(3, 0x7); // reserved bits
		w.u(13, 80); // network PID
		w.u(16, 0x0001); // program_number
		w.u(3, 0x7); // reserved bits
		w.u(13, 50); // program map PID

		w.u(32, 0x2ab104b2); // CRC32
	}

	// PMT
	{
		w.seek(1 * 188);
		w.u(8, 0x47); // sync byte
		w.u(1, 0); // TEI
		w.u(1, 1); // PUSI
		w.u(1, 0); // priority
		w.u(13, 50); // PID
		w.u(2, 0); // scrambling control
		w.u(2, 0b01); // adaptation field control
		w.u(4, 0); // continuity counter

		w.u(8, 0x00); // pointer field

		w.u(8, 0x02); // table id: PMT
		w.u(1, 0x1); // section syntax indicator
		w.u(1, 0x0); // private bit
		w.u(2, 0x3); // reserved
		w.u(12, 0x24); // section_length

		w.u(16, 0x01); // program_number (Table ID extension)
		w.u(2, 0x3); // reserved
		w.u(5, 0x0); // version_number
		w.u(1, 0x1); // current_section_indicator
		w.u(8, 0x00); // section_number
		w.u(8, 0x00); // last_section_number

		// actual PMT data
		w.u(3, 0x7); // reserved
		w.u(13, 0x100); // PCR_PID
		w.u(4, 0xf); // reserved
		w.u(12, 0x3); // program_info_length

		// program info
		w.u(8, 0x33); // garbage byte
		w.u(8, 0x33); // garbage byte
		w.u(8, 0x33); // garbage byte

		// Elementary stream info
		w.u(8, 0x04); // stream type: MPEG2 audio
		w.u(3, 0x7); // reserved
		w.u(13, 777); // PID
		w.u(4, 0xf); // reserved
		w.u(12, 0x3); // ES info length

		// ES info
		w.u(8, 0x0); // ES info: descriptor_tag
		w.u(8, 0x1); // ES info: descriptor_length
		w.u(8, 0x77); // garbage byte (ES info)

		// Elementary stream info
		w.u(8, 0x1b); // stream type: H.264
		w.u(3, 0x7); // reserved
		w.u(13, 666); // PID
		w.u(4, 0xf); // reserved
		w.u(12, 0x0); // ES info length

		// Elementary stream info: AC-3 (private_data + AC-3 descriptor)
		w.u(8, 0x6); // stream type: private
		w.u(3, 0x7); // reserved
		w.u(13, 555); // PID
		w.u(4, 0xf); // reserved
		w.u(12, 0x2); // ES info length
		w.u(8, 0x6a); // ES info: descriptor_tag for AC-3
		w.u(8, 0x0); //  ES info: descriptor_length

		w.u(32, 0x896249fe); // CRC32
	}

	TsDemuxerConfig cfg;
	cfg.pids = {};
	cfg.pids.push_back(TsDemuxerConfig::ANY_VIDEO());
	cfg.pids.push_back(TsDemuxerConfig::ANY_AUDIO());
	cfg.pids.push_back(TsDemuxerConfig::ANY_AUDIO());

	auto demux = loadModule("TsDemuxer", &NullHost, &cfg);

	demux->getInput(0)->push(createPacket(tsPackets));
	demux->flush();

	auto meta0 = safe_cast<const MetadataPkt>(demux->getOutput(0)->getMetadata());
	ASSERT(meta0 != nullptr);
	ASSERT_EQUALS("h264_annexb", meta0->codec);

	auto meta1 = safe_cast<const MetadataPkt>(demux->getOutput(1)->getMetadata());
	ASSERT(meta1 != nullptr);
	ASSERT_EQUALS("mp2", meta1->codec);

	auto meta2 = safe_cast<const MetadataPkt>(demux->getOutput(2)->getMetadata());
	ASSERT(meta2 != nullptr);
	ASSERT_EQUALS("ac3", meta2->codec);
}

fuzztest("TsDemuxer") {
	SpanC testdata;
	GetFuzzTestData(testdata.ptr, testdata.len);

	TsDemuxerConfig cfg;
	cfg.pids = {};
	cfg.pids.push_back(TsDemuxerConfig::ANY_VIDEO());
	cfg.pids.push_back(TsDemuxerConfig::ANY_AUDIO());

	auto demux = loadModule("TsDemuxer", &NullHost, &cfg);

	demux->getInput(0)->push(createPacket(testdata));
	demux->flush();
}

