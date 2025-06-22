#include "file.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_media/common/attributes.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp" // Info

using namespace Modules;

namespace {

auto const IOSIZE = 66176;
static_assert(IOSIZE % 32 == 0, "IOSIZE must be a multiple of 32");
static_assert(IOSIZE % 188 == 0, "IOSIZE must be a multiple of 188");

class FileInput : public Module {
	public:
		FileInput(KHost* host, FileInputConfig const& config) : m_host(host) {
			m_blockSize = config.blockSize ? config.blockSize : IOSIZE;
			file = fopen(config.filename.c_str(), "rb");
			if (!file)
				throw error(format("Can't open file for reading: %s", config.filename));
			fseek(file, 0, SEEK_END);
			auto const size = ftell(file);
			fseek(file, 0, SEEK_SET);
			if (size > m_blockSize)
				m_host->log(Info, format("File %s size is %s, will be sent by %s bytes chunks. Check the downstream modules are able to aggregate data frames.",
				    config.filename, size, m_blockSize).c_str());

			m_host->activate(true);

			output = addOutput();
		}

		~FileInput() {
			fclose(file);
		}

		void process() override {
			auto out = output->allocData<DataRawResizable>(m_blockSize);
			size_t read = fread(out->buffer->data().ptr, 1, m_blockSize, file);
			if (read == 0) {
				m_host->activate(false);
				return;
			}
			out->resize(read);
			out->set(PresentationTime{0});
			output->post(out);
		}

	private:
		KHost* const m_host;
		FILE *file;
		OutputDefault *output;
		int m_blockSize;
};

IModule* createObject(KHost* host, void* va) {
	auto config = (FileInputConfig*)va;
	enforce(host, "FileInput: host can't be NULL");
	enforce(config, "FileInput: config can't be NULL");
	return createModule<FileInput>(host, *config).release();
}

auto const registered = Factory::registerModule("FileInput", &createObject);

}
