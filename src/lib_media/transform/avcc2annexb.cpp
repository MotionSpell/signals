#include "avcc2annexb.hpp"
#include "lib_media/common/libav.hpp"
#include "lib_ffpp/ffpp.hpp"

extern "C" {
#include <libavformat/avformat.h> // av_packet_copy_props
}

namespace Modules {
namespace Transform {

struct ByteReader {
	SpanC data;
	size_t pos = 0;

	size_t available() const {
		return data.len - pos;
	}

	uint8_t u8() {
		return data.ptr[pos++];
	}

	uint32_t u32() {
		uint32_t r = 0;
		for(int i=0; i < 4; ++i) {
			r <<= 8;
			r += u8();
		}
		return r;
	}

	void read(Span out) {
		memcpy(out.ptr, data.ptr+pos, out.len);
		pos+=out.len;
	}
};

AVCC2AnnexBConverter::AVCC2AnnexBConverter() {
	auto input = createInput(this);
	output = addOutput<OutputDataDefault<DataAVPacket>>(input->getMetadata());
}

void AVCC2AnnexBConverter::process(Data in) {
	auto out = output->getBuffer(in->data().len);
	av_packet_copy_props(out->getPacket(), safe_cast<const DataAVPacket>(in)->getPacket());

	auto bs = ByteReader { in->data() };
	while ( auto availableBytes = bs.available() ) {
		if (availableBytes < 4) {
			log(Error, "Need to read 4 byte start-code, only %s available. Exit current conversion.", availableBytes);
			break;
		}
		auto const size = bs.u32();
		if (size + 4 > availableBytes) {
			log(Error, "Too much data read: %s (available: %s - 4) (total %s). Exit current conversion.", size, availableBytes, in->data().len);
			break;
		}
		// write start code
		auto bytes = out->data().ptr + bs.pos;
		*bytes++ = 0x00;
		*bytes++ = 0x00;
		*bytes++ = 0x00;
		*bytes++ = 0x01;
		bs.read({out->data().ptr + bs.pos + 4, size});
	}

	out->setMetadata(in->getMetadata());
	out->setMediaTime(in->getMediaTime());
	output->emit(out);
}

}
}
