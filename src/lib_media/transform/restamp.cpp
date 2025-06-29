#include "restamp.hpp"
#include "lib_utils/system_clock.hpp"
#include "lib_utils/log_sink.hpp" // Info
#include "lib_utils/format.hpp"
#include "lib_utils/tools.hpp" // enforce
#include "../common/attributes.hpp" // PresentationTime

#include <cassert>
#include <algorithm> //std::max

namespace Modules {
namespace Transform {

Restamp::Restamp(KHost* host, Mode mode, int64_t offsetIn180k)
	: m_host(host), offset(offsetIn180k), mode(mode) {
	output = addOutput();
}

Restamp::~Restamp() {
}

int64_t Restamp::restamp(int64_t time) {
	switch (mode) {
	case Passthru:
		break;
	case Reset:
		if (!isInitTime) {
			isInitTime = true;
			offset -= time;
		}
		break;
	case ClockSystem:
		time = fractionToClock(g_SystemClock->now());
		if (!isInitTime) {
			isInitTime = true;
			offset -= time;
		}
		break;
	case IgnoreFirstAndReset:
		assert(!isInitTime);
		time = 0;
		mode = Reset;
		break;
	default:
		throw error("Unknown mode");
	}

	if (time + offset < 0) {
		if (time / IClock::Rate < 2) {
			m_host->log(Error, format("reset offset [%s -> %ss (time=%s, offset=%s)]", (double)time / IClock::Rate, (double)(std::max<int64_t>(0, time + offset)) / IClock::Rate, time, offset).c_str());
			offset = 0;
		}
	}

	return std::max<int64_t>(0, time + offset);
}

void Restamp::processOne(Data data) {
	auto const time = data->get<PresentationTime>().time;
	auto const restampedTime = restamp(time);
	m_host->log(((time != 0) && (time + offset < 0)) ? Info : Debug, format("%s -> %ss (time=%s, offset=%s)", (double)time / IClock::Rate, (double)(restampedTime) / IClock::Rate, time, offset).c_str());
	auto dataOut = data->clone();
	dataOut->set(PresentationTime{restampedTime});
	output->post(dataOut);
}

BitrateRestamp::BitrateRestamp(KHost* host,  int64_t bitrateInBps)
	: m_host(host), m_bitrateInBps(bitrateInBps) {
	(void)m_host;
	enforce(bitrateInBps > 0, "Invalid bitrate");
	output = addOutput();
}

void BitrateRestamp::processOne(Data data) {
	auto const timestamp = (m_totalBits * IClock::Rate) / m_bitrateInBps;

	auto dataOut = data->clone();
	dataOut->set(PresentationTime{timestamp});
	output->post(dataOut);
	m_totalBits += 8 * data->data().len;
}
}
}
