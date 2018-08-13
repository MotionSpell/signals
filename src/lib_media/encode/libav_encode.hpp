#pragma once

#include "lib_modules/core/log.hpp"
#include "lib_modules/utils/helper.hpp"
#include "../common/pcm.hpp"
#include "../common/picture.hpp"
#include "lib_utils/queue.hpp"
#include <string>

struct EncoderConfig {
	enum Type {
		Video,
		Audio,
	};

	Type type;
	int bitrate = 128000;

	//video only
	Resolution res = Resolution(320, 180);
	Fraction GOPSize = Fraction(25, 1);
	Fraction frameRate = Fraction(25, 1);
	bool isLowLatency = false;
	Modules::VideoCodecType codecType = Modules::Software;
	Modules::PixelFormat pixelFormat = Modules::UNKNOWN_PF; //set by the encoder

	//audio only
	int sampleRate = 44100;
	int numChannels = 2;

	std::string avcodecCustom = "";
};

struct AVCodecContext;
struct AVStream;
struct AVFrame;

namespace ffpp {
class Frame;
}

namespace Modules {
class DataAVPacket;

namespace Encode {

class LibavEncode : public ModuleS, private LogCap {
	public:
		LibavEncode(EncoderConfig* params);
		~LibavEncode();
		void process(Data data) override;
		void flush() override;

	private:
		void encodeFrame(AVFrame* frame);
		int64_t computeNearestGOPNum(int64_t timeDiff) const;
		void computeFrameAttributes(AVFrame * const f, const int64_t currMediaTime);
		void setMediaTime(std::shared_ptr<DataAVPacket> data);

		std::shared_ptr<AVCodecContext> codecCtx;
		std::unique_ptr<PcmFormat> pcmFormat = nullptr;
		std::unique_ptr<ffpp::Frame> const avFrame;
		OutputDataDefault<DataAVPacket>* output;
		int64_t firstMediaTime, prevMediaTime;
		Fraction GOPSize;
};

}
}
