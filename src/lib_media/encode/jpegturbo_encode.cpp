#include "lib_modules/utils/factory.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/log.hpp"
#include "../common/metadata.hpp"
#include "../common/picture.hpp"
extern "C" {
#include <turbojpeg.h>
}

#define JPEG_DEFAULT_QUALITY 70

using namespace Modules;

namespace {

class JPEGTurboEncode : public ModuleS {
	public:
		JPEGTurboEncode(IModuleHost* host, int quality = JPEG_DEFAULT_QUALITY);
		~JPEGTurboEncode();
		void process(Data data) override;

	private:
		IModuleHost* const m_host;
		OutputDefault* output;
		tjhandle jtHandle;
		int quality;
};

JPEGTurboEncode::JPEGTurboEncode(IModuleHost* host_, int quality)
	: m_host(host_),
	  jtHandle(tjInitCompress()), quality(quality) {
	auto input = createInput(this);
	input->setMetadata(make_shared<MetadataRawVideo>());
	output = addOutput<OutputDefault>();
}

JPEGTurboEncode::~JPEGTurboEncode() {
	tjDestroy(jtHandle);
}

void JPEGTurboEncode::process(Data data_) {
	auto videoData = safe_cast<const DataPicture>(data_);
	auto const w = videoData->getFormat().res.width, h = videoData->getFormat().res.height;
	auto const dataSize = tjBufSize(w, h, TJSAMP_420);
	auto out = output->getBuffer(dataSize);
	unsigned char *buf = (unsigned char*)out->data().ptr;
	auto jpegBuf = videoData->data().ptr;
	unsigned long jpegSize;

	switch (videoData->getFormat().format) {
	case Y8: case YUV420P: case YUV420P10LE: case YUV422P: case YUV422P10LE: case YUYV422: case NV12: {
		uint8_t const* srcSlice[8] {};
		int srcStride[8] {};
		for (size_t i = 0; i<videoData->getNumPlanes(); ++i) {
			srcSlice[i] = videoData->getPlane(i);
			srcStride[i] = (int)videoData->getPitch(i);
		}

		if (tjCompressFromYUVPlanes(jtHandle,
		        srcSlice, videoData->getFormat().res.width, srcStride, videoData->getFormat().res.height, TJSAMP_420,
		        &buf, &jpegSize, quality, TJFLAG_NOREALLOC | TJFLAG_FASTDCT)) {
			m_host->log(Warning, "error encountered while compressing (YUV).");
			return;
		}
		break;
	}
	case RGB24: case RGBA32: {
		if (tjCompress2(jtHandle, (unsigned char*)jpegBuf, w, 0/*pitch*/, h, TJPF_RGB, &buf, &jpegSize, TJSAMP_420, quality, TJFLAG_NOREALLOC | TJFLAG_FASTDCT) < 0) {
			m_host->log(Warning, "error encountered while compressing (RGB).");
			return;
		}
		break;
	}
	default:
		throw error(format("Unsupported colorspace %s", videoData->getFormat().format));
	}

	out->resize(jpegSize);
	out->setMediaTime(data_->getMediaTime());
	output->emit(out);
}

IModule* createObject(IModuleHost* host, va_list va) {
	(void)va;
	enforce(host, "JPEGTurboEncode: host can't be NULL");
	return create<JPEGTurboEncode>(host).release();
}

auto const registered = Factory::registerModule("JPEGTurboEncode", &createObject);
}
