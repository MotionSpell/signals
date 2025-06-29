#include "tests/tests.hpp"
#include "lib_utils/i_scheduler.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_modules/core/connection.hpp" // ConnectModules
#include "lib_media/transform/rectifier.hpp"
#include "lib_media/common/pcm.hpp"
#include "lib_media/common/picture.hpp"
#include "lib_media/common/subtitle.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/attributes.hpp"

#include <algorithm> // sort
#include <cassert>

using namespace std;
using namespace Tests;
using namespace Modules;

struct Event {
	int index;
	int64_t clockTime;
	int64_t mediaTime;
	bool operator<(Event other) const {
		if(clockTime != other.clockTime)
			return clockTime < other.clockTime;
		if(index != other.index)
			return index < other.index;
		else
			return mediaTime < other.mediaTime;
	}
};

// allows ASSERT_EQUALS on Event
static ostream& operator<<(ostream& o, Event t) {
	o << "{ #" << t.index;
	o << " clk=" << t.clockTime;
	o << " mt=" << t.mediaTime;
	o << "}";
	return o;
}

static bool operator==(Event a, Event b) {
	return a.index == b.index && a.clockTime == b.clockTime && a.mediaTime == b.mediaTime;
}

class ClockMock : public IClock, public IScheduler {
	public:
		void setTime(Fraction t) {
			assert(t >= m_time);

			// beware: running tasks might modify m_tasks by pushing new tasks
			while(!m_tasks.empty() && m_tasks[0].time <= t) {
				auto tsk = std::move(m_tasks[0]);
				m_tasks.erase(m_tasks.begin());

				m_time = tsk.time;
				tsk.func(m_time);
			}

			m_time = t;
		}

		Fraction now() const override {
			return m_time;
		}

		Id scheduleAt(TaskFunc &&task, Fraction time) override {
			if(time < m_time)
				throw runtime_error("The rectifier is scheduling events in the past");

			m_tasks.push_back({time, task});
			sort(m_tasks.begin(), m_tasks.end());
			return -1;
		}

		Id scheduleIn(TaskFunc &&, Fraction) override {
			assert(0);
			return -1;
		}

		void cancel(Id id) override {
			(void)id;
			assert(!m_tasks.empty());
			m_tasks.pop_back();
		}

	private:
		Fraction m_time = 0;

		struct Task {
			Fraction time;
			TaskFunc func;
			bool operator<(Task const& other) const {
				return time < other.time;
			}
		};

		vector<Task> m_tasks; // keep this sorted
};

template<typename METADATA, int SAMPLERATE>
struct DataGenerator : ModuleS, virtual IOutputCap {
	DataGenerator(Fraction fps) : fps(fps) {
		output = addOutput();
		output->setMetadata(make_shared<METADATA>());
	}

	void processOne(Data dataIn) override {
		if (output->getMetadata()->isAudio()) {
			auto const samplesN = (int)(fps.inverse() * pktCounter * SAMPLERATE);
			pktCounter++;
			auto const samplesNplus1 = (int)(fps.inverse() * pktCounter * SAMPLERATE);

			PcmFormat fmt(SAMPLERATE, 1, Mono);
			auto dataPcm = output->allocData<DataPcm>((samplesNplus1 - samplesN), fmt);

			// fill with a counter
			ASSERT(dataPcm->format.getBytesPerSample() % sizeof(uint32_t) == 0);
			auto const dataSize = dataPcm->getSampleCount() * dataPcm->format.getBytesPerSample();
			for (int i = 0; i < dataSize / (int)sizeof(uint32_t); ++i)
				*((uint32_t*)dataPcm->getPlane(0) + i) = audioSampleCounter++;

			dataPcm->set(PresentationTime{ dataIn->get<PresentationTime>().time });
			output->post(dataPcm);
		} else if (output->getMetadata()->isSubtitle()) {
			auto dataSubtitle = output->allocData<DataSubtitle>(0);
			dataSubtitle->page.showTimestamp = dataIn->get<PresentationTime>().time;
			dataSubtitle->page.hideTimestamp = dataSubtitle->page.showTimestamp + fractionToClock(fps.inverse());
			dataSubtitle->set(PresentationTime{ dataIn->get<PresentationTime>().time });
			output->post(dataSubtitle);
		} else {
			ASSERT(output->getMetadata()->isVideo());
			auto data = output->allocData<DataPicture>(0, PixelFormat::I420);
			data->set(PresentationTime{ dataIn->get<PresentationTime>().time });
			output->post(data);
		}
	}
	OutputDefault *output;
	Fraction fps;
	uint32_t audioSampleCounter = 0;
	int64_t pktCounter = 0;
};

