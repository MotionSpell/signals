#include "span.hpp"

struct CrcTable {
	uint32_t data[256];
};

static constexpr CrcTable initCrc(int bits, uint32_t poly) {
	CrcTable r {};
	for (int i = 0; i < 256; i++) {
		uint32_t c = i << 24;
		for (int j = 0; j < 8; j++)
			c = (c << 1) ^ ((poly << (32 - bits)) & (((int32_t) c) >> 31));
		r.data[i] = c;
	}

	return r;
}

uint32_t Crc32(SpanC data) {
	static constexpr auto table = initCrc(32, 0x04C11DB7);
	uint32_t r = 0xffffffff;

	for(auto& b : data)
		r = (r << 8) ^ table.data[((r >> 24) ^ b) & 0xff];

	return r;
}

