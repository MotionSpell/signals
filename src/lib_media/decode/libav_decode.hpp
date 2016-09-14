#pragma once

#include "lib_modules/core/module.hpp"
#include "../common/libav.hpp"
#include "../common/pcm.hpp"

struct AVCodecContext;

namespace ffpp {
class Frame;
}

class AudioConverter;

namespace Modules {
namespace Decode {

class LibavDecode : public ModuleS {
	public:
		LibavDecode(const MetadataPktLibav &metadata);
		~LibavDecode();
		void process(Data data) override;
		void flush() override;

	private:
		bool processAudio(const DataAVPacket*);
		bool processVideo(const DataAVPacket*);

		AVCodecContext * const codecCtx;
		std::unique_ptr<ffpp::Frame> const avFrame;
		int64_t cumulatedDuration = 0;
		OutputPicture* videoOutput;
		OutputPcm* audioOutput;
};

}
}