typedef DataGenerator<MetadataRawVideo, 0> VideoGenerator;
typedef DataGenerator<MetadataRawAudio, 48000> AudioGenerator48000;
typedef DataGenerator<MetadataRawAudio, 44100> AudioGenerator44100;
typedef DataGenerator<MetadataRawSubtitle, 0> SubtitleGenerator;

struct Fixture {
	shared_ptr<ClockMock> clock = make_shared<ClockMock>();
	vector<unique_ptr<ModuleS>> generators;
	shared_ptr<IModule> rectifier;
	vector<Event> actualTimes;
	bool initialPadding = true;

	Fixture(Fraction fps) {
		auto cfg = RectifierConfig { clock, clock, fps };
		rectifier = loadModule("Rectifier", &NullHost, &cfg);
	}

	~Fixture() {
		for (int i=0; i<rectifier->getNumOutputs(); ++i) {
			auto meta = rectifier->getOutput(i)->getMetadata();
			if (meta && meta->isAudio()) {
				ASSERT_EQUALS(false, initialPadding);
				break;
			}
		}
	}

	void setTime(int64_t time) {
		clock->setTime(Fraction(time, IClock::Rate));
	}

	void addStream(int i, unique_ptr<ModuleS>&& generator) {
		generators.push_back(std::move(generator));

		ConnectModules(generators[i].get(), 0, rectifier.get(), i);
		ConnectOutput(rectifier->getOutput(i), [i, this](Data data) {
			this->onOutputSample(i, data);
		});
	}

	void push(int index, int64_t mediaTime) {
		auto data = make_shared<DataRaw>(1);
		data->set(PresentationTime{mediaTime});
		generators[index]->processOne(data);
	}

	void onOutputSample(int i, Data data) {
		auto dataPcm = dynamic_pointer_cast<const DataPcm>(data);
		if (dataPcm) {
			uint32_t rem = *((uint32_t*)dataPcm->getPlane(0));
			for (int i = 0; i < dataPcm->getSampleCount() * dataPcm->format.getBytesPerSample() / (int)sizeof(uint32_t); ++i) {
				const int64_t val = *((uint32_t*)dataPcm->getPlane(0) + i);
				const int64_t expectedAudioVal = i + rem;
				if (!initialPadding || val != 0) { // initial unmatched samples contain zeroes instead of the counter: exclude them
					ASSERT_EQUALS(expectedAudioVal, val);
					initialPadding = false;
				}
			}
		}

		actualTimes.push_back(Event{i, fractionToClock(clock->now()), data->get<PresentationTime>().time});
	}
};

vector<Event> runRectifier(
    Fraction fps,
    vector<unique_ptr<ModuleS>> &gen,
    vector<Event> events) {

	Fixture fix(fps);

	for (int i = 0; i < (int)gen.size(); ++i)
		fix.addStream(i, std::move(gen[i]));

	const int first = 0, last = (int)events.size();
	for (int i = first; i < last; ++i) {
		auto& event = events[i];

		fix.push(event.index, event.mediaTime);

		if (event.clockTime > 0) {
			bool update = true;
			if (i+1 != last) {
				ASSERT(events[i+1].clockTime >= event.clockTime); //assume increasing order
				if (events[i+1].clockTime == event.clockTime)
					update = false;
			}
			if (update)
				fix.setTime(event.clockTime);
		}
	}

	fix.clock->setTime(fix.clock->now());

	return fix.actualTimes;
}

