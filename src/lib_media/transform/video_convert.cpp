#include "lib_modules/utils/helper.hpp" // ModuleS
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_media/common/picture.hpp" // PictureFormat
#include "lib_utils/tools.hpp"
#include "lib_ffpp/ffpp.hpp"
#include "../common/libav.hpp"

extern "C" {
#include <libswscale/swscale.h>
}

#define ALIGN_PAD(n, align, pad) ((n/align + 1) * align + pad)

using namespace Modules;

namespace {

class VideoConvert : public ModuleS {
	public:
		VideoConvert(IModuleHost* host, const PictureFormat &dstFormat);
		~VideoConvert();
		void process(Data data) override;

	private:
		void reconfigure(const PictureFormat &format);

		IModuleHost* const m_host;
		SwsContext *m_SwContext;
		PictureFormat srcFormat, dstFormat;
		OutputPicture* output;
};

VideoConvert::VideoConvert(IModuleHost* host, const PictureFormat &dstFormat)
	: m_host(host),
	  m_SwContext(nullptr), dstFormat(dstFormat) {
	auto input = createInput(this);
	input->setMetadata(make_shared<MetadataRawVideo>());
	output = addOutput<OutputPicture>();
}

void VideoConvert::reconfigure(const PictureFormat &format) {
	sws_freeContext(m_SwContext);
	m_SwContext = sws_getContext(format.res.width, format.res.height, pixelFormat2libavPixFmt(format.format),
	        dstFormat.res.width, dstFormat.res.height, pixelFormat2libavPixFmt(dstFormat.format),
	        SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!m_SwContext)
		throw error("Impossible to set up video converter.");
	srcFormat = format;
}

VideoConvert::~VideoConvert() {
	sws_freeContext(m_SwContext);
}

void VideoConvert::process(Data data) {
	auto videoData = safe_cast<const DataPicture>(data);
	if (videoData->getFormat() != srcFormat) {
		if (m_SwContext)
			m_host->log(Info, "Incompatible input video data. Reconfiguring.");
		reconfigure(videoData->getFormat());
	}

	uint8_t const* srcSlice[8] {};
	int srcStride[8] {};
	for (size_t i=0; i<videoData->getNumPlanes(); ++i) {
		srcSlice[i] = videoData->getPlane(i);
		srcStride[i] = (int)videoData->getPitch(i);
	}

	std::shared_ptr<DataBase> out;
	uint8_t* pDst[8] {};
	int dstStride[8] {};
	switch (dstFormat.format) {
	case Y8: case YUV420P: case YUV420P10LE: case YUV422P: case YUV422P10LE: case YUYV422: case NV12: case RGB24: case RGBA32: {
		auto resInternal = Resolution(ALIGN_PAD(dstFormat.res.width, 16, 0), ALIGN_PAD(dstFormat.res.height, 8, 0));
		auto pic = DataPicture::create(output, dstFormat.res, resInternal, dstFormat.format);
		for (size_t i=0; i<pic->getNumPlanes(); ++i) {
			pDst[i] = pic->getPlane(i);
			dstStride[i] = (int)pic->getPitch(i);
		}
		out = pic;
		break;
	}
	default:
		throw error("Destination colorspace not supported.");
	}

	sws_scale(m_SwContext, srcSlice, srcStride, 0, srcFormat.res.height, pDst, dstStride);

	out->setMediaTime(data->getMediaTime());
	output->emit(out);
}

Modules::IModule* createObject(IModuleHost* host, va_list va) {
	auto fmt = va_arg(va, PictureFormat*);
	enforce(host, "VideoConvert: host can't be NULL");
	return Modules::create<VideoConvert>(host, *fmt).release();
}

auto const registered = registerModule("VideoConvert", &createObject);
}
