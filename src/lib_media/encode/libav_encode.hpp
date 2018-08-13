#pragma once

#include "../common/pcm.hpp"
#include "../common/picture.hpp"
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

#include "lib_modules/utils/helper.hpp" // ModuleS

struct AVCodecContext;
struct AVStream;
struct AVFrame;

namespace ffpp {
class Frame;
}

namespace Modules {
class DataAVPacket;

namespace Encode {

class LibavEncode : public ModuleS {
	public:
		LibavEncode(IModuleHost* host, EncoderConfig* params);
		~LibavEncode();
		void process(Data data) override;
		void flush() override;

	private:
		void encodeFrame(AVFrame* frame);
		int64_t computeNearestGOPNum(int64_t timeDiff) const;
		void computeFrameAttributes(AVFrame * const f, const int64_t currMediaTime);
		void setMediaTime(std::shared_ptr<DataAVPacket> data);

		IModuleHost* const m_host;
		std::shared_ptr<AVCodecContext> codecCtx;
		std::unique_ptr<PcmFormat> pcmFormat;
		std::unique_ptr<ffpp::Frame> const avFrame;
		OutputDataDefault<DataAVPacket>* output {};
		int64_t firstMediaTime = 0;
		int64_t prevMediaTime = 0;
		Fraction GOPSize {};
};

}
}
