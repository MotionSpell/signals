#include "libav_encode.hpp"
#include "lib_modules/utils/helper.hpp" // ModuleS
#include "lib_modules/utils/factory.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/log.hpp"
#include "../common/ffpp.hpp"
#include "../common/pcm.hpp"
#include "../common/libav.hpp"

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavcodec/avcodec.h> // avcodec_open2
}

using namespace Modules;

namespace {

AVRational toAVRational(Fraction f) {
	return {(int)f.num, (int)f.den};
}

struct LibavEncode : ModuleS {
		LibavEncode(IModuleHost* host, EncoderConfig *pparams)
			: m_host(host),
			  params(*pparams),
			  avFrame(new ffpp::Frame) {

			auto const type = params.type;
			std::string generalOptions;
			switch (type) {
			case EncoderConfig::Video: {
				GOPSize = params.GOPSize;
				codecOptions += format(" -b %s", params.bitrate);
				codecName = "vcodec";
				ffpp::Dict customDict(typeid(*this).name(), params.avcodecCustom);
				auto const pixFmt = customDict.get("pix_fmt");
				if (pixFmt) {
					generalOptions += format(" -pix_fmt %s", pixFmt->value);
				}
				auto const codec = customDict.get("vcodec");
				if (codec) {
					generalOptions += format(" -vcodec %s", codec->value);
					av_dict_free(&customDict);
					break;
				}
				av_dict_free(&customDict);
				codecOptions += " -forced-idr 1";
				switch (params.codecType) {
				case VideoCodecType::Software:
					generalOptions += " -vcodec libx264";
					if (params.isLowLatency) {
						codecOptions += " -preset ultrafast -tune zerolatency";
					} else {
						codecOptions += " -preset veryfast";
					}
					break;
				case VideoCodecType::Hardware_qsv:
					generalOptions += " -vcodec h264_qsv";
					break;
				case VideoCodecType::Hardware_nvenc:
					generalOptions += " -vcodec h264_nvenc";
					break;
				default:
					throw error("Unknown video encoder type. Failed.");
				}
				codecOptions += " -bf 0";
				break;
			}
			case EncoderConfig::Audio: {
				codecName = "acodec";
				ffpp::Dict customDict(typeid(*this).name(), params.avcodecCustom);
				auto const codec = customDict.get("acodec");
				if (codec) {
					generalOptions += format(" -acodec %s", codec->value);
					av_dict_free(&customDict);
					break;
				}
				av_dict_free(&customDict);
				codecOptions += format(" -b %s", params.bitrate);
				generalOptions += " -acodec aac -profile aac_low";
				GOPSize = 1;
				break;
			}
			default:
				throw error("Unknown encoder type. Failed.");
			}

			/* find the encoder */
			ffpp::Dict generalDict(typeid(*this).name(), generalOptions);
			auto const entry = generalDict.get(codecName);
			if (!entry)
				throw error(format("Could not get codecName (\"%s\").", codecName));
			m_codec = avcodec_find_encoder_by_name(entry->value);
			if (!m_codec) {
				auto desc = avcodec_descriptor_get_by_name(entry->value);
				if (!desc)
					throw error(format("Codec descriptor '%s' not found, disable output.", entry->value));
				m_codec = avcodec_find_encoder(desc->id);
			}
			if (!m_codec)
				throw error(format("Codec '%s' not found, disable output.", entry->value));
			codecCtx = shptr(avcodec_alloc_context3(m_codec));
			if (!codecCtx)
				throw error(format("Could not allocate the m_codec context (\"%s\").", codecName));

			auto input = addInput(this);
			output = addOutput<OutputDataDefault<DataAVPacket>>();

			// Make ffmpeg use the same time scale as the framework:
			// thus, no timestamp conversion is needed.
			codecCtx->time_base.num = 1;
			codecCtx->time_base.den = IClock::Rate;

			// encoder configuration
			switch (type) {
			case EncoderConfig::Video: {
				if (generalDict.get("pix_fmt")) {
					codecCtx->pix_fmt = av_get_pix_fmt(generalDict.get("pix_fmt")->value);
					if (codecCtx->pix_fmt == AV_PIX_FMT_NONE)
						throw error(format("Unknown pixel format\"%s\".", generalDict.get("pix_fmt")->value));
					av_dict_set(&generalDict, "pix_fmt", nullptr, 0);
				} else if (!strcmp(generalDict.get("vcodec")->value, "mjpeg")) {
					codecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
				} else if (!strcmp(generalDict.get("vcodec")->value, "h264_qsv")) {
					codecCtx->pix_fmt = AV_PIX_FMT_NV12;
				} else {
					codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
				}

				// output
				pparams->pixelFormat = libavPixFmt2PixelFormat(codecCtx->pix_fmt);

				framePeriod = params.frameRate.inverse();

				prepareFrame = std::bind(&LibavEncode::prepareVideoFrame, this, std::placeholders::_1);
				input->setMetadata(make_shared<MetadataRawVideo>());
				break;
			}
			case EncoderConfig::Audio:

				prepareFrame = std::bind(&LibavEncode::prepareAudioFrame, this, std::placeholders::_1);
				input->setMetadata(make_shared<MetadataRawAudio>());
				break;
			default:
				throw error(format("Invalid codec type: %d", type));
			}

			av_dict_free(&generalDict);
			avFrame->get()->pts = std::numeric_limits<int64_t>::min();
		}

