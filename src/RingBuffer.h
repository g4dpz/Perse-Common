#ifndef CIRCUITOS_RINGBUFFER_H
#define CIRCUITOS_RINGBUFFER_H

#include <cstdint>
#include <cstddef>
#include <cstring>

class RingBuffer {
public:
	RingBuffer(size_t size);
	virtual ~RingBuffer();

	size_t writeAvailable();
	size_t readAvailable();

	size_t read(uint8_t* destination, size_t n);
	size_t write(uint8_t* source, size_t n);

	/// Peek at data at the given offset. If the requested range wraps the
	/// internal buffer boundary, data is linearised into an internal scratch
	/// buffer. The returned pointer is valid until the next peek() or write().
	/// Returns nullptr if insufficient data available.
	template<typename T>
	const T* peek(size_t offset = 0){
		if(offset + sizeof(T) > readAvailable()) return nullptr;

		size_t startPos = (beginning + offset) % size;
		size_t endPos = startPos + sizeof(T);

		if(endPos <= size){
			// Contiguous — return direct pointer
			return reinterpret_cast<const T*>(buffer + startPos);
		}

		// Wraps — linearise into scratch buffer
		size_t firstPart = size - startPos;
		memcpy(peekBuf, buffer + startPos, firstPart);
		memcpy(peekBuf + firstPart, buffer, sizeof(T) - firstPart);
		return reinterpret_cast<const T*>(peekBuf);
	}

	size_t skip(size_t n);

	void clear();

private:
	size_t size;

	size_t beginning = 0;
	size_t end = 0;

	uint8_t* buffer;
	uint8_t peekBuf[64];  // scratch buffer for wrap-boundary peeks
};


#endif //CIRCUITOS_RINGBUFFER_H
