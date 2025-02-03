#include "tests/tests.hpp"
#include "lib_utils/i_scheduler.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_modules/core/connection.hpp" // ConnectModules
#include "lib_media/transform/rectifier.hpp"
#include "lib_media/common/pcm.hpp"
#include "lib_media/common/picture.hpp"
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
static std::ostream& operator<<(std::ostream& o, Event t) {
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
			std::sort(m_tasks.begin(), m_tasks.end());
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

template<typename METADATA, typename TYPE>
struct DataGenerator : public ModuleS, public virtual IOutputCap {
	DataGenerator() {
		output = addOutput();
		output->setMetadata(make_shared<METADATA>());
	}
	void processOne(Data dataIn) override {
		auto data = output->allocData<TYPE>(0);
		auto dataPcm = dynamic_pointer_cast<DataPcm>(data);
		if (dataPcm) {
			PcmFormat fmt(48000);
			dataPcm->format = fmt;
			dataPcm->setSampleCount(1024);
		}
		data->set(dataIn->get<PresentationTime>());
		output->post(data);
	}
	OutputDefault *output;
};

typedef DataGenerator<MetadataRawVideo, DataPicture> VideoGenerator;
typedef DataGenerator<MetadataRawAudio, DataPcm> AudioGenerator;

struct Fixture {
	std::shared_ptr<ClockMock> clock = make_shared<ClockMock>();
	vector<unique_ptr<ModuleS>> generators;
	std::shared_ptr<IModule> rectifier;
	vector<Event> actualTimes;

	Fixture(Fraction fps) {
		auto cfg = RectifierConfig { clock, clock.get(), fps };
		rectifier = loadModule("Rectifier", &NullHost, &cfg);
	}

	void setTime(int64_t time) {
		clock->setTime(Fraction(time, IClock::Rate));
	}

	void addStream(int i, std::unique_ptr<ModuleS>&& generator) {
		generators.push_back(std::move(generator));

		ConnectModules(generators[i].get(), 0, rectifier.get(), i);
		ConnectOutput(rectifier->getOutput(i), [i, this](Data data) {
			this->onOutputSample(i, data);
		});
	}

	void push(int index, int64_t mediaTime) {
		auto data = make_shared<DataRaw>(0);
		data->setMediaTime(mediaTime);
		generators[index]->processOne(data);
	}

	void onOutputSample(int i, Data data) {
		actualTimes.push_back(Event{i, fractionToClock(clock->now()), data->get<PresentationTime>().time});
	}
};

vector<Event> runRectifier(
    Fraction fps,
    vector<unique_ptr<ModuleS>> &gen,
    vector<Event> events) {

	Fixture fix(fps);

	for (int i = 0; i < (int)gen.size(); ++i) {
		fix.addStream(i, std::move(gen[i]));
	}

	for (auto event : events) {
		if(event.clockTime > 0)
			fix.setTime(event.clockTime);
		fix.push(event.index, event.mediaTime);
	}

	fix.clock->setTime(fix.clock->now());

	return fix.actualTimes;
}

unittest("rectifier: simple offset") {
	// use '1000' as a human-readable frame period
	auto fix = Fixture(Fraction(IClock::Rate, 1000));
	fix.addStream(0, createModuleWithSize<VideoGenerator>(100));

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
	auto fix = Fixture(Fraction(IClock::Rate, 100));
	fix.addStream(0, createModuleWithSize<VideoGenerator>(100));

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
	auto fix = Fixture(Fraction(IClock::Rate, 100));
	fix.addStream(0, createModuleWithSize<VideoGenerator>(100));

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
	auto fix = Fixture(Fraction(IClock::Rate, 100));
	fix.addStream(0, createModuleWithSize<VideoGenerator>(100));

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
	generators.push_back(createModuleWithSize<GeneratorType>(inTimes.size()));

	auto actualTimes = runRectifier(fps, generators, inTimes);

	fixupTimes(expectedTimes, actualTimes);

	ASSERT_EQUALS(expectedTimes, actualTimes);
}

struct TimePair {
	int64_t mediaTime;
	int64_t clockTime;
};

TimePair generateValuesDefault(uint64_t step, Fraction fps) {
	auto const t = (int64_t)fractionToClock(fps.inverse() * step);
	return TimePair{t, t};
};

vector<Event> generateData(Fraction fps, function<TimePair(uint64_t, Fraction)> generateValue = generateValuesDefault) {
	auto const numItems = (int)(Fraction(15) * fps / Fraction(25, 1));
	vector<Event> times;
	for (int i = 0; i < numItems; ++i) {
		auto tp = generateValue(i, fps);
		times.push_back({0, tp.clockTime, tp.mediaTime});
	}
	return times;
}

void testFPSFactor(Fraction fps, Fraction factor) {
	auto const genVal = [](uint64_t step, Fraction fps) {
		auto const tIn = timescaleToClock(step * fps.den, fps.num);
		return TimePair{(int64_t)tIn, (int64_t)tIn};
	};

	auto const outTimes = generateData(fps * factor, genVal);
	auto const inTimes = generateData(fps);
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
	auto const inGenVal = [](uint64_t step, Fraction fps, int shift) {
		auto const t = (int64_t)(IClock::Rate * (step + shift) * fps.den) / fps.num;
		return TimePair{t, int64_t((IClock::Rate * step * fps.den) / fps.num)};
	};

	auto const outTimes = generateData(fps, bind(inGenVal, placeholders::_1, placeholders::_2, 0));
	auto const inTimes1 = generateData(fps, bind(inGenVal, placeholders::_1, placeholders::_2, 5));
	testRectifierSinglePort<VideoGenerator>(fps, inTimes1, outTimes);

	auto const inTimes2 = generateData(fps, bind(inGenVal, placeholders::_1, placeholders::_2, -5));
	testRectifierSinglePort<VideoGenerator>(fps, inTimes1, outTimes);
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
	auto const inTimes = generateData(fps, inGenVal);

	auto const outGenVal = [](uint64_t step, Fraction fps) {
		auto const t = int64_t((IClock::Rate * step * fps.den) / fps.num);
		return TimePair{t, t};
	};
	auto const outTimes = generateData(fps, outGenVal);

	testRectifierSinglePort<VideoGenerator>(fps, inTimes, outTimes);
}

unittest("rectifier: deal with backward discontinuity (single port)") {
	auto const fps = Fraction(25, 1);
	auto const outGenVal = [](uint64_t step, Fraction fps, int64_t clockTimeOffset, int64_t mediaTimeOffset) {
		auto const mediaTime = (int64_t)(IClock::Rate * (step + mediaTimeOffset) * fps.den) / fps.num;
		auto const clockTime = (int64_t)(IClock::Rate * (step + clockTimeOffset) * fps.den) / fps.num;
		return TimePair{mediaTime, clockTime};
	};
	auto inTimes1 = generateData(fps);
	auto inTimes2 = generateData(fps, bind(outGenVal, placeholders::_1, placeholders::_2, inTimes1.size(), 0));
	auto outTimes1 = generateData(fps);
	auto outTimes2 = generateData(fps, bind(outGenVal, placeholders::_1, placeholders::_2, inTimes1.size(), inTimes1.size()));
	inTimes1.insert(inTimes1.end(), inTimes2.begin(), inTimes2.end());
	outTimes1.insert(outTimes1.end(), outTimes2.begin(), outTimes2.end());
	testRectifierSinglePort<VideoGenerator>(fps, inTimes1, outTimes1);
}

unittest("rectifier: multiple media types simple") {
	// real timescale is required here, because we're dealing with audio
	auto fix = Fixture({25, 1});
	fix.addStream(0, createModuleWithSize<VideoGenerator>(100));
	fix.addStream(1, createModuleWithSize<AudioGenerator>(100));

	// 3840 = (1024 * IClock::Rate) / 48kHz;

	fix.setTime( 1000 + 7200 * 0);
	fix.push(0, 7200 * 0); fix.push(1, 3840 * 0);
	fix.push(0, 7200 * 1); fix.push(1, 3840 * 1);
	fix.setTime( 1000 + 7200 * 0);
	fix.push(0, 7200 * 2); fix.push(1, 3840 * 2);
	fix.setTime( 1000 + 7200 * 1);
	fix.push(0, 7200 * 3); fix.push(1, 3840 * 3);
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

unittest("rectifier: two streams, only the first receives data") {
	const auto videoRate = Fraction(25, 1);
	auto times = generateData(videoRate);
	vector<unique_ptr<ModuleS>> generators;
	generators.push_back(createModuleWithSize<VideoGenerator>(100));
	generators.push_back(createModuleWithSize<AudioGenerator>(100));

	auto actualTimes = runRectifier(videoRate, generators, times);

	ASSERT_EQUALS(times, actualTimes);
}

unittest("rectifier: fail when no video") {
	vector<unique_ptr<ModuleS>> generators;
	generators.push_back(createModuleWithSize<AudioGenerator>(1));

	ASSERT_THROWN(runRectifier(Fraction(25, 1), generators, {Event()}));
}
