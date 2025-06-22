#include "helper_dyn.hpp"
#include "helper_input.hpp"
#include "lib_utils/log.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/tools.hpp" // safe_cast

namespace Modules {

KInput* Module::addInput() {
	inputs.push_back(make_unique<Input>(this));
	return inputs.back().get();
}

MetadataCap::MetadataCap(Metadata metadata) : m_metadata(metadata) {
}

void MetadataCap::setMetadata(Metadata metadata) {
	if (metadata == m_metadata)
		return;

	if (!m_metadata) {
		g_Log->log(Debug, "Output: metadata transported by data changed. Updating.");
		m_metadata = metadata;
		return;
	}

	if (metadata->type != m_metadata->type)
		throw std::runtime_error(format("Metadata update: incompatible types %s for data and %s for attached",
		    (int)metadata->type, (int)m_metadata->type));

	if (*m_metadata == *metadata) {
		g_Log->log(Debug, "Output: metadata not equal but comparable by value. Updating.");
		m_metadata = metadata;
	} else {
		g_Log->log(Info, "Metadata update from data not supported yet: output port and data won't carry the same metadata.");
	}
}

bool MetadataCap::updateMetadata(Data &data) {
	if (!data)
		return false;

	auto const &metadata = data->getMetadata();
	if (!metadata) {
		const_cast<DataBase*>(data.get())->setMetadata(m_metadata);
		return true;
	}

	if (metadata == m_metadata)
		return false;

	setMetadata(metadata);
	return true;
}

void NullHostType::log(int level, char const* msg) {
	if(0)
		printf("[%d] %s\n", level, msg);
}

void Output::post(Data data) {
	m_metadataCap.updateMetadata(data);
	signal.emit(data);
}

void Output::connect(IInput* next) {
	signal.connect([=](Data data) {
		next->push(data);
	});
}

void Output::disconnect() {
	signal.disconnectAll();
}

Metadata Output::getMetadata() const {
	return m_metadataCap.getMetadata();
}

void Output::setMetadata(Metadata metadata) {
	m_metadataCap.setMetadata(metadata);
}

void Output::connectFunction(std::function<void(Data)> f) {
	signal.connect(f);
}


// used by unit tests
void ConnectOutput(IOutput* o, std::function<void(Data)> f) {
	auto output = safe_cast<Output>(o);
	output->connectFunction(f);
}

}
