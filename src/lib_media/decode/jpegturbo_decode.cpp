#include "jpegturbo_decode.hpp"
#include "../common/libav.hpp"
#include "lib_utils/tools.hpp"

extern "C" {
#include <turbojpeg.h>
#include <libavutil/pixfmt.h>
}

namespace Modules {
namespace Decode {

class JPEGTurbo {
public:
	JPEGTurbo() {
		handle = tjInitDecompress();
	}
	~JPEGTurbo() {
		tjDestroy(handle);
	}
	tjhandle get() {
		return handle;
	}

private:
	tjhandle handle;
};

AVPixelFormat getAVPF(int JPEGTurboPixelFmt) {
	switch (JPEGTurboPixelFmt) {
	case TJPF_RGB: return AV_PIX_FMT_RGB24;
	default: throw std::runtime_error("[JPEGTurboDecode] Unsupported pixel format conversion. Failed.");
	}
	return AV_PIX_FMT_NONE;
}

JPEGTurboDecode::JPEGTurboDecode()
: jtHandle(new JPEGTurbo) {
	auto input = addInput(new Input<DataBase>(this));
	input->setMetadata(shptr(new MetadataPktVideo));
	output = addOutput<OutputPicture>(shptr(new MetadataRawVideo));
}

JPEGTurboDecode::~JPEGTurboDecode() {
}

void JPEGTurboDecode::ensureMetadata(int /*width*/, int /*height*/, int /*pixelFmt*/) {
	if (!output->getMetadata()) {
		auto p = safe_cast<const MetadataRawVideo>(output->getMetadata());
		//TODO: add resolution and pixel format to MetadataRawVideo
		//ctx->width = width;
		//ctx->height = height;
		//ctx->pix_fmt = getAVPF(pixelFmt);
		//output->setMetadata(new MetadataRawVideo(ctx));
	}
}

void JPEGTurboDecode::process(Data data_) {
	auto data = safe_cast<const DataBase>(data_);
	const int pixelFmt = TJPF_RGB;
	int w=0, h=0, jpegSubsamp=0;
	auto jpegBuf = data->data();
	if (tjDecompressHeader2(jtHandle->get(), (unsigned char*)jpegBuf, (unsigned long)data->size(), &w, &h, &jpegSubsamp) < 0) {
		log(Warning, "error encountered while decompressing header.");
		return;
	}
	auto out = DataPicture::create(output, Resolution(w, h), RGB24);
	if (tjDecompress2(jtHandle->get(), (unsigned char*)jpegBuf, (unsigned long)data->size(), out->data(), w, 0/*pitch*/, h, pixelFmt, TJFLAG_FASTDCT) < 0) {
		log(Warning, "error encountered while decompressing frame.");
		return;
	}
	ensureMetadata(w, h, pixelFmt);
	out->setMediaTime(data->getMediaTime());
	output->emit(out);
}

}
}