		AVFrame* prepareAudioFrame(Data data) {
			AVFrame *f = avFrame->get();
			const auto pcmData = safe_cast<const DataPcm>(data);
			if (pcmData->getFormat() != *pcmFormat)
				throw error("Incompatible audio data (1)");
			libavFrameDataConvert(pcmData.get(), f);
			return f;
		}

		AVFrame* prepareVideoFrame(Data data) {
			const auto pic = safe_cast<const DataPicture>(data);
			AVFrame *f = avFrame->get();
			f->format = (int)pixelFormat2libavPixFmt(pic->getFormat().format);
			for (size_t i = 0; i < pic->getNumPlanes(); ++i) {
				f->width = pic->getFormat().res.width;
				f->height = pic->getFormat().res.height;
				f->data[i] = (uint8_t*)pic->getPlane(i);
				f->linesize[i] = (int)pic->getPitch(i);
			}
			computeFrameAttributes(f, data->getMediaTime());
			return f;
		}

		void process(Data data) {
			if(!m_isOpen) {
				openEncoder(data);
				m_isOpen = true;
			}

			auto f = prepareFrame(data);
			f->pts = data->getMediaTime();
			encodeFrame(f);
		}

		void flush() {
			if (codecCtx && (codecCtx->codec->capabilities & AV_CODEC_CAP_DELAY)) {
				encodeFrame(nullptr);
			}
		}

		int64_t computeNearestGOPNum(int64_t timeDiff) const {
			auto const num = timeDiff * GOPSize.den * framePeriod.den;
			auto const den = GOPSize.num * framePeriod.num * (int64_t)IClock::Rate;
			auto const halfStep = (den * framePeriod.num) / (framePeriod.den * 2);
			return (num + halfStep - 1) / den;
		}

		void computeFrameAttributes(AVFrame * const f, const int64_t currMediaTime) {
			if (f->pts == std::numeric_limits<int64_t>::min()) {
				firstMediaTime = currMediaTime;
				f->key_frame = 1;
				f->pict_type = AV_PICTURE_TYPE_I;
			} else {
				auto const prevGOP = computeNearestGOPNum(prevMediaTime - firstMediaTime);
				auto const currGOP = computeNearestGOPNum(currMediaTime - firstMediaTime);
				if (prevGOP != currGOP) {
					if (currGOP != prevGOP + 1) {
						m_host->log(Warning, format("Invalid content: switching from GOP %s to GOP %s - inserting RAP.", prevGOP, currGOP).c_str());
					}
					f->key_frame = 1;
					f->pict_type = AV_PICTURE_TYPE_I;
				} else {
					f->key_frame = 0;
					f->pict_type = AV_PICTURE_TYPE_NONE;
				}
			}
			prevMediaTime = currMediaTime;
		}

