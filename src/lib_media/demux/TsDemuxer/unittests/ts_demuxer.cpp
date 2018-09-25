#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "../ts_demuxer.hpp"
#include "string.h" // memcpy

using namespace Tests;
using namespace Modules;

template<size_t N>
std::shared_ptr<DataBase> createPacket(uint8_t const (&bytes)[N]) {
	auto pkt = make_shared<DataRaw>(N);
	memcpy(pkt->data().ptr, bytes, N);
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

	void putBit(int bit) {
		auto bitIndex = m_pos%8;
		auto byteIndex = m_pos/8;
		auto mask = (1 << (7-bitIndex));
		if(bit)
			dst.ptr[byteIndex] |= mask;
		else
			dst.ptr[byteIndex] &= ~mask;
		m_pos++;
	}

	int m_pos = 0;
};

std::shared_ptr<DataBase> getTestTs() {
	uint8_t tsPacket[188] {};
	BitWriter w { {tsPacket, sizeof tsPacket} };
	w.u(8, 0x47); // sync byte
	w.u(1, 0); // TEI
	w.u(1, 0); // PUSI
	w.u(1, 0); // priority
	w.u(13, 120); // PID
	w.u(2, 0); // scrambling control
	w.u(2, 0); // adaptation field control
	w.u(4, 0); // continuity counter

	return createPacket(tsPacket);
}
}

unittest("TsDemuxer: simple") {
	struct FrameCounter : ModuleS {
		FrameCounter() {
			addInput(this);
		}
		void process(Data) override {
			++frameCount;
		}
		int frameCount = 0;
	};

	TsDemuxerConfig cfg;
	cfg.pids[0].pid = 130;
	cfg.pids[0].type = 1;
	cfg.pids[1].pid = 120;
	cfg.pids[1].type = 1;

	auto demux = loadModule("TsDemuxer", &NullHost, &cfg);
	auto rec120 = create<FrameCounter>();
	auto rec130 = create<FrameCounter>();
	ConnectOutputToInput(demux->getOutput(0), rec120->getInput(0));
	ConnectOutputToInput(demux->getOutput(1), rec130->getInput(0));

	auto frame = getTestTs();
	demux->getInput(0)->push(frame);
	demux->process();

	ASSERT_EQUALS(0, rec120->frameCount);
	ASSERT_EQUALS(1, rec130->frameCount);
}

