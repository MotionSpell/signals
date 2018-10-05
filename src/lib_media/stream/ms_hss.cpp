#include "ms_hss.hpp"
#include "lib_utils/format.hpp"

inline uint32_t U32BE(uint8_t* p) {
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3] << 0);
}

template<size_t N>
constexpr uint32_t FOURCC(const char (&a)[N]) {
	static_assert(N == 5, "FOURCC must be composed of 4 characters");
	return (a[0]<<24) | (a[1]<<16) | (a[2]<<8) | a[3];
}

namespace Modules {
namespace Stream {

MS_HSS::MS_HSS(IModuleHost* host, const std::string &url)
	: m_host(host) {
	auto cfg = HttpOutputConfig{url};
	cfg.endOfSessionSuffix = { 0, 0, 0, 8, 'm', 'f', 'r', 'a' };
	m_http = create<Out::HTTP>(host, cfg);
	m_http->m_controller = this;
}

void MS_HSS::process(Data data) {
	m_http->process(data);
}

void MS_HSS::flush() {
	m_http->flush();
}

void MS_HSS::newFileCallback(span<uint8_t> out) {
	auto buf = out.ptr;

	// skip 'ftyp' box
	m_http->readTransferedBs(buf, 8);
	auto size = U32BE(buf + 0);
	auto type = U32BE(buf + 4);
	if (type != FOURCC("ftyp"))
		throw error("ftyp not found");
	m_http->readTransferedBs(buf, size - 8);

	// skip some box
	m_http->readTransferedBs(buf, 8);
	size = U32BE(buf);
	m_http->readTransferedBs(buf, size - 8);

	// skip 'free' box
	m_http->readTransferedBs(buf, 8);
	size = U32BE(buf + 0);
	type = U32BE(buf + 4);
	if (type != FOURCC("free"))
		throw error("free not found");
	m_http->readTransferedBs(buf, size - 8);

	// put the contents of 'moov' box into 'buf'
	m_http->readTransferedBs(buf, 8);
	size = U32BE(buf + 0);
	type = U32BE(buf + 4);
	if (type != FOURCC("moov"))
		throw error("moov not found");
	m_http->readTransferedBs(buf, size - 8);
}

}
}