unittest("rectifier: simple offset") {
	// use '1000' as a human-readable frame period
	auto const fps = Fraction(IClock::Rate, 1000);
	Fixture fix(fps);
	fix.addStream(0, createModuleWithSize<VideoGenerator>(100, fps));

	fix.setTime(8801000);
	fix.push(0, 301007);
	fix.setTime(8802000);
	fix.push(0, 301007);
	fix.setTime(8803000);
	fix.push(0, 302007);
	fix.setTime(8804000);
	fix.push(0, 303007);
	fix.setTime(8805000);
	fix.push(0, 304007);
	fix.setTime(8806000);

	auto const expectedTimes = vector<Event>({
		Event{0, 8801000, 0},
		Event{0, 8802000, 1000},
		Event{0, 8803000, 2000},
		Event{0, 8804000, 3000},
		Event{0, 8805000, 4000},
		Event{0, 8806000, 5000},
	});

	ASSERT_EQUALS(expectedTimes, fix.actualTimes);
}

unittest("rectifier: missing frame") {
	// use '100' as a human-readable frame period
	auto const fps = Fraction(IClock::Rate, 100);
	Fixture fix(fps);
	fix.addStream(0, createModuleWithSize<VideoGenerator>(100, fps));

	fix.setTime(0);
	fix.push(0, 30107);
	fix.setTime(100);
	// missing fix.push(0, 30207}
	fix.push(0, 30307);
	fix.setTime(400);
	fix.push(0, 30407);
	fix.setTime(500);
	fix.push(0, 30507);
	fix.setTime(600);

	auto const expectedTimes = vector<Event>({
		Event{0, 0, 0},
		Event{0, 100, 100},
		Event{0, 200, 200},
		Event{0, 300, 300},
		Event{0, 400, 400},
		Event{0, 500, 500},
		Event{0, 600, 600},
	});

	ASSERT_EQUALS(expectedTimes, fix.actualTimes);
}

unittest("rectifier: loss of input") {
	auto const fps = Fraction(IClock::Rate, 100);
	Fixture fix(fps);
	fix.addStream(0, createModuleWithSize<VideoGenerator>(100, fps));

	// send one frame, and then nothing, but keep the clock ticking
	fix.setTime(1000);
	fix.push(0, 0);
	fix.setTime(1000);
	fix.setTime(1100);
	fix.setTime(1200);
	fix.setTime(1300);
	fix.setTime(1400);
	fix.setTime(1500);

	auto const expectedTimes = vector<Event>({
		Event{0, 1000,   0},
		Event{0, 1100, 100},
		Event{0, 1200, 200},
		Event{0, 1300, 300},
		Event{0, 1400, 400},
		Event{0, 1500, 500},
	});

	ASSERT_EQUALS(expectedTimes, fix.actualTimes);
}

unittest("rectifier: noisy timestamps") {
	// use '100' as a human-readable frame period
	auto const fps = Fraction(IClock::Rate, 100);
	Fixture fix(fps);
	fix.addStream(0, createModuleWithSize<VideoGenerator>(100, fps));

	fix.setTime(  0);
	fix.push(0, 1000 + 2);
	fix.setTime(100);
	fix.setTime(100 + 5);
	fix.push(0, 1100 - 3);
	fix.setTime(200 - 1);
	fix.push(0, 1200 - 1);
	fix.setTime(200);
	fix.setTime(300);
	fix.setTime(300 + 2);
	fix.push(0, 1300 + 7);
	fix.setTime(400 - 2);
	fix.setTime(400);
	fix.push(0, 1400 - 9);
	fix.setTime(500);
	fix.setTime(500 + 1);
	fix.push(0, 1500 + 15);
	fix.setTime(600);

	auto const expectedTimes = vector<Event>({
		Event{0,   0,   0},
		Event{0, 100, 100},
		Event{0, 200, 200},
		Event{0, 300, 300},
		Event{0, 400, 400},
		Event{0, 500, 500},
		Event{0, 600, 600},
	});

	ASSERT_EQUALS(expectedTimes, fix.actualTimes);
}

static void fixupTimes(vector<Event>& expectedTimes, vector<Event>& actualTimes) {
	// cut the surplus 'actual' times
	if(actualTimes.size() > expectedTimes.size())
		actualTimes.resize(expectedTimes.size());
	else if(expectedTimes.size() - actualTimes.size() <= 3)
		// workaround: don't compare beyond 'actual' times
		expectedTimes.resize(actualTimes.size());
}

template<typename GeneratorType>
void testRectifierSinglePort(Fraction fps, vector<Event> inTimes, vector<Event> expectedTimes) {
	vector<unique_ptr<ModuleS>> generators;
	generators.push_back(createModuleWithSize<GeneratorType>(inTimes.size(), fps));

	auto actualTimes = runRectifier(fps, generators, inTimes);

	fixupTimes(expectedTimes, actualTimes);

	ASSERT_EQUALS(expectedTimes, actualTimes);
}

