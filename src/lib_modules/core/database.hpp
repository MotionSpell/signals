#pragma once

#include "data.hpp"
#include <memory>
#include <vector>
#include "lib_utils/clock.hpp"

namespace Modules {

struct IMetadata;

auto const DATA_FLAGS_DISCONTINUITY = 0x0001;

//A generic timed data container with metadata.
class DataBase : public IData {
	public:
		virtual ~DataBase() = default;

		std::shared_ptr<const IMetadata> getMetadata() const;
		void setMetadata(std::shared_ptr<const IMetadata> metadata);

		void setMediaTime(int64_t timeIn180k, uint64_t timescale = IClock::Rate);
		int64_t getMediaTime() const;

		uint32_t flags = 0;

	private:
		int64_t mediaTimeIn180k = 0;
		std::shared_ptr<const IMetadata> metadata;
};

class DataBaseRef : public DataBase {
	public:
		DataBaseRef(std::shared_ptr<const DataBase> data);
		std::shared_ptr<const DataBase> getData() const;

		bool isRecyclable() const override;
		Span data() override;
		SpanC data() const override;
		void resize(size_t size) override;

	private:
		std::shared_ptr<const DataBase> dataRef;
};

/* automatic inputs have a loose datatype */
struct DataLoose : public DataBase {};

class DataRaw : public DataBase {
	public:
		DataRaw(size_t size);
		bool isRecyclable() const override;
		Span data() override;
		SpanC data() const override;
		void resize(size_t size) override;

	private:
		std::vector<uint8_t> buffer;
};

typedef std::shared_ptr<const DataBase> Data;

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

	throw std::runtime_error("dynamic cast error: could not convert from Modules::Data to " + std::string(typeid(T).name()));
}

