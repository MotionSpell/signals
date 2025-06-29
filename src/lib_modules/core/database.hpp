#pragma once

#include "buffer.hpp"
#include <cstring> //memcpy
#include <memory>
#include <vector>
#include "lib_utils/small_map.hpp"
#include "lib_utils/clock.hpp"

namespace Modules {

struct IMetadata;

//A generic timed data container with metadata.
class DataBase {
	public:
		DataBase (const DataBase&) = delete;
		DataBase& operator= (const DataBase&) = delete;
		virtual ~DataBase() = default;

		static void clone(DataBase const * const src, DataBase * const dst) {
			dst->buffer = src->buffer;
			dst->setMetadata(src->getMetadata());
			dst->copyAttributes(*src);
		}

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

		virtual std::shared_ptr<DataBase> clone() const = 0;

	protected:
		DataBase() = default;

	private:
		std::shared_ptr<const IMetadata> metadata;
		std::vector<uint8_t> attributes;
		SmallMap<int, int> attributeOffset;
};

class DataRaw : public DataBase {
	public:
		DataRaw(size_t size);
		std::shared_ptr<DataBase> clone() const override;
};

class DataRawResizable : public DataRaw {
	public:
		DataRawResizable(size_t size);
		void resize(size_t size);
};

using Data = std::shared_ptr<const DataBase>;
using Metadata = std::shared_ptr<const IMetadata>;

inline bool isDeclaration(Data data) {
	return data->buffer == nullptr;
}

}

[[noreturn]] void throw_dynamic_cast_error(const char* typeName);

template<class T>
std::shared_ptr<T> safe_cast(std::shared_ptr<const Modules::DataBase> p) {
	if (!p)
		return nullptr;
	if (auto r = std::dynamic_pointer_cast<T>(p))
		return r;

	throw_dynamic_cast_error(typeid(T).name());
}
