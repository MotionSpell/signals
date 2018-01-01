#pragma once

#include "lib_modules/core/module.hpp"
#include "lib_gpacpp/gpacpp.hpp"
#include <vector>

//FIXME: should not be needed here
extern "C" {
#include <libavformat/avformat.h>
}

namespace Modules {
namespace Mux {

typedef Queue<AVPacket*> DataInput;

class GPACMuxMPEG2TS : public ModuleDynI, public gpacpp::Init {
public:
	GPACMuxMPEG2TS(bool real_time, unsigned mux_rate, unsigned pcr_ms = 100, int64_t pcr_init_val = -1);
	~GPACMuxMPEG2TS();
	void process() override;

private:
	void declareStream(Data data);
	GF_Err fillInput(GF_ESInterface *esi, u32 ctrl_type, size_t inputIdx);
	static GF_Err staticFillInput(GF_ESInterface *esi, u32 ctrl_type, void *param);

	std::unique_ptr<gpacpp::M2TSMux> muxer;
	std::vector<std::unique_ptr<DataInput>> inputData;
	OutputDefault *output;
};

}
}