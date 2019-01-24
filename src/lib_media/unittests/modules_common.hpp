#pragma once

#include "lib_modules/modules.hpp"
#include "lib_media/common/metadata_file.hpp"
#include <iostream> // std::cout

namespace {

struct Meta {
	bool operator==(const Meta& rhs) const {
		return this->filename == rhs.filename
		    && this->mimeType == rhs.mimeType
		    && this->codecName == rhs.codecName
		    && this->durationIn180k == rhs.durationIn180k
		    //FIXME: sizes are sometimes different with FFmpeg on different platforms: && this->filesize == rhs.filesize
		    && this->latencyIn180k == rhs.latencyIn180k
		    && this->startsWithRAP == rhs.startsWithRAP
		    && this->eos == rhs.eos;
	}
	std::string filename, mimeType, codecName;
	uint64_t durationIn180k, filesize, latencyIn180k;
	bool startsWithRAP, eos;
};

struct Listener : public Modules::ModuleS {
	Listener() {
		addInput();
	}
	void process(Modules::Data data) override {
		auto const &m = safe_cast<const Modules::MetadataFile>(data->getMetadata());
		results.push_back({ m->filename, m->mimeType, m->codecName,
		        m->durationIn180k, m->filesize, m->latencyIn180k, m->startsWithRAP, m->EOS });
	}
	void print() { //used for generating reference results
		for (auto &r : results) {
			std::cout << "{ \"" << r.filename << "\", \"" << r.mimeType << "\", \"" << r.codecName << "\", " << r.durationIn180k << ", " << r.filesize << ", " << r.latencyIn180k << ", " << r.startsWithRAP << ", " << r.eos << " }," << std::endl;
		}
	}

	std::vector<Meta> results;
};

}
