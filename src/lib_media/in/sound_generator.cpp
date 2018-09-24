#include "lib_utils/tools.hpp"
#include "sound_generator.hpp"
#include <cmath>

#ifndef M_PI //Cygwin does not have maths.h extensions
#define M_PI 3.14159265358979323846
#endif

namespace Modules {
namespace In {

auto const SINE_FREQ = 880.0;

SoundGenerator::SoundGenerator(IModuleHost* host)
	:  m_host(host), m_numSamples(20000) {
	(void)m_host;
	pcmFormat.sampleFormat = S16;
	pcmFormat.sampleRate = 44100;
	pcmFormat.numPlanes = 1;
	output = addOutput<OutputPcm>();
}

bool SoundGenerator::work() {
	auto const bytesPerSample = pcmFormat.getBytesPerSample();
	auto const sampleDurationInMs = 40;
	auto const bufferSize = bytesPerSample * sampleDurationInMs * pcmFormat.sampleRate / 1000;

	auto out = output->getBuffer(0);
	out->setFormat(pcmFormat);
	for (uint8_t i = 0; i < pcmFormat.numPlanes; ++i)
		out->setPlane(i, nullptr, bufferSize / pcmFormat.numPlanes);
	out->setMediaTime(m_numSamples, pcmFormat.sampleRate);

	// generate sound
	auto const p = out->data().ptr;
	assert(pcmFormat.numPlanes == 1);
	for(int i=0; i < (int)out->size()/bytesPerSample; ++i) {
		auto const fVal = nextSample();
		auto const val = int(fVal * 32767.0f);

		// left
		p[i*bytesPerSample+0] = (val >> 0) & 0xFF;
		p[i*bytesPerSample+1] = (val >> 8) & 0xFF;

		// right
		p[i*bytesPerSample+2] = (val >> 0) & 0xFF;
		p[i*bytesPerSample+3] = (val >> 8) & 0xFF;
	}

	output->emit(out);
	return false;
}

double SoundGenerator::nextSample() {
	auto const BEEP_PERIOD = pcmFormat.sampleRate;
	auto const beepPhase = int(m_numSamples % BEEP_PERIOD);
	auto const phase = m_numSamples * 2.0 * SINE_FREQ * M_PI / pcmFormat.sampleRate;
	auto const fVal = beepPhase < BEEP_PERIOD/8 ? sin(phase) : 0;
	m_numSamples++;
	return fVal;
}

}
}