struct TimePair {
	int64_t mediaTime;
	int64_t clockTime;
};

static auto const numEvents = 15;

TimePair generateTimePair(uint64_t step, Fraction fps, int64_t clockTimeOffset, int64_t mediaTimeOffset) {
	auto const mediaTime = (int64_t)(IClock::Rate * (step + mediaTimeOffset) * fps.den) / fps.num;
	auto const clockTime = (int64_t)(IClock::Rate * (step + clockTimeOffset) * fps.den) / fps.num;
	return TimePair{ mediaTime, clockTime };
};

TimePair generateTimePairDefault(uint64_t step, Fraction fps) {
	auto gen = bind(generateTimePair, placeholders::_1, placeholders::_2, 0, 0);
	return gen(step, fps);
}

vector<Event> generateEvents(Fraction fps, int index = 0, function<TimePair(uint64_t, Fraction)> generateValue = generateTimePairDefault) {
	auto const numItems = (int)(Fraction(numEvents) * fps / Fraction(25, 1));
	vector<Event> times;
	for (int i = 0; i < numItems; ++i) {
		auto tp = generateValue(i, fps);
		times.push_back({index, tp.clockTime, tp.mediaTime});
	}
	return times;
}

vector<Event> generateInterleavedEvents(Fraction frameDurAudio, int clockOffsetAudio, int mediaOffsetAudio,
    Fraction frameDurVideo, int clockOffsetVideo, int mediaOffsetVideo) {
	auto times = generateEvents(frameDurAudio.inverse(), 0,
	    bind(generateTimePair, placeholders::_1, placeholders::_2, clockOffsetAudio, mediaOffsetAudio)); //audio
	auto times2 = generateEvents(frameDurVideo.inverse(), 1,
	    bind(generateTimePair, placeholders::_1, placeholders::_2, clockOffsetVideo, mediaOffsetVideo)); //video
	times.insert(times.end(), times2.begin(), times2.end());
	sort(times.begin(), times.end());
	return times;
};

void testFPSFactor(Fraction fps, Fraction factor) {
	auto const outTimes = generateEvents(fps * factor);
	auto const inTimes = generateEvents(fps);
	testRectifierSinglePort<VideoGenerator>(fps * factor, inTimes, outTimes);
}

unittest("rectifier: FPS factor (single port) 9 fps, x1") {
	testFPSFactor(9, 1);
}

unittest("rectifier: FPS factor (single port) 9 fps, x2") {
	testFPSFactor(9, 2);
}

unittest("rectifier: FPS factor (single port) 25 fps, x1/2") {
	testFPSFactor(25, Fraction(1, 2));
}

unittest("rectifier: FPS factor (single port) 29.97 fps, x1") {
	testFPSFactor(Fraction(30000, 1001), 1);
}

unittest("rectifier: FPS factor (single port) 29.97 fps, x2") {
	testFPSFactor(Fraction(30000, 1001), 2);
}

unittest("rectifier: FPS factor (single port) 29.97 fps, x1/2") {
	testFPSFactor(Fraction(30000, 1001), Fraction(1, 2));
}

unittest("rectifier: initial offset (single port)") {
	auto const fps = Fraction(25, 1);

	auto const outTimes = generateEvents(fps, 0, bind(generateTimePair, placeholders::_1, placeholders::_2, 0, 0));
	auto const inTimes1 = generateEvents(fps, 0, bind(generateTimePair, placeholders::_1, placeholders::_2, 0, 5));
	testRectifierSinglePort<VideoGenerator>(fps, inTimes1, outTimes);

	auto const inTimes2 = generateEvents(fps, 0, bind(generateTimePair, placeholders::_1, placeholders::_2, 0, -5));
	testRectifierSinglePort<VideoGenerator>(fps, inTimes2, outTimes);
}

unittest("rectifier: deal with missing frames (single port)") {
	const uint64_t freq = 2;
	auto const fps = Fraction(25, 1);

	auto const inGenVal = [=](uint64_t step, Fraction fps) {
		static uint64_t i = 0;
		if (step && !(step % freq)) i++;
		auto const t = int64_t((IClock::Rate * (step+i) * fps.den) / fps.num);
		return TimePair{t, t};
	};
	auto const inTimes = generateEvents(fps, 0, inGenVal);
	auto const outTimes = generateEvents(fps);

	testRectifierSinglePort<VideoGenerator>(fps, inTimes, outTimes);
}