		void encodeFrame(AVFrame* f) {
			int ret;

			ret = avcodec_send_frame(codecCtx.get(), f);
			if (ret != 0) {
				auto desc = f ? format("pts=%s", f->pts) : format("flush");
				m_host->log(Warning, format("error encountered while encoding frame (%s) : %s", desc, avStrError(ret)).c_str());
				return;
			}

			while(1) {
				auto out = output->getBuffer(0);
				ret = avcodec_receive_packet(codecCtx.get(), out->getPacket());
				if(ret != 0)
					break;

				if(out->getPacket()->flags & AV_PKT_FLAG_KEY)
					out->flags |= DATA_FLAGS_KEYFRAME;

				out->setMediaTime(out->getPacket()->pts);
				out->setDecodingTime(out->getPacket()->dts);
				output->emit(out);
			}
		}

	private:
		IModuleHost* const m_host;
		EncoderConfig const params;
		std::shared_ptr<AVCodecContext> codecCtx;
		std::unique_ptr<PcmFormat> pcmFormat;
		std::unique_ptr<ffpp::Frame> const avFrame;
		OutputDataDefault<DataAVPacket>* output {};
		int64_t firstMediaTime = 0;
		int64_t prevMediaTime = 0;
		Fraction GOPSize {};
		Fraction framePeriod {};
		std::function<AVFrame*(Data)> prepareFrame;
		std::string codecOptions, codecName;
		AVCodec* m_codec = nullptr;
		bool m_isOpen = false;

		void openEncoder(Data data) {
			auto codecOptions = this->codecOptions;
			if(!data)
				throw error("Can't open encoder: no data");

			// input format configuration
			switch (params.type) {
			case EncoderConfig::Video: {
				const auto fmt = safe_cast<const DataPicture>(data.get())->getFormat();
				codecCtx->width = fmt.res.width;
				codecCtx->height = fmt.res.height;

				// for VUI signalisation
				codecCtx->framerate = toAVRational(framePeriod.inverse());

				// for encoding level checks (MB rate) and rate control
				codecCtx->ticks_per_frame = int(framePeriod * IClock::Rate);

				output->setMetadata(createMetadataPktLibavVideo(codecCtx.get()));
				break;
			}
			case EncoderConfig::Audio: {
				const auto fmt = safe_cast<const DataPcm>(data)->getFormat();
				AudioLayout layout;
				switch (fmt.numChannels) {
				case 1: layout = Modules::Mono; break;
				case 2: layout = Modules::Stereo; break;
				default: throw error("Unknown libav audio layout");
				}
				pcmFormat = make_unique<PcmFormat>(fmt.sampleRate, fmt.numChannels, layout);
				libavAudioCtxConvert(pcmFormat.get(), codecCtx.get());
				codecOptions += format(" -ar %s -ac %s", fmt.sampleRate, fmt.numChannels);

				output->setMetadata(createMetadataPktLibavAudio(codecCtx.get()));
				break;
			}
			default:
				throw error(format("Invalid codec type: %d", params.type));
			}

			/* open it */
			codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; //gives access to the extradata (e.g. H264 SPS/PPS, etc.)
			ffpp::Dict codecDict(typeid(*this).name(), codecOptions + " -threads auto " + params.avcodecCustom);
			av_dict_set(&codecDict, codecName.c_str(), nullptr, 0);
			if (avcodec_open2(codecCtx.get(), m_codec, &codecDict) < 0)
				throw error(format("Could not open codec %s, disable output.", codecName));
			codecDict.ensureAllOptionsConsumed();
		}

};

Modules::IModule* createObject(IModuleHost* host, va_list va) {
	auto config = va_arg(va, EncoderConfig*);
	enforce(host, "Encoder: host can't be NULL");
	return Modules::createModule<LibavEncode>(config->bufferSize, host, config).release();
}

auto const registered = Factory::registerModule("Encoder", &createObject);
}
