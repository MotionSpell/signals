#pragma once

#include "buffer.hpp"
#include <cstring> //memcpy
#include <memory>
#include <vector>
#include "lib_utils/small_map.hpp"
#include "lib_utils/clock.hpp"
#include "lib_utils/tools.hpp"

namespace Modules {

struct IMetadata;

//A generic timed data container with metadata.
class DataBase {
	public:
		DataBase();
		virtual ~DataBase() = default;

		std::shared_ptr<const IMetadata> getMetadata() const;
		void setMetadata(std::shared_ptr<const IMetadata> metadata);

		SpanC getAttribute(int typeId) const;
		void setAttribute(int typeId, SpanC data);
		void copyAttributes(DataBase const& from);

		template<typename Type>
		Type get() const {
			auto data = getAttribute(Type::TypeId);
			Type r;
			memcpy(&r, data.ptr, sizeof r);
			return r;
		}

		template<typename Type>
		void set(const Type& attribute) {
			static_assert(std::is_pod<Type>::value, "Type must be POD");
			setAttribute(Type::TypeId, {(const uint8_t*)&attribute, sizeof attribute});
		}

		std::shared_ptr<IBuffer> buffer;

		SpanC data() const {
			if(!buffer)
				return {};
			return ((const IBuffer*)buffer.get())->data();
		}

	private:
		std::shared_ptr<const IMetadata> metadata;
		std::vector<uint8_t> attributes;
		SmallMap<int, int> attributeOffset;

		// TODO: remove this
	public:
		// Deprecated: use 'set(PresentationTime{xxx})' instead.
		void setMediaTime(int64_t timeIn180k, uint64_t timescale = IClock::Rate);
};

std::shared_ptr<DataBase> clone(std::shared_ptr<const DataBase> data);

class DataBaseRef : public DataBase {
	public:
		DataBaseRef(std::shared_ptr<const DataBase> data);
		std::shared_ptr<const DataBase> getData() const;

	private:
		std::shared_ptr<const DataBase> dataRef;
};

class DataRaw : public DataBase {
	public:
		DataRaw(size_t size);
};

using Data = std::shared_ptr<const DataBase>;
using Metadata = std::shared_ptr<const IMetadata>;

inline bool isDeclaration(Data data) {
	auto refData = std::dynamic_pointer_cast<const DataBaseRef>(data);
	if(refData && !refData->getData())
		return true;

	return data->buffer == nullptr;
}

}

template<class T>
std::shared_ptr<T> safe_cast(std::shared_ptr<const Modules::DataBase> p) {
	if (!p)
		return nullptr;
	if (auto r = std::dynamic_pointer_cast<T>(p))
		return r;

	if (auto ref = std::dynamic_pointer_cast<const Modules::DataBaseRef>(p)) {
		if (auto r = std::dynamic_pointer_cast<T>(ref->getData()))
			return r;
		if (auto r = std::dynamic_pointer_cast<const Modules::DataBase>(ref->getData()))
			return safe_cast<T>(r);
	}

    if (auto r = std::dynamic_pointer_cast<T>(p)) {
        std::cout << "DEBUG safe cast: Cast successful!\n";
        return r;
    }

    if (auto ref = std::dynamic_pointer_cast<const Modules::DataBaseRef>(p)) {
        if (auto r = std::dynamic_pointer_cast<T>(ref->getData())) {
            std::cout << "DEBUG safe cast: Cast successful via DataBaseRef!\n";
            return r;
        }
    }

    std::cerr << "ERROR :  dynamic_cast failed from " << typeid(*p).name() << " to " << typeid(T).name() << std::endl;
	throw_dynamic_cast_error("Modules::Data", typeid(T).name());
}