unittest("rectifier: deal with backward discontinuity (single port)") {
	auto const fps = Fraction(25, 1);
	auto inTimes1 = generateEvents(fps);
	auto inTimes2 = generateEvents(fps, 0, bind(generateTimePair, placeholders::_1, placeholders::_2, inTimes1.size(), 0));
	auto outTimes1 = generateEvents(fps);
	auto outTimes2 = generateEvents(fps, 0, bind(generateTimePair, placeholders::_1, placeholders::_2, inTimes1.size(), inTimes1.size()));
	inTimes1.insert(inTimes1.end(), inTimes2.begin(), inTimes2.end());
	outTimes1.insert(outTimes1.end(), outTimes2.begin(), outTimes2.end());
	testRectifierSinglePort<VideoGenerator>(fps, inTimes1, outTimes1);
}

unittest("rectifier: multiple media types simple") {
	// real timescale is required here, because we're dealing with audio
	auto const fps = Fraction(25, 1);
	Fixture fix(fps);
	fix.addStream(0, createModuleWithSize<VideoGenerator>(100, fps));
	fix.addStream(1, createModuleWithSize<AudioGenerator48000>(100, fps));

	// 7200 = (1920 * IClock::Rate) / 48kHz;

	fix.setTime( 1000 + 7200 * 0);
	fix.push(0, 7200 * 0); fix.push(1, 7200 * 0);
	fix.push(0, 7200 * 1); fix.push(1, 7200 * 1);
	fix.setTime( 1000 + 7200 * 0);
	fix.push(0, 7200 * 2); fix.push(1, 7200 * 2);
	fix.setTime( 1000 + 7200 * 1);
	fix.push(0, 7200 * 3); fix.push(1, 7200 * 3);
	fix.setTime( 1000 + 7200 * 2);
	fix.setTime( 1000 + 7200 * 3);

	auto const expectedTimes = vector<Event>({
		Event{0, 1000 + 7200 * 0, 7200 * 0}, Event{1, 1000 + 7200 * 0, 7200 * 0},
		Event{0, 1000 + 7200 * 1, 7200 * 1}, Event{1, 1000 + 7200 * 1, 7200 * 1},
		Event{0, 1000 + 7200 * 2, 7200 * 2}, Event{1, 1000 + 7200 * 2, 7200 * 2},
		Event{0, 1000 + 7200 * 3, 7200 * 3}, Event{1, 1000 + 7200 * 3, 7200 * 3},
	});

	ASSERT_EQUALS(expectedTimes, fix.actualTimes);
}

unittest("rectifier: fail when no video") {
	auto const fps = Fraction(25, 1);
	vector<unique_ptr<ModuleS>> generators;
	generators.push_back(createModuleWithSize<AudioGenerator48000>(1, fps));
	ASSERT_THROWN(runRectifier(fps, generators, {Event()}));
}

unittest("rectifier: two streams, only the first receives data") {
	const auto videoRate = Fraction(25, 1);
	auto times = generateEvents(videoRate); //generate video events only
	vector<unique_ptr<ModuleS>> generators;
	generators.push_back(createModuleWithSize<VideoGenerator>(100, videoRate));
	generators.push_back(createModuleWithSize<AudioGenerator48000>(100, videoRate /*same as audio is ok*/));

	auto actualTimes = runRectifier(videoRate, generators, times);

	ASSERT_EQUALS(times, actualTimes);
}

unittest("rectifier: master stream arrives in advance of slave streams (but within the analyze window)") {
	const auto videoRate = Fraction(25, 1);
	const auto offset = 3;
	auto times = generateInterleavedEvents(videoRate.inverse(), 0, 0, videoRate.inverse(), offset, 0);
	vector<unique_ptr<ModuleS>> generators;
	generators.push_back(createModuleWithSize<VideoGenerator>(100, videoRate));
	generators.push_back(createModuleWithSize<AudioGenerator48000>(100, videoRate /*same as audio is ok*/));

	auto actualTimes = runRectifier(videoRate, generators, times);

	auto expected = generateInterleavedEvents(videoRate.inverse(), 0, 0, videoRate.inverse(), offset, 0);
	for (auto& e : expected) if (e.index == 1) e.mediaTime = e.clockTime;
	expected.resize(expected.size() - offset);
	fixupTimes(expected, actualTimes);

	ASSERT_EQUALS(expected, actualTimes);
}

