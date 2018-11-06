#include "lib_utils/log.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "../common/metadata.hpp"
#include "../common/attributes.hpp"
#include "../common/picture_allocator.hpp"
#include "../common/pcm.hpp"
#include "../common/libav.hpp"
#include "../common/ffpp.hpp"
#include "lib_utils/tools.hpp"
#include <cassert>

extern "C" {
#include <libavcodec/avcodec.h> // avcodec_open2
}

using namespace Modules;

namespace {

struct Decoder : ModuleS, PictureAllocator {

		Decoder(IModuleHost* host, StreamType type)
			: m_host(host), avFrame(new ffpp::Frame) {
			addInput(this);

			if(type == VIDEO_PKT) {
				videoOutput = addOutput<OutputPicture>(make_shared<MetadataRawVideo>());
				getDecompressedData = std::bind(&Decoder::processVideo, this);
			} else if(type == AUDIO_PKT) {
				audioOutput = addOutput<OutputPcm>(make_shared<MetadataRawAudio>());
				getDecompressedData = std::bind(&Decoder::processAudio, this);
			} else
				throw error("Can only decode audio or video.");
		}

		// IModule implementation
		void process(Data data) {
			inputs[0]->updateMetadata(data);

			if(!codecCtx) {
				auto meta = data->getMetadata();
				if(!meta)
					meta = inputs[0]->getMetadata();

				if(!meta)
					throw error("Can't instantiate decoder: no metadata for input data");

				openDecoder(safe_cast<const MetadataPkt>(meta.get()));
			}

			assert(codecCtx);

			auto flags = data->getAttribute<AttributeCueFlags>();
			if (flags.discontinuity) {
				avcodec_flush_buffers(codecCtx.get());
			}

			AVPacket pkt {};
			pkt.pts = data->getMediaTime();
			pkt.data = (uint8_t*)data->data().ptr;
			pkt.size = (int)data->data().len;
			processPacket(&pkt);
		}

		void flush() {
			if (codecCtx.get())
				processPacket(nullptr);
		}

	private:
		void openDecoder(const MetadataPkt* metadata) {
			auto const codec = avcodec_find_decoder_by_name(metadata->codec.c_str());
			if (!codec)
				throw error(format("Decoder not found for codec '%s'.", metadata->codec));

			codecCtx = shptr(avcodec_alloc_context3(codec));

			// copy extradata: this allows decoding headerless bitstreams
			// (i.e AVCC / H264-in-mp4).
			{
				auto const extradata = metadata->codecSpecificInfo;

				codecCtx->extradata = (uint8_t*)av_calloc(1, extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
				codecCtx->extradata_size = (int)extradata.size();
				if(extradata.data())
					memcpy(codecCtx->extradata, extradata.data(), extradata.size());
			}

			ffpp::Dict dict(typeid(*this).name(), "-threads auto -err_detect 1 -flags output_corrupt -flags2 showall");
			if (avcodec_open2(codecCtx.get(), codec, &dict) < 0)
				throw error("Couldn't open stream.");

			if (videoOutput) {
				if (codecCtx->codec->capabilities & AV_CODEC_CAP_DR1) {
					codecCtx->thread_safe_callbacks = 0;
					codecCtx->opaque = static_cast<PictureAllocator*>(this);
					codecCtx->get_buffer2 = avGetBuffer2;

					// use a large number: some H.264 streams require up to 14 simultaneous buffers.
					// this memory is lazily allocated anyway.
					allocatorSize = 64;
					videoOutput->resetAllocator(allocatorSize);
				}
			}
		}

		std::shared_ptr<DataBase> processAudio() {
			auto out = audioOutput->getBuffer(0);
			PcmFormat pcmFormat;
			libavFrame2pcmConvert(avFrame->get(), &pcmFormat);
			out->setFormat(pcmFormat);
			for (uint8_t i = 0; i < pcmFormat.numPlanes; ++i) {
				out->setPlane(i, avFrame->get()->data[i], avFrame->get()->nb_samples * pcmFormat.getBytesPerSample() / pcmFormat.numPlanes);
			}

			return out;
		}

		std::shared_ptr<DataBase> processVideo() {

			std::shared_ptr<DataPicture> pic;
			if (auto ctx = static_cast<PictureContext*>(avFrame->get()->opaque)) {
				pic = ctx->pic;
				ctx->pic->setVisibleResolution(Resolution(codecCtx->width, codecCtx->height));
			} else {
				pic = DataPicture::create(videoOutput, Resolution(avFrame->get()->width, avFrame->get()->height), libavPixFmt2PixelFormat((AVPixelFormat)avFrame->get()->format));
				copyToPicture(avFrame->get(), pic.get());
			}

			return pic;
		}

		void setMediaTime(DataBase* data) {
			data->setMediaTime(avFrame->get()->pts);
		}

		PictureAllocator::PictureContext* getPicture(Resolution res, Resolution resInternal, PixelFormat format) {
			auto ctx = new PictureAllocator::PictureContext;
			ctx->pic = DataPicture::create(videoOutput, res, resInternal, format);
			return ctx;
		}

		void processPacket(AVPacket const * pkt) {
			int ret;

			ret = avcodec_send_packet(codecCtx.get(), pkt);
			if (ret < 0) {
				m_host->log(Warning, format("Decoding error: %s", avStrError(ret)).c_str());
				return;
			}

			while(1) {
				ret = avcodec_receive_frame(codecCtx.get(), avFrame->get());
				if(ret != 0)
					break; // no more frames

				if (avFrame->get()->decode_error_flags || (avFrame->get()->flags & AV_FRAME_FLAG_CORRUPT)) {
					m_host->log(Error, "Corrupted frame decoded");
				}

				auto data = getDecompressedData();
				setMediaTime(data.get());
				outputs[0]->emit(data);
			}
		}

		IModuleHost* const m_host;
		std::shared_ptr<AVCodecContext> codecCtx;
		std::unique_ptr<ffpp::Frame> const avFrame;
		OutputPicture *videoOutput = nullptr;
		OutputPcm *audioOutput = nullptr;
		std::function<std::shared_ptr<DataBase>(void)> getDecompressedData;
};

Modules::IModule* createObject(IModuleHost* host, va_list va) {
	auto type = (StreamType)va_arg(va, int);
	enforce(host, "Decoder: host can't be NULL");
	return Modules::create<Decoder>(host, type).release();
}

auto const registered = Factory::registerModule("Decoder", &createObject);

}
