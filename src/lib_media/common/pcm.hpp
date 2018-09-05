#pragma once

#include <cstring> // memcpy
#include "lib_modules/core/data.hpp"
#include "lib_modules/core/database.hpp"
#include "lib_utils/log.hpp"
#include "lib_utils/format.hpp"

namespace Modules {

enum AudioSampleFormat {
	S16,
	F32
};

enum AudioLayout {
	Mono,
	Stereo
};

enum AudioStruct {
	Interleaved,
	Planar
};

static const int AUDIO_PCM_PLANES_MAX = 8;
}

namespace {
uint8_t getNumChannelsFromLayout(Modules::AudioLayout layout) {
	switch (layout) {
	case Modules::Mono: return 1;
	case Modules::Stereo: return 2;
	default: throw std::runtime_error("Unknown audio layout");
	}
}
}

namespace Modules {

class PcmFormat {
	public:
		PcmFormat(int sampleRate = 44100, int numChannels = 2,
		    AudioLayout layout = Stereo, AudioSampleFormat sampleFormat = F32, AudioStruct structa = Planar) :
			sampleRate(sampleRate), numChannels(numChannels), layout(layout), sampleFormat(sampleFormat), numPlanes((structa == Planar) ? numChannels : 1) {
		}

		PcmFormat(int sampleRate, AudioLayout layout, AudioSampleFormat sampleFormat, AudioStruct structa) :
			sampleRate(sampleRate), numChannels(getNumChannelsFromLayout(layout)), layout(layout), sampleFormat(sampleFormat), numPlanes((structa == Planar) ? numChannels : 1) {
		}

		bool operator!=(const PcmFormat& other) const {
			return !(*this == other);
		}

		PcmFormat& operator=(const PcmFormat& other) {
			if (this != &other) {
				sampleRate = other.sampleRate;
				numChannels = other.numChannels;
				layout = other.layout;
				sampleFormat = other.sampleFormat;
				numPlanes = other.numPlanes;
			}
			return *this;
		}

		bool operator==(const PcmFormat& other) const {
			if (other.sampleRate != sampleRate) {
				g_Log->log(Debug, format("[Audio] Incompatible configuration: sample rate is %s, expect %s.", other.sampleRate, sampleRate).c_str());
				return false;
			}
			if (other.numChannels != numChannels) {
				g_Log->log(Debug, format("[Audio] Incompatible configuration: channel number is %s, expect %s.", other.numChannels, numChannels).c_str());
				return false;
			}
			if (other.layout != layout) {
				g_Log->log(Debug, format("[Audio] Incompatible configuration: layout is %s, expect %s.", other.layout, layout).c_str());
				return false;
			}
			if (other.sampleFormat != sampleFormat) {
				g_Log->log(Debug, format("[Audio] Incompatible configuration: sample format is %s, expect %s.", other.sampleFormat, sampleFormat).c_str());
				return false;
			}
			if (other.numPlanes != numPlanes) {
				g_Log->log(Debug, format("[Audio] Incompatible configuration: plane number is %s, expect %s.", other.numPlanes, numPlanes).c_str());
				return false;
			}

			return true;
		}

		int getBytesPerSample() const {
			int b = 1;
			switch (sampleFormat) {
			case S16: b *= 2; break;
			case F32: b *= 4; break;
			default: throw std::runtime_error("Unknown audio format");
			}
			b *= getNumChannelsFromLayout(layout);
			return b;
		}

		int sampleRate;
		int numChannels;
		AudioLayout layout;

		AudioSampleFormat sampleFormat;
		int numPlanes;
};

class DataPcm : public DataRaw {
	public:
		DataPcm(size_t size) : DataRaw(0) {
			if (size > 0) {
				throw std::runtime_error("Forbidden operation. Requested size must be 0. Then call setFormat().");
				format.numPlanes = 1;
				setPlane(0, nullptr, size);
			}
		}
		~DataPcm() {
			freePlanes();
		}
		bool isRecyclable() const override {
			return false;
		}

		const PcmFormat& getFormat() const {
			return format;
		}

		void setFormat(PcmFormat const& format) {
			this->format = format;
		}

		Span data() override {
			return Span { planes[0], size() };
		}

		SpanC data() const override {
			return SpanC { planes[0], size() };
		}

		size_t size() const {
			size_t size = 0;
			for (int i = 0; i < format.numPlanes; ++i) {
				size += planeSize[i];
			}
			return size;
		}

		void resize(size_t /*size*/) override {
			throw std::runtime_error("Forbidden operation. You cannot resize PCM data.");
		}

		uint8_t* getPlane(int planeIdx) const {
			if (planeIdx > format.numPlanes)
				throw std::runtime_error("Pcm plane doesn't exist.");
			return planes[planeIdx];
		}

		uint64_t getPlaneSize(int planeIdx) const {
			if (planeIdx > format.numPlanes)
				throw std::runtime_error("Pcm plane doesn't exist.");
			return planeSize[planeIdx];
		}

		uint8_t * const * getPlanes() const {
			return planes;
		}

		void setPlane(int planeIdx, uint8_t *plane, size_t size) {
			if (planeIdx > format.numPlanes)
				throw std::runtime_error("Pcm plane doesn't exist.");
			if ((planes[planeIdx] == nullptr) ||
			    (plane != planes[planeIdx]) ||
			    ((plane == planes[planeIdx]) && (size > planeSize[planeIdx]))) {
				freePlane(planeIdx);
				planes[planeIdx] = new uint8_t[(size_t)size];
			}
			planeSize[planeIdx] = size;
			if (plane && (plane != planes[planeIdx])) {
				memcpy(planes[planeIdx], plane, (size_t)size);
			}
		}

	private:
		void freePlane(int planeIdx) {
			delete [] planes[planeIdx];
			planes[planeIdx] = nullptr;
			planeSize[planeIdx] = 0;
		}
		void freePlanes() {
			for (int i = 0; i < format.numPlanes; ++i) {
				freePlane(i);
			}
		}

		PcmFormat format;
		uint8_t* planes[AUDIO_PCM_PLANES_MAX] {}; //TODO: use std::vector
		size_t planeSize[AUDIO_PCM_PLANES_MAX] {};
};

}

namespace Modules {
template<typename DataType>
class OutputDataDefault;
typedef OutputDataDefault<DataPcm> OutputPcm;
}
