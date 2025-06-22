#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

template<typename T>
class GenericFifo {
	public:
		void write(const T* data, size_t len) {
			if (!len) return;
			m_data.resize(m_writePos + len);
			memcpy(&m_data[m_writePos], data, len);
			m_writePos += len;
		}

		const T* readPointer() {
			return &m_data[m_readPos];
		}

		void consume(size_t numBytes) {
			assert(numBytes <= bytesToRead());
			m_readPos += numBytes;

			// shift everything to the beginning of the buffer
			memmove(m_data.data(), m_data.data() + m_readPos, bytesToRead());
			m_writePos -= m_readPos;
			m_readPos = 0;
		}

		size_t bytesToRead() const {
			return m_writePos - m_readPos;
		}

	private:
		size_t m_writePos = 0;
		size_t m_readPos = 0;
		std::vector<T> m_data;
};

typedef GenericFifo<uint8_t> Fifo;
