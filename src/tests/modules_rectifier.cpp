#include "lib_media/transform/time_rectifier.hpp"
#include "tests.hpp"
#include "lib_media/transform/restamp.hpp"
#include "lib_media/transform/time_rectifier.hpp"
#include "lib_media/utils/recorder.hpp"
#include "lib_media/common/pcm.hpp"

using namespace Tests;
using namespace Modules;

namespace {
template<typename T>
class DataGenerator : public Module, public virtual IOutputCap {
public:
	DataGenerator() {
		output = addOutput<OutputPcm>();
		output->setMetadata(shptr(new T));
	}
	void process() {}
	void push(int64_t mediaTime, int64_t clockTime) {
		auto data = output->getBuffer(0);
		data->setMediaTime(mediaTime);
		data->setClockTime(clockTime);
		output->emit(data);
	}

private:
	OutputPcm *output;
};

template<typename Metadata>
void testRectifier(std::vector<std::pair<int64_t, int64_t>> inTimes, std::vector<int64_t> outTimes) {
	auto rectifier = create<TimeRectifier>(Clock::Rate/2, uptr(new Scheduler(shptr(new Clock(0.0)))));
	auto generator = create<DataGenerator<Metadata>>();
	ConnectModules(generator.get(), 0, rectifier.get(), 0);
	auto recorder = create<Utils::Recorder>();
	ConnectModules(rectifier.get(), 0, recorder.get(), 0);
	for (size_t i = 0; i < inTimes.size(); ++i) {
		generator->push(inTimes[i].first, inTimes[i].second);
	}
	rectifier->flush();
	recorder->process(nullptr);
	Data data;
	size_t i = 0;
	while ((data = recorder->pop())) {
		ASSERT(data->getMediaTime() == outTimes[i++]);
	}
	ASSERT(outTimes.size() == i);
}
}

/*Romain: there are four times:
1-2) Inputs: realtime   + timestamp
3)   Clock:  media time +
4)   Output: media time
*/
//id:            1 2 3 4 5 => 1 2 3 4 5
//half:          1 2 3 4 5 => 1 x 3 x 5
//double:        1 2 3 4 5 => 1 1 2 2 3 3 4 4 5 5
//any framerate: XXX
//missing frame: 1 2 4  5  => 1 2 2 4 5
//ts backward  : 1 2 10 11 => 1 2 3 4
//
//Romain: also think about when output lags - maybe limit based on raw vs pkt
//Pipeline is not single responsability. And dispatch is not perfect/ideal. It also assumes the first timestamp is zero.
//
//Romain: dont forget to remove the restamper (tests are below) + sparseStreamsHeartbeat in demux

unittest("rectifier: timing checks with a single pin") {
	testRectifier<MetadataRawVideo>({ { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 4 } }, { 1, 2, 3, 4, 5 });
}

unittest("rectifier: timing checks with multiple pins") {
	//TODO
}

unittest("rectifier: fail when no video") {
	bool thrown = false;
	try {
		testRectifier<MetadataRawAudio>({ { 0, 0 } }, { 0 });
	} catch (std::exception const& e) {
		std::cerr << "Expected error: " << e.what() << std::endl;
		thrown = true;
	}
	ASSERT(thrown);
}

unittest("restamp: passthru with offsets") {
	auto const time = 10001LL;
	auto data = std::make_shared<DataRaw>(0);

	data->setMediaTime(time);
	auto restamp = create<Transform::Restamp>(Transform::Restamp::Reset);
	restamp->process(data);
	ASSERT_EQUALS(0, data->getMediaTime());

	data->setMediaTime(time);
	restamp = create<Transform::Restamp>(Transform::Restamp::Reset, 0);
	restamp->process(data);
	ASSERT_EQUALS(0, data->getMediaTime());

	data->setMediaTime(time);
	restamp = create<Transform::Restamp>(Transform::Restamp::Reset, time);
	restamp->process(data);
	ASSERT_EQUALS(time, data->getMediaTime());
}

unittest("restamp: reset with offsets") {
	uint64_t time = 10001;
	int64_t offset = -100;
	auto data = std::make_shared<DataRaw>(0);

	data->setMediaTime(time);
	auto restamp = create<Transform::Restamp>(Transform::Restamp::Passthru);
	restamp->process(data);
	ASSERT_EQUALS(time, data->getMediaTime());

	data->setMediaTime(time);
	restamp = create<Transform::Restamp>(Transform::Restamp::Passthru, 0);
	restamp->process(data);
	ASSERT_EQUALS(time, data->getMediaTime());

	data->setMediaTime(time);
	restamp = create<Transform::Restamp>(Transform::Restamp::Passthru, offset);
	restamp->process(data);
	ASSERT_EQUALS(time + offset, data->getMediaTime());

	data->setMediaTime(time);
	restamp = create<Transform::Restamp>(Transform::Restamp::Passthru, time);
	restamp->process(data);
	ASSERT_EQUALS(time + time, data->getMediaTime());
}
