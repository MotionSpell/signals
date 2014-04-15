#include "tests.hpp"
#include "modules.hpp"
#include <memory>

#include "demux/libav_demux.hpp"

using namespace Tests;
using namespace Modules;

namespace {

unittest("Packet type erasure + multi-output-pin: libav Demux -> {libav Decoder -> Out::Print}*") {
	std::unique_ptr<Demux::LibavDemux> demux(Demux::LibavDemux::create("data/BatmanHD_1000kbit_mpeg_0_20_frag_1000.mp4"));
	ASSERT(demux != nullptr);

	std::vector<std::unique_ptr<Decode::LibavDecode>> decoders;
	std::vector<std::unique_ptr<Out::Print>> printers;
	for (size_t i = 0; i < demux->getNumPin(); ++i) {
		Props *props = demux->getPin(i)->getProps();
		PropsDecoder *decoderProps = dynamic_cast<PropsDecoder*>(props);
		ASSERT(decoderProps);
		std::unique_ptr<Decode::LibavDecode> decode(Decode::LibavDecode::create(*decoderProps));
		ASSERT(decode != nullptr);

		std::unique_ptr<Out::Print> p(Out::Print::create(std::cout));
		ASSERT(p != nullptr);

		ConnectPin(demux->getPin(i), decode.get(), &Decode::LibavDecode::process);
		ConnectPin(decode->getPin(0), p.get(), &Out::Print::process);

		decoders.push_back(std::move(decode));
		printers.push_back(std::move(p));
	}


	while (demux->process(nullptr)) {
	}

	demux->waitForCompletion();
	for (size_t i = 0; i < demux->getNumPin(); ++i) {
		decoders[i]->waitForCompletion();
	}
}

}