unittest("rectifier: slave stream arrives in advance of master streams") {
	const auto videoRate = Fraction(25, 1);
	const auto offset = 3;
	auto times = generateInterleavedEvents(videoRate.inverse(), 0, 0, videoRate.inverse(), offset, 0);
	vector<unique_ptr<ModuleS>> generators;
	generators.push_back(createModuleWithSize<AudioGenerator48000>(100, videoRate));
	generators.push_back(createModuleWithSize<VideoGenerator>(100, videoRate /*same as audio is ok*/));

	auto actualTimes = runRectifier(videoRate, generators, times);
	auto expected = generateInterleavedEvents(videoRate.inverse(), offset, 0, videoRate.inverse(), offset, 0);
	for (auto& e : expected) e.index = 1 - e.index;
	ASSERT_EQUALS(expected, actualTimes);
}

unittest("rectifier: subtitles (sparse)") {
	const Fraction fps(25, 1);
	Fixture fix(fps);
	fix.addStream(0, createModuleWithSize<VideoGenerator>(100, fps));
	fix.addStream(1, createModuleWithSize<SubtitleGenerator>(100, fps));

	auto const offset = 1000;
	auto const delta = (int)(fps.inverse() * IClock::Rate);
	fix.setTime(offset + delta * 0);
	fix.push(0, delta * 0); /*no subtitle*/
	fix.push(0, delta * 1); fix.push(1, delta * 1);
	fix.setTime(offset + delta * 1);
	fix.push(0, delta * 3); fix.push(1, delta * 3);
	fix.setTime(offset + delta * 2);
	fix.setTime(offset + delta * 3);

	auto const expectedTimes = vector<Event>({
		Event{0, offset + delta * 0, delta * 0}, Event{1, offset + delta * 0, delta * 0} /*wrong subtitle*/, Event{1, offset + delta * 0, delta * 0} /*heartbeat*/,
		Event{0, offset + delta * 1, delta * 1}, Event{1, offset + delta * 1, delta * 1} /*heartbeat*/,
		Event{0, offset + delta * 2, delta * 2}, Event{1, offset + delta * 2, delta * 2} /*wrong subtitle*/, Event{1, offset + delta * 2, delta * 2} /*heartbeat*/,
		Event{0, offset + delta * 3, delta * 3}, Event{1, offset + delta * 3, delta * 3} /*heartbeat*/,
	});

	ASSERT_EQUALS(expectedTimes, fix.actualTimes);
}

unittest("rectifier: audio 44100 interleave") {
	const auto videoRate = Fraction(25, 1);
	const auto audioFrameDur = Fraction(1024, 44100);
	auto times = generateInterleavedEvents(audioFrameDur, 0, 0, videoRate.inverse(), 0, 0);
	vector<unique_ptr<ModuleS>> generators;
	generators.push_back(createModuleWithSize<AudioGenerator44100>(100, audioFrameDur.inverse()));
	generators.push_back(createModuleWithSize<VideoGenerator>(100, videoRate));
	auto actualTimes = runRectifier(videoRate, generators, times);
	auto expected = vector<Event>({
		Event{1, 0, 0}, Event{0, 0, 0},
		Event{1, 7200, 7200}, Event{0, 7200, 7200},
		Event{1, 14400, 14400}, Event{0, 14400, 14400},
		Event{1, 21600, 21600}, Event{0, 21600, 21600},
		Event{1, 28800, 28800}, Event{0, 28800, 28800},
		Event{1, 36000, 36000}, Event{0, 36000, 36000},
		Event{1, 43200, 43200}, Event{0, 43200, 43200},
		Event{1, 50400, 50400}, Event{0, 50400, 50400},
		Event{1, 57600, 57600}, Event{0, 57600, 57600},
		Event{1, 64800, 64800}, Event{0, 64800, 64800},
		Event{1, 72000, 72000}, Event{0, 72000, 72000},
		Event{1, 79200, 79200}, Event{0, 79200, 79200},
		Event{1, 86400, 86400}, Event{0, 86400, 86400},
		Event{1, 93600, 93600}, Event{0, 93600, 93600},
		Event{1, 100800, 100800}, Event{0, 100800, 100800},
	});
	ASSERT_EQUALS(expected, actualTimes);
}

