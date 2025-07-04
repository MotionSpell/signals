#include "database.hpp"
#include "raw_buffer.hpp"
#include <cstring> // memcpy
#include <stdexcept> //runtime_error

namespace Modules {

std::shared_ptr<const IMetadata> DataBase::getMetadata() const {
	return metadata;
}

void DataBase::setMetadata(std::shared_ptr<const IMetadata> metadata) {
	this->metadata = metadata;
}

SpanC DataBase::getAttribute(int typeId) const {
	auto first = attributeOffset.find(typeId);
	if(first == attributeOffset.end())
		throw std::runtime_error("Attribute not found");
	return {attributes.data() + (*first).value, 0};
}

void DataBase::setAttribute(int typeId, SpanC data) {
	if(!data.ptr)
		throw std::runtime_error("Can't set a NULL attribute");

	{
		auto first = attributeOffset.find(typeId);
		if(first != attributeOffset.end()) {
			// HACK for DecodingTime and PresentationTime.
			if(typeId == 0x35A12022 || typeId == 0x5DF434D0) {
				memcpy(attributes.data() + (*first).value, data.ptr, data.len);
				return;
			}

			throw std::runtime_error("Attribute is already set");
		}
	}

	auto offset = attributes.size();
	attributeOffset[typeId] = offset;
	attributes.resize(offset + data.len);
	memcpy(attributes.data() + offset, data.ptr, data.len);
}

void DataBase::copyAttributes(DataBase const& from) {
	attributeOffset = from.attributeOffset;
	attributes = from.attributes;
}

DataRaw::DataRaw(size_t size) {
	if (size > 0)
		buffer = std::make_shared<RawBuffer>(size);
}

std::shared_ptr<DataBase> DataRaw::clone() const {
	std::shared_ptr<DataBase> clone = std::make_shared<DataRaw>(0);
	DataBase::clone(this, clone.get());
	return clone;
}

DataRawResizable::DataRawResizable(size_t size) : DataRaw(0) {
	buffer = std::make_shared<RawBuffer>(size);
}

void DataRawResizable::resize(size_t size) {
	std::dynamic_pointer_cast<RawBuffer>(buffer)->resize(size);
}

}
