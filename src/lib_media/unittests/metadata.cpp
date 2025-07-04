#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_media/common/pcm.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/resolution.hpp"

using namespace std;
using namespace Tests;
using namespace Modules;

namespace {
class FakeOutput : public Module {
	public:
		FakeOutput() {
			output = addOutput();
		}
		void process() {
			PcmFormat fmt;
			auto data = output->allocData<DataPcm>(0, fmt);
			output->post(data);
		}
		void setMetadata(std::shared_ptr<const IMetadata> metadata) {
			output->setMetadata(metadata);
		}

	private:
		OutputDefault *output;
};

class FakeInput : public Module {
	public:
		FakeInput() {
			input = addInput();
		}
		void process() {
			auto data = inputs[0]->pop();
			inputs[0]->updateMetadata(data);
		}

		void setMetadata(std::shared_ptr<const IMetadata> metadata) {
			input->setMetadata(metadata);
		}

	private:
		KInput *input;
};
}

unittest("metadata: backwarded by connection") {
	auto output = createModule<FakeOutput>();
	auto input  = createModule<FakeInput>();
	input->setMetadata(make_shared<MetadataRawAudio>());
	auto o = output->getOutput(0);
	auto i = input->getInput(0);
	ConnectOutputToInput(o, i);
	ASSERT(o->getMetadata() == i->getMetadata());
}

unittest("metadata: not forwarded by connection") {
	auto output = createModule<FakeOutput>();
	auto input = createModule<FakeInput>();
	output->setMetadata(make_shared<MetadataRawAudio>());
	auto o = output->getOutput(0);
	auto i = input->getInput(0);
	ConnectOutputToInput(o, i);
	ASSERT(!i->getMetadata());
}

unittest("metadata: same back and fwd by connection") {
	auto output = createModule<FakeOutput>();
	auto input = createModule<FakeInput>();
	auto meta = make_shared<MetadataRawAudio>();
	input->setMetadata(meta);
	output->setMetadata(meta);
	auto o = output->getOutput(0);
	auto i = input->getInput(0);
	ConnectOutputToInput(o, i);
	ASSERT(o->getMetadata() == i->getMetadata());
}

unittest("metadata: forwarded by data") {
	auto output = createModule<FakeOutput>();
	auto input = createModule<FakeInput>();
	auto o = output->getOutput(0);
	auto i = input->getInput(0);
	ConnectOutputToInput(o, i);
	output->setMetadata(make_shared<MetadataRawAudio>());
	output->process();
	ASSERT(o->getMetadata() == i->getMetadata());
}

unittest("metadata: incompatible by data") {
	auto output = createModule<FakeOutput>();
	auto input = createModule<FakeInput>();
	input->setMetadata(make_shared<MetadataRawAudio>());
	auto o = output->getOutput(0);
	auto i = input->getInput(0);
	ConnectOutputToInput(o, i);
	ASSERT_THROWN(output->setMetadata(make_shared<MetadataRawVideo>()));
}

unittest("metadata: incompatible back and fwd") {
	auto output = createModule<FakeOutput>();
	auto input = createModule<FakeInput>();
	input->setMetadata(make_shared<MetadataRawAudio>());
	output->setMetadata(make_shared<MetadataRawVideo>());
	auto o = output->getOutput(0);
	auto i = input->getInput(0);
	ASSERT_THROWN(ConnectOutputToInput(o, i));
}

unittest("metadata: updated twice by data") {
	auto output = createModule<FakeOutput>();
	auto input = createModule<FakeInput>();
	auto o = output->getOutput(0);
	auto i = input->getInput(0);
	ConnectOutputToInput(o, i);
	output->setMetadata(make_shared<MetadataRawAudio>());
	output->process();
	output->setMetadata(make_shared<MetadataRawAudio>());
	output->process();
	ASSERT(o->getMetadata() == i->getMetadata());
}

unittest("duplicating data") {
	PcmFormat fmt;
	auto data = make_shared<const DataPcm>(0, fmt);

	Data dataCopy = data->clone();
	data = nullptr;

	auto dataCopyPcm = safe_cast<const DataPcm>(dataCopy);
	ASSERT(dataCopyPcm);
}