unittest("rectifier: audio timescale rounding compensation") {
	const auto videoRate = Fraction(25, 1);
	const auto audioFrameDur = Fraction(1024, 44100);
	auto times = generateInterleavedEvents(audioFrameDur, 0, 0, videoRate.inverse(), 0, 0);
	for (auto& t : times)
		if (t.index == 0)
			if (t.mediaTime != 0) // this test assumes we start at 0
				t.mediaTime += 1 + (t.mediaTime % 2); // insert offset and rounding
	vector<unique_ptr<ModuleS>> generators;
	generators.push_back(createModuleWithSize<AudioGenerator44100>(100, audioFrameDur.inverse()));
	generators.push_back(createModuleWithSize<VideoGenerator>(100, videoRate));
	auto actualTimes = runRectifier(videoRate, generators, times);
	auto expected = vector<Event>({
		Event{1, 0, 0}, Event{0, 0, 0},
		Event{1, 7200, 7200}, Event{0, 7200, 7200},
		Event{1, 14400, 14400}, Event{0, 14400, 14400},
		Event{1, 21600, 21600}, Event{0, 21600, 21600},
		Event{1, 28800, 28800}, Event{0, 28800, 28800},
		Event{1, 36000, 36000}, Event{0, 36000, 36000},
		Event{1, 43200, 43200}, Event{0, 43200, 43200},
		Event{1, 50400, 50400}, Event{0, 50400, 50400},
		Event{1, 57600, 57600}, Event{0, 57600, 57600},
		Event{1, 64800, 64800}, Event{0, 64800, 64800},
		Event{1, 72000, 72000}, Event{0, 72000, 72000},
		Event{1, 79200, 79200}, Event{0, 79200, 79200},
		Event{1, 86400, 86400}, Event{0, 86400, 86400},
		Event{1, 93600, 93600}, Event{0, 93600, 93600},
		Event{1, 100800, 100800}, Event{0, 100800, 100800},
	});
	ASSERT_EQUALS(expected, actualTimes);
}

unittest("rectifier: audio 44100, video 29.97") {
	const auto videoRate = Fraction(30000, 1001);
	const auto audioFrameDur = Fraction(1024, 44100);
	auto times = generateInterleavedEvents(audioFrameDur, 0, 0, videoRate.inverse(), 0, 0);
	vector<unique_ptr<ModuleS>> generators;
	generators.push_back(createModuleWithSize<AudioGenerator44100>(100, audioFrameDur.inverse()));
	generators.push_back(createModuleWithSize<VideoGenerator>(100, videoRate));
	auto actualTimes = runRectifier(videoRate, generators, times);
	auto expected = vector<Event>({
		Event{1, 0, 0}, Event{0, 0, 0},
		Event{1, 6006, 6006}, Event{0, 6006, 6006},
		Event{1, 12012, 12012}, Event{0, 12012, 12012},
		Event{1, 18018, 18018}, Event{0, 18018, 18018},
		Event{1, 24024, 24024}, Event{0, 24024, 24024},
		Event{1, 30030, 30030}, Event{0, 30030, 30030},
		Event{1, 36036, 36036}, Event{0, 36036, 36036},
		Event{1, 42042, 42042}, Event{0, 42042, 42042},
		Event{1, 48048, 48048}, Event{0, 48048, 48048},
		Event{1, 54054, 54054}, Event{0, 54054, 54054},
		Event{1, 60060, 60060}, Event{0, 60060, 60060},
		Event{1, 66066, 66066}, Event{0, 66066, 66066},
		Event{1, 72072, 72072}, Event{0, 72072, 72072},
		Event{1, 78078, 78078}, Event{0, 78078, 78078},
		Event{1, 84084, 84084}, Event{0, 84084, 84084},
		Event{1, 90090, 90090}, Event{0, 90090, 90090},
		Event{1, 96096, 96096}, Event{0, 96096, 96096},
	});
	ASSERT_EQUALS(expected, actualTimes);
}
