#include "tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_media/transform/restamp.hpp"

using namespace Tests;
using namespace Modules;

namespace {

unittest("global clock") {
	for (int i = 0; i < 5; ++i) {
		auto const now = g_DefaultClock->now();
		std::cout << "Time: " << now << std::endl;

		auto const duration = std::chrono::milliseconds(20);
		std::this_thread::sleep_for(duration);
	}
}

unittest("basic clock, speed 0.5x") {
	Clock clock(0.5);
	for (int i = 0; i < 5; ++i) {
		auto const now = clock.now();
		std::cout << "Time: " << now << std::endl;

		auto const duration = std::chrono::milliseconds(20);
		std::this_thread::sleep_for(duration);
	}
}

unittest("basic clock, speed 2x") {
	Clock clock(2.0);
	for (int i = 0; i < 5; ++i) {
		auto const now = clock.now();
		std::cout << "Time: " << now << std::endl;

		auto const duration = std::chrono::milliseconds(20);
		std::this_thread::sleep_for(duration);
	}
}

unittest("restamp: passthru with offsets") {
	const uint64_t time = 10001;
	auto data = std::make_shared<DataRaw>(0);

	data->setTime(time);
	auto restamp = uptr(create<Transform::Restamp>(Transform::Restamp::Reset));
	restamp->process(data);
	ASSERT_EQUALS(0, data->getTime());

	data->setTime(time);
	restamp = uptr(create<Transform::Restamp>(Transform::Restamp::Reset, 0));
	restamp->process(data);
	ASSERT_EQUALS(0, data->getTime());

	data->setTime(time);
	restamp = uptr(create<Transform::Restamp>(Transform::Restamp::Reset, time));
	restamp->process(data);
	ASSERT_EQUALS(time, data->getTime());
}

unittest("restamp: reset with offsets") {
	uint64_t time = 10001;
	int64_t offset = -100;
	auto data = std::make_shared<DataRaw>(0);

	data->setTime(time);
	auto restamp = uptr(create<Transform::Restamp>(Transform::Restamp::Passthru));
	restamp->process(data);
	ASSERT_EQUALS(time, data->getTime());

	data->setTime(time);
	restamp = uptr(create<Transform::Restamp>(Transform::Restamp::Passthru, 0));
	restamp->process(data);
	ASSERT_EQUALS(time, data->getTime());

	data->setTime(time);
	restamp = uptr(create<Transform::Restamp>(Transform::Restamp::Passthru, offset));
	restamp->process(data);
	ASSERT_EQUALS(time + offset, data->getTime());

	data->setTime(time);
	restamp = uptr(create<Transform::Restamp>(Transform::Restamp::Passthru, time));
	restamp->process(data);
	ASSERT_EQUALS(time+time, data->getTime());
}

}
