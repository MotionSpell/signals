#include "filesystem.hpp"
#include "lib_media/common/metadata_file.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/os.hpp"
#include "lib_utils/tools.hpp" // enforce

#include <map>
#include <fstream>

using namespace Modules;

namespace {

std::ofstream openOutput(std::string path) {
	auto fp = std::ofstream(path, std::ios::binary);
	if(!fp.is_open())
		throw std::runtime_error("can't open for writing: '" + path + "'");
	return fp;
}

std::string dirName(std::string path) {
	auto i = path.rfind('/');
	if(i == std::string::npos)
		return ".";
	return path.substr(0, i);
}

void ensureDirRecurse(std::string path) {
	if(path == "")
		return;

	if(!dirExists(path)) {
		auto parent = dirName(path);
		if(!dirExists(parent))
			ensureDirRecurse(parent);

		mkdir(path);
	}
}

class FileSystemSink : public ModuleS {
	public:
		FileSystemSink(KHost* host, FileSystemSinkConfig cfg)
			: m_host(host),
			  m_config(cfg) {
		}

		void processOne(Data data) override {
			auto meta = safe_cast<const MetadataFile>(data->getMetadata());

			auto const path = m_config.directory + "/" + meta->filename;

			if(meta->filesize == INT64_MAX) {
				remove(path.c_str());
				return;
			}

			if(m_files.find(path) == m_files.end()) {
				ensureDirRecurse(dirName(path));
				m_files.insert(std::pair<std::string, std::ofstream> {path, openOutput(path)});
			}

			auto& fp = m_files[path];
			auto contents = data->data();
			fp.write((char*)contents.ptr, contents.len);
			if(!fp)
				m_host->log(Error, "write failure");

			if(meta->EOS)
				m_files.erase(path);
		}

	private:
		KHost* const m_host;
		FileSystemSinkConfig const m_config;
		std::map<std::string, std::ofstream> m_files;
};

IModule* createObject(KHost* host, void* va) {
	auto config = (FileSystemSinkConfig*)va;
	enforce(host, "FileSystemSink: host can't be NULL");
	enforce(config, "FileSystemSink: config can't be NULL");
	return createModule<FileSystemSink>(host, *config).release();
}

auto const registered = Factory::registerModule("FileSystemSink", &createObject);
}
