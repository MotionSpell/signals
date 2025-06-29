#include "logo_overlay.hpp"

#include "lib_modules/modules.hpp"
#include "lib_media/common/picture.hpp"
#include "lib_utils/tools.hpp" // safe_cast
#include "lib_modules/utils/loader.hpp"
#include <algorithm> //std::min
#include <cstring> // memcpy

using namespace Modules;

namespace {

static uint8_t blend(uint8_t a, uint8_t b, int alpha) {
	return (alpha  * b + (256 - alpha) * a) >> 8;
}

static int getSubsampling(const DataPicture* pic, int p) {
	if(pic->getStride(0) == pic->getStride(p))
		return 0;
	else if(pic->getStride(0)/2 == pic->getStride(p))
		return 1;

	throw std::runtime_error("Unhandled subsampling");
}

static void compose(DataPicture* pic,
    int x, int y,
    const DataPicture* overlay,
    const DataPicture* mask) {
	auto const picRes = pic->getFormat().res;
	auto const logoRes = overlay->getFormat().res;
	auto const width = std::min<int>(logoRes.width, picRes.width - x);

	for (int p = 0; p < pic->getNumPlanes(); ++p) {
		auto const srcStride = overlay->getStride(p);
		auto const dstStride = (int)pic->getStride(p);

		auto const subsampling = getSubsampling(pic, p);
		auto const multiplicator = dstStride > picRes.width ? dstStride/picRes.width : 1;
		auto const xAdj = (x * multiplicator) >> subsampling;
		auto const yAdj = (y >> subsampling);
		auto const logoResHDiv = logoRes.height >> subsampling;
		auto const picResHDiv = picRes.height >> subsampling;
		auto const blitHeight = std::min(logoResHDiv, picResHDiv - yAdj);
		auto const blitWidth = (width * multiplicator) >> subsampling;

		auto srcPels = overlay->getPlane(p);
		auto dstPels = pic->getPlane(p) + yAdj * dstStride + xAdj;

		if (mask) {
			auto const rgbaStride = ((1<<subsampling) * 4) / multiplicator;
			auto const maskPitch = ((1<<subsampling) * mask->getStride(0)) / multiplicator;
			auto const logoAlphaP0 = mask->getPlane(0);
			for (int h = 0; h < blitHeight; ++h) {
				auto const maskLine = logoAlphaP0 + h * maskPitch;
				for (int w = 0; w < blitWidth; ++w) {
					const auto alpha = maskLine[w * rgbaStride + 3] + 1;
					dstPels[w] = blend(dstPels[w], srcPels[w], alpha);
				}
				srcPels += srcStride;
				dstPels += dstStride;
			}
		} else {
			for (int h = 0; h < blitHeight; ++h) {
				memcpy(dstPels, srcPels, blitWidth);
				srcPels += srcStride;
				dstPels += dstStride;
			}
		}
	}
}

class LogoOverlay : public Module {
	public:
		LogoOverlay(KHost*, LogoOverlayConfig* cfg)
			: m_cfg(*cfg) {
			m_mainInput = addInput();
			m_overlayInput = addInput();
			m_output = addOutput();
		}
		void process() {
			Data data;
			if (m_mainInput->tryPop(data)) {
				processVideo(safe_cast<const DataPicture>(data));
			} else if (m_overlayInput->tryPop(data)) {
				processOverlay(safe_cast<const DataPicture>(data));
			} else
				throw error("Awaken on unknown input.");
		}

	private:
		const LogoOverlayConfig m_cfg;

		std::shared_ptr<const DataPicture> m_overlay;
		std::shared_ptr<const DataPicture> m_convertedOverlay;
		std::shared_ptr<const DataPicture> m_overlayMask;

		KOutput* m_output;
		KInput* m_mainInput;
		KInput* m_overlayInput;

		void processOverlay(std::shared_ptr<const DataPicture> overlay) {
			m_overlay = overlay;
		}
		void processVideo(std::shared_ptr<const DataPicture> pic) {
			if (!m_convertedOverlay && m_overlay) {
				createOverlay(pic->getFormat().format);
				m_overlay = nullptr;
			}

			if (m_convertedOverlay) {
				auto nonConstPic = const_cast<DataPicture*>(pic.get());
				compose(nonConstPic, m_cfg.x, m_cfg.y, m_convertedOverlay.get(), m_overlayMask.get());
			}

			m_output->post(pic);
		}

		void createOverlay(PixelFormat pixelFormat) {
			struct OutStub : ModuleS {
				std::function<void(Data)> fn;
				void processOne(Data data) override {
					fn(data);
				}
			};
			auto res = m_overlay->getFormat().res;
			if(m_cfg.dim.width)
				res.width = m_cfg.dim.width;
			if(m_cfg.dim.height)
				res.height = m_cfg.dim.height;
			{
				const PictureFormat pfo(res, pixelFormat);
				OutStub stub;
				stub.fn = [this](Data data) {
					this->m_convertedOverlay = safe_cast<const DataPicture>(data);
				};
				auto converter = loadModule("VideoConvert", &NullHost, &pfo);
				converter->getOutput(0)->connect(stub.getInput(0));
				converter->getInput(0)->push(m_overlay);
				converter->process();
				converter->flush();
			}
			if (m_overlay->getFormat().hasTransparency()) {
				OutStub stub;
				stub.fn = [this](Data data) {
					this->m_overlayMask = safe_cast<const DataPicture>(data);
				};
				const PictureFormat pfo(res, m_overlay->getFormat().format);
				auto converter = loadModule("VideoConvert", &NullHost, &pfo);
				converter->getOutput(0)->connect(stub.getInput(0));
				converter->getInput(0)->push(m_overlay);
				converter->process();
				converter->flush();
			}
		}
};

IModule* createObject(KHost* host, void* va) {
	auto config = (LogoOverlayConfig*)va;
	enforce(host, "LogoOverlay: host can't be NULL");
	enforce(config, "LogoOverlay: config can't be NULL");
	return createModule<LogoOverlay>(host, config).release();
}

auto const registered = Factory::registerModule("LogoOverlay", &createObject);

}

