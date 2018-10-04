#pragma once

#include <string>
#include <vector>

struct HttpOutputConfig {
	struct Flags {
		bool InitialEmptyPost = true;
		bool Chunked = true; //use HTTP chunked transfer mode (disabling it is experimental).
		bool UsePUT = false; //use PUT instead of POST
	};

	std::string url;
	std::string userAgent = "GPAC Signals/";
	std::vector<std::string> headers {};

	// optional: data to transfer just before closing the connection
	std::vector<uint8_t> endOfSessionSuffix {};

	Flags flags {};
};

#include "lib_modules/utils/helper.hpp"

namespace Modules {
namespace Out {

class HTTP : public ModuleS {
	public:

		HTTP(IModuleHost* host, HttpOutputConfig const& cfg);
		virtual ~HTTP();

		void process(Data data) final;
		void flush() final;

		struct Controller {
			virtual void newFileCallback(span<uint8_t>) {}
		};
		Controller* m_controller = &m_nullController;

		void readTransferedBs(uint8_t* dst, size_t size);

	private:
		Controller m_nullController;

		enum State {
			RunNewConnection, //untouched, send from previous ftyp/moov
			RunNewFile,       //execute newFileCallback()
			RunResume,        //untouched
			Stop,             //close the connection
		};

		bool loadNextData();
		void clean();

		void threadProc(bool chunked);

		IModuleHost* const m_host;

		struct Private;

		std::unique_ptr<Private> m_pImpl;

		Data m_currData;
		span<const uint8_t> m_currBs {}; // points into the contents of m_currData

		static size_t staticCurlCallback(void *ptr, size_t size, size_t nmemb, void *userp);
		size_t fillBuffer(span<uint8_t> buffer);

		const std::string url;
		const std::vector<uint8_t> endOfSessionSuffix;
		State state {};
		OutputDataDefault<DataRaw> *outputFinished;
};

}
}
