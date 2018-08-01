#include "lib_utils/tools.hpp"
#include "lib_utils/log.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/factory.hpp"
#include "../common/pcm.hpp"
#include "../common/libav.hpp"
#include "../common/pcm.hpp"
#include "lib_ffpp/ffpp.hpp"

using namespace Modules;

namespace {
class AudioConvert : public ModuleS {
	public:
		/*dstFrameSize is the number of output sample - '-1' is same as input*/
		AudioConvert(IModuleHost* host, const PcmFormat &dstFormat, int64_t dstNumSamples = -1);
		AudioConvert(IModuleHost* host, const PcmFormat &srcFormat, const PcmFormat &dstFormat, int64_t dstNumSamples = -1);
		~AudioConvert();
		void process(Data data) override;
		void flush() override;

	private:
		void configure(const PcmFormat &srcFormat);
		void reconfigure(const PcmFormat &srcFormat);

		IModuleHost* m_host;
		PcmFormat srcPcmFormat;
		PcmFormat const dstPcmFormat;
		int64_t dstNumSamples, curDstNumSamples = 0;
		std::shared_ptr<DataPcm> curOut;
		std::unique_ptr<ffpp::SwResampler> m_Swr;
		uint64_t accumulatedTimeInDstSR = 0;
		OutputPcm *output;
		bool autoConfigure;
};

AudioConvert::AudioConvert(IModuleHost* host, const PcmFormat &dstFormat, int64_t dstNumSamples)
	: m_host(host),
	  dstPcmFormat(dstFormat), dstNumSamples(dstNumSamples), m_Swr(nullptr), autoConfigure(true) {
	srcPcmFormat = { 0 };
	auto input = createInput(this);
	input->setMetadata(make_shared<MetadataRawAudio>());
	output = addOutput<OutputPcm>();
}

AudioConvert::AudioConvert(IModuleHost* host, const PcmFormat &srcFormat, const PcmFormat &dstFormat, int64_t dstNumSamples)
	:m_host(host),
	 srcPcmFormat(srcFormat), dstPcmFormat(dstFormat), dstNumSamples(dstNumSamples), m_Swr(new ffpp::SwResampler), autoConfigure(false) {
	configure(srcPcmFormat);
	auto input = createInput(this);
	input->setMetadata(make_shared<MetadataRawAudio>());
	output = addOutput<OutputPcm>();
}

AudioConvert::~AudioConvert() {
}

void AudioConvert::reconfigure(const PcmFormat &srcFormat) {
	flush();
	m_Swr = make_unique<ffpp::SwResampler>();
	configure(srcFormat);
	srcPcmFormat = srcFormat;
}

void AudioConvert::configure(const PcmFormat &srcFormat) {
	AVSampleFormat avSrcFmt, avDstFmt;
	uint64_t avSrcChannelLayout, avDstChannelLayout;
	int avSrcNumChannels, avDstNumChannels, avSrcSampleRate, avDstSampleRate;
	libavAudioCtxConvertLibav(&srcFormat, avSrcSampleRate, avSrcFmt, avSrcNumChannels, avSrcChannelLayout);
	libavAudioCtxConvertLibav(&dstPcmFormat, avDstSampleRate, avDstFmt, avDstNumChannels, avDstChannelLayout);

	m_Swr->setInputSampleFmt(avSrcFmt);
	m_Swr->setInputLayout(avSrcChannelLayout);
	m_Swr->setInputSampleRate(avSrcSampleRate);
	m_Swr->setOutputSampleFmt(avDstFmt);
	m_Swr->setOutputLayout(avDstChannelLayout);
	m_Swr->setOutputSampleRate(avDstSampleRate);
	m_Swr->init();
}

void AudioConvert::flush() {
	if (m_Swr.get())
		process(nullptr);
}

void AudioConvert::process(Data data) {
	uint64_t srcNumSamples = 0;
	uint8_t * const * pSrc = nullptr;
	auto audioData = safe_cast<const DataPcm>(data);
	if (audioData) {
		if (audioData->getFormat() != srcPcmFormat) {
			if (autoConfigure) {
				m_host->log(Info, "Incompatible input audio data. Reconfiguring.");
				reconfigure(audioData->getFormat());
				accumulatedTimeInDstSR = clockToTimescale(data->getMediaTime(), srcPcmFormat.sampleRate);
			} else
				throw error("Incompatible input audio data.");
		}

		srcNumSamples = audioData->size() / audioData->getFormat().getBytesPerSample();
		if (dstNumSamples == -1) {
			dstNumSamples = divUp(srcNumSamples * dstPcmFormat.sampleRate, (uint64_t)srcPcmFormat.sampleRate);
		}
		pSrc = audioData->getPlanes();
	} else {
		auto const delay = m_Swr->getDelay(dstPcmFormat.sampleRate);
		if (delay == 0 && curDstNumSamples == 0) {
			return;
		} else if (delay < dstNumSamples) {
			dstNumSamples = delay; //we are flushing, these are the last samples
		}
		pSrc = nullptr;
		srcNumSamples = 0;
	}

	std::shared_ptr<DataPcm> out;
	if (curDstNumSamples) {
		out = curOut;
	} else {
		auto const dstBufferSize = dstNumSamples * dstPcmFormat.getBytesPerSample();
		out = output->getBuffer(0);
		out->setFormat(dstPcmFormat);
		for (uint8_t i = 0; i < dstPcmFormat.numPlanes; ++i) {
			out->setPlane(i, nullptr, (size_t)dstBufferSize / dstPcmFormat.numPlanes);
		}
	}

	uint8_t* dstPlanes[AUDIO_PCM_PLANES_MAX];
	for (int i=0; i<dstPcmFormat.numPlanes; ++i) {
		dstPlanes[i] = out->getPlanes()[i] + curDstNumSamples * dstPcmFormat.getBytesPerSample() / dstPcmFormat.numPlanes;
	}
	int64_t targetNumSamples;
	if (audioData) {
		targetNumSamples = dstNumSamples - curDstNumSamples;
	} else {
		targetNumSamples = dstNumSamples;
		dstNumSamples += curDstNumSamples;
		const int64_t maxTargetNumSamples = out->getPlaneSize(0) * dstPcmFormat.numPlanes / dstPcmFormat.getBytesPerSample();
		if (targetNumSamples + curDstNumSamples > maxTargetNumSamples) {
			m_host->log(Warning, "Truncating last samples.");
			targetNumSamples = maxTargetNumSamples;
		}
	}
	assert(targetNumSamples >= 0);

	auto const outNumSamples = m_Swr->convert(dstPlanes, (int)targetNumSamples, (const uint8_t**)pSrc, (int)srcNumSamples);

	if (outNumSamples == targetNumSamples) {
		curDstNumSamples = 0;
		curOut = nullptr;
		targetNumSamples = dstNumSamples;

		auto const outPlaneSize = dstNumSamples * dstPcmFormat.getBytesPerSample() / dstPcmFormat.numPlanes;
		for (uint8_t i = 0; i < dstPcmFormat.numPlanes; ++i)
			out->setPlane(i, out->getPlane(i), (size_t)outPlaneSize);

		auto const accumulatedTimeIn180k = timescaleToClock(accumulatedTimeInDstSR, dstPcmFormat.sampleRate);
		out->setMediaTime(accumulatedTimeIn180k);
		accumulatedTimeInDstSR += dstNumSamples;

		output->emit(out);
		out = nullptr;
		if (m_Swr->getDelay(dstPcmFormat.sampleRate) >= dstNumSamples) { //accumulated more than one output buffer: flush.
			process(nullptr);
		}
	} else if (outNumSamples < targetNumSamples) {
		curDstNumSamples += outNumSamples;
		curOut = out;
	} else
		throw error(format("Unexpected case: output %s samples when %s was requested (frame size = %s)", outNumSamples, targetNumSamples, dstNumSamples));
}

IModule* createObject(IModuleHost* host, va_list va) {
	auto src = va_arg(va, PcmFormat*);
	auto dst = va_arg(va, PcmFormat*);
	auto samples = va_arg(va, int);
	enforce(host, "AudioConvert: host can't be NULL");
	enforce(dst, "AudioConvert: dst format can't be NULL");
	enforce(samples == -1 || (samples >= 0 && samples < 1024 * 1024), "AudioConvert: sample count must be valid");
	if(!src)
		return Modules::create<AudioConvert>(host, *dst, samples).release();
	else
		return Modules::create<AudioConvert>(host, *src, *dst, samples).release();
}

auto const registered = registerModule("AudioConvert", &createObject);
}
