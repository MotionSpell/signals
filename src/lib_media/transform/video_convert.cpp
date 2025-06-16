#include "lib_modules/utils/helper.hpp" // ModuleS
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_media/common/picture.hpp" // PictureFormat
#include "lib_utils/tools.hpp"
#include "../common/ffpp.hpp"
#include "../common/libav.hpp"
#include "../common/attributes.hpp"

#include <cassert>

extern "C" {
#include <libswscale/swscale.h>
}

static size_t ALIGN_PAD(size_t n, size_t align) {
	return ((n + align - 1)/align) * align;
}

using namespace Modules;

namespace {

class VideoConvert : public ModuleS {
	public:
		VideoConvert(KHost* host, const PictureFormat &dstFormat)
			: m_host(host),
			  m_SwContext(nullptr), dstFormat(dstFormat) {
			input->setMetadata(make_shared<MetadataRawVideo>());
			output = addOutput();
		}

		~VideoConvert() {
			sws_freeContext(m_SwContext);
		}

		void processOne(Data data) override {
			auto videoData = safe_cast<const DataPicture>(data);

			// pass-through case
			if(videoData->getFormat() == dstFormat) {
				output->post(data);
				return;
			}

			if (videoData->getFormat() != srcFormat) {
				reconfigure(videoData->getFormat());
			}

			uint8_t const* srcSlice[8] {};
			int srcStride[8] {};
			for (int i=0; i<videoData->getNumPlanes(); ++i) {
				srcSlice[i] = videoData->getPlane(i);
				srcStride[i] = (int)videoData->getStride(i);
			}

			uint8_t* pDst[8] {};
			int dstStride[8] {};
			if(dstFormat.format == PixelFormat::UNKNOWN)
				throw error("Destination colorspace not supported.");

			auto resInternal = Resolution(ALIGN_PAD(dstFormat.res.width, 16 * 2), ALIGN_PAD(dstFormat.res.height, 8));
			auto pic = DataPicture::create(output, dstFormat.res, resInternal, dstFormat.format);
			for (int i=0; i<pic->getNumPlanes(); ++i) {
				pDst[i] = pic->getPlane(i);
				dstStride[i] = (int)pic->getStride(i);
				assert(dstStride[i]%16 == 0); // otherwise, sws_scale will crash
			}

			sws_scale(m_SwContext, srcSlice, srcStride, 0, srcFormat.res.height, pDst, dstStride);

			pic->set(data->get<PresentationTime>());
			output->post(pic);
		}

	private:
		void reconfigure(const PictureFormat &srcFormat) {
			sws_freeContext(m_SwContext);
			m_SwContext = sws_getContext(
			        srcFormat.res.width, srcFormat.res.height,
			        pixelFormat2libavPixFmt(srcFormat.format),
			        dstFormat.res.width, dstFormat.res.height,
			        pixelFormat2libavPixFmt(dstFormat.format),
			        SWS_BILINEAR, nullptr, nullptr, nullptr);
			if (!m_SwContext)
				throw error("Impossible to set up video converter.");
			m_host->log(Info, format("Converter configured to: %sx%s:%s -> %sx%s:%s",
			        srcFormat.res.width, srcFormat.res.height, (int)srcFormat.format,
			        dstFormat.res.width, dstFormat.res.height, (int)dstFormat.format
			    ).c_str());
			this->srcFormat = srcFormat;
		}

		KHost* const m_host;
		SwsContext *m_SwContext;
		PictureFormat srcFormat, dstFormat;
		OutputDefault* output;
};


Modules::IModule* createObject(KHost* host, void* va) {
	auto fmt = (PictureFormat*)va;
	enforce(host, "VideoConvert: host can't be NULL");
	return Modules::createModule<VideoConvert>(host, *fmt).release();
}

auto const registered = Factory::registerModule("VideoConvert", &createObject);
}
