#pragma once

#include "lib_modules/utils/helper.hpp"
#include <atomic>
#include <thread>
#include <vector>

struct AVFormatContext;
struct AVIOContext;
struct AVPacket;

namespace Modules {
class DataAVPacket;

namespace Transform {
class Restamp;
}

namespace Demux {

class LibavDemux : public ActiveModule {
	public:

		typedef std::function<int(uint8_t* buf, int bufSize)> ReadFunc;
		//@param url may be a file, a remote URL, or a webcam (set "webcam" to list the available devices)
		LibavDemux(const std::string &url, bool loop = false, const std::string &avformatCustom = "", uint64_t seekTimeInMs = 0, const std::string &formatName = "", ReadFunc func = nullptr);
		~LibavDemux();
		bool work() override;

	private:
		int readFrame(AVPacket* pkt);
		void clean();
		void webcamList();
		bool webcamOpen(const std::string &options);
		void initRestamp();
		void seekToStart();
		bool rectifyTimestamps(AVPacket &pkt);
		void inputThread();
		void setTimestamp(std::shared_ptr<DataAVPacket> data);
		bool dispatchable(AVPacket * const pkt);
		void dispatch(AVPacket *pkt);
		void sparseStreamsHeartbeat(AVPacket const * const pkt);

		struct Stream {
			OutputDataDefault<DataAVPacket>* output = nullptr;
			uint64_t offsetIn180k = 0;
			int64_t lastDTS = 0;
			std::unique_ptr<Transform::Restamp> restamper;
		};

		std::vector<Stream> m_streams;

		const bool loop;
		bool highPriority = false;
		std::thread workingThread;
		std::atomic_bool done;
		QueueLockFree<AVPacket> packetQueue;
		AVFormatContext* m_formatCtx;
		AVIOContext* m_avioCtx = nullptr;
		const ReadFunc m_read;
		int64_t curTimeIn180k = 0, startPTSIn180k = std::numeric_limits<int64_t>::min();

		static int read(void* user, uint8_t* data, int size) {
			auto pThis = (LibavDemux*)user;
			return pThis->m_read(data, size);
		}
};

}
}
