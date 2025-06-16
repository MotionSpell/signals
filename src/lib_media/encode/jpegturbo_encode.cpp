#include "lib_modules/utils/factory.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/format.hpp"
#include "../common/attributes.hpp"
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
		JPEGTurboEncode(KHost* host_, int quality = JPEG_DEFAULT_QUALITY)
			: m_host(host_),
			  jtHandle(tjInitCompress()), quality(quality) {
			input->setMetadata(make_shared<MetadataRawVideo>());
			output = addOutput();
		}

		~JPEGTurboEncode() {
			tjDestroy(jtHandle);
		}

		void processOne(Data data_) override {
			auto videoData = safe_cast<const DataPicture>(data_);
			auto const dim = videoData->getFormat().res;
			auto const dataSize = tjBufSize(dim.width, dim.height, TJSAMP_420);
			auto out = output->allocData<DataRaw>(dataSize);
			unsigned char *buf = (unsigned char*)out->data().ptr;
			auto jpegBuf = videoData->data().ptr;
			unsigned long jpegSize;

			switch (videoData->getFormat().format) {
			case PixelFormat::Y8:
			case PixelFormat::I420:
			case PixelFormat::YUV420P10LE:
			case PixelFormat::YUV422P:
			case PixelFormat::YUV422P10LE:
			case PixelFormat::YUYV422:
			case PixelFormat::NV12: {
				uint8_t const* srcSlice[8] {};
				int srcStride[8] {};
				for (int i = 0; i<videoData->getNumPlanes(); ++i) {
					srcSlice[i] = videoData->getPlane(i);
					srcStride[i] = (int)videoData->getStride(i);
				}

				if (tjCompressFromYUVPlanes(jtHandle,
				        srcSlice, videoData->getFormat().res.width, srcStride, videoData->getFormat().res.height, TJSAMP_420,
				        &buf, &jpegSize, quality, TJFLAG_NOREALLOC | TJFLAG_FASTDCT)) {
					m_host->log(Warning, "error encountered while compressing (YUV).");
					return;
				}
				break;
			}
			case PixelFormat::RGB24:
			case PixelFormat::RGBA32: {
				if (tjCompress2(jtHandle, (unsigned char*)jpegBuf, dim.width, 0/*pitch*/, dim.height, TJPF_RGB, &buf, &jpegSize, TJSAMP_420, quality, TJFLAG_NOREALLOC | TJFLAG_FASTDCT) < 0) {
					m_host->log(Warning, "error encountered while compressing (RGB).");
					return;
				}
				break;
			}
			default:
				throw error(format("Unsupported colorspace %s", (int)videoData->getFormat().format));
			}

			out->buffer->resize(jpegSize);
			out->set(data_->get<PresentationTime>());
			output->post(out);
		}

	private:
		KHost* const m_host;
		OutputDefault* output;
		tjhandle jtHandle;
		int quality;
};

IModule* createObject(KHost* host, void* va) {
	(void)va;
	enforce(host, "JPEGTurboEncode: host can't be NULL");
	return createModule<JPEGTurboEncode>(host).release();
}

auto const registered = Factory::registerModule("JPEGTurboEncode", &createObject);
}
