#include "lib_utils/log_sink.hpp"
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
#include <libavutil/imgutils.h> // av_image_fill_linesizes
}

using namespace Modules;

namespace {

void lavc_ReleaseFrame(void *opaque, uint8_t * /*data*/) {
	delete static_cast<PictureAllocator::PictureContext*>(opaque);
}

int avGetBuffer2(struct AVCodecContext *ctx, AVFrame *frame, int /*flags*/) {
	try {
		int w = frame->width, wAlign = frame->width, hAlign = frame->height, unaligned = 0, linesize[4] = {}, linesize_align[4] = {};
		avcodec_align_dimensions2(ctx, &w, &hAlign, linesize_align);
		do {
			auto ret = av_image_fill_linesizes(linesize, ctx->pix_fmt, w);
			if (ret < 0)
				return ret;
			wAlign = w;
			// increase alignment of w for next try (rhs gives the lowest bit set in w)
			w += w & ~(w - 1);
			unaligned = 0;
			for (int i = 0; i < 4; i++)
				unaligned |= linesize[i] % linesize_align[i];
		} while (unaligned);
		auto dr = static_cast<PictureAllocator*>(ctx->opaque);
		auto picCtx = dr->getPicture(Resolution(frame->width, frame->height), Resolution(wAlign, hAlign), libavPixFmt2PixelFormat((AVPixelFormat)frame->format));
		if (!picCtx->pic)
			return -1;
		frame->opaque = picCtx;
		auto pic = picCtx->pic.get();
		for (size_t i = 0; i < AV_NUM_DATA_POINTERS; ++i) {
			frame->data[i] = NULL;
			frame->linesize[i] = 0;
			frame->buf[i] = NULL;
		}
		for (int i = 0; i < pic->getNumPlanes(); ++i) {
			frame->data[i] = pic->getPlane(i);
			frame->linesize[i] = (int)pic->getStride(i);
			assert(!(pic->getStride(i) % linesize_align[i]));
			frame->buf[i] = av_buffer_create(frame->data[i], frame->linesize[i], lavc_ReleaseFrame, i == 0 ? (void*)picCtx : nullptr, 0);
		}
		return 0;
	} catch(...) {
		return -1;
	}
}

struct Decoder : ModuleS, PictureAllocator {

		Decoder(KHost* host, StreamType type)
			: m_host(host), avFrame(new ffpp::Frame) {
			mediaOutput = addOutput();
			output = mediaOutput;

			if(type == VIDEO_PKT) {
				output->setMetadata(make_shared<MetadataRawVideo>());
				getDecompressedData = std::bind(&Decoder::processVideo, this);
			} else if(type == AUDIO_PKT) {
				output->setMetadata(make_shared<MetadataRawAudio>());
				getDecompressedData = std::bind(&Decoder::processAudio, this);
			} else
				throw error("Can only decode audio or video.");
		}

		// IModule implementation
		void processOne(Data data) {
			inputs[0]->updateMetadata(data);

			if(!codecCtx) {
				auto meta = data->getMetadata();
				if(!meta)
					throw error("Can't instantiate decoder: no metadata for input data");

				openDecoder(safe_cast<const MetadataPkt>(meta.get()));
			}

			assert(codecCtx);

			if(isDeclaration(data))
				return;

			auto flags = data->get<CueFlags>();
			if (flags.discontinuity) {
				avcodec_flush_buffers(codecCtx.get());
			}

			AVPacket pkt {};
			pkt.pts = data->get<PresentationTime>().time;
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
			auto avcodecId = signalsIdToAvCodecId(metadata->codec.c_str());
			auto const codec = avcodec_find_decoder((AVCodecID)avcodecId);
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

			if (metadata->codec == "raw_video") {
				auto const m = safe_cast<const MetadataPktVideo>(metadata);
				codecCtx->pix_fmt = pixelFormat2libavPixFmt(m->pixelFormat);
				codecCtx->width = m->resolution.width;
				codecCtx->height = m->resolution.height;
			}

			if (metadata->codec == "raw_audio") {
				auto const m = safe_cast<const MetadataPktAudio>(metadata);
				av_channel_layout_default(&codecCtx->ch_layout, m->numChannels);
				codecCtx->sample_fmt = AV_SAMPLE_FMT_S16;
				codecCtx->sample_rate = m->sampleRate;
			}

			ffpp::Dict dict(typeid(*this).name(), "-threads auto -err_detect 1 -flags output_corrupt -flags2 showall");

			if (avcodec_open2(codecCtx.get(), codec, &dict) < 0)
				throw error("Couldn't open stream.");

			if (codecCtx->codec->type == AVMEDIA_TYPE_VIDEO) {
				if (codecCtx->codec->capabilities & AV_CODEC_CAP_DR1) {
					codecCtx->opaque = static_cast<PictureAllocator*>(this);
					codecCtx->get_buffer2 = avGetBuffer2;

					// use a large number: some H.264 streams require up to 14 simultaneous buffers.
					// this memory is lazily allocated anyway.
					allocatorSize = 64;
					mediaOutput->resetAllocator(allocatorSize);
				}
			}
		}

		std::shared_ptr<DataBase> processAudio() {
			auto out = mediaOutput->allocData<DataPcm>(0);
			PcmFormat pcmFormat;
			libavFrame2pcmConvert(avFrame->get(), &pcmFormat);
			out->format = pcmFormat;
			out->setSampleCount(avFrame->get()->nb_samples);
			for (int i = 0; i < pcmFormat.numPlanes; ++i) {
				memcpy(out->getPlane(i), avFrame->get()->data[i], avFrame->get()->nb_samples * pcmFormat.getBytesPerSample() / pcmFormat.numPlanes);
			}

			return out;
		}

		std::shared_ptr<DataBase> processVideo() {

			std::shared_ptr<DataPicture> pic;
			if (auto ctx = static_cast<PictureContext*>(avFrame->get()->opaque)) {
				pic = ctx->pic;
				ctx->pic->setVisibleResolution(Resolution(codecCtx->width, codecCtx->height));
			} else {
				pic = DataPicture::create(mediaOutput, Resolution(avFrame->get()->width, avFrame->get()->height), libavPixFmt2PixelFormat((AVPixelFormat)avFrame->get()->format));
				copyToPicture(avFrame->get(), pic.get());
			}

			return pic;
		}

		PictureAllocator::PictureContext* getPicture(Resolution res, Resolution resInternal, PixelFormat format) {
			auto ctx = new PictureAllocator::PictureContext;
			ctx->pic = DataPicture::create(mediaOutput, res, resInternal, format);
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
				data->set(PresentationTime{avFrame->get()->pts});
				output->post(data);
			}
		}

		KHost* const m_host;
		std::shared_ptr<AVCodecContext> codecCtx;
		std::unique_ptr<ffpp::Frame> const avFrame;
		OutputDefault* mediaOutput = nullptr; // used for allocation
		KOutput* output = nullptr;
		std::function<std::shared_ptr<DataBase>(void)> getDecompressedData;
};

Modules::IModule* createObject(KHost* host, void* va) {
	auto type = (StreamType)(uintptr_t)va;
	enforce(host, "Decoder: host can't be NULL");
	return Modules::createModule<Decoder>(host, type).release();
}

auto const registered = Factory::registerModule("Decoder", &createObject);

}
