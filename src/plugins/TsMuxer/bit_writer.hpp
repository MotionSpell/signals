#pragma once

#include "span.hpp"
#include <cassert>

struct BitWriter {
	Span dst;

	void u(int n, uint64_t val) {
		for(int i=0; i < n; ++i) {
			int bit = (val >> (n-1-i)) & 1;
			putBit(bit);
		}
	}

	int offset() const {
		assert(m_pos%8 == 0);
		return m_pos/8;
	}

	void putBit(int bit) {
		auto bitIndex = m_pos%8;
		auto byteIndex = m_pos/8;
		auto mask = (1 << (7-bitIndex));
		if(bitIndex == 0)
			dst[byteIndex] = 0;

		if(bit)
			dst[byteIndex] |= mask;

		m_pos++;
	}

	int m_pos = 0;
};

