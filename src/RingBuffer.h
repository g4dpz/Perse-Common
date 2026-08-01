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

	template<typename T>
	const T* peek(size_t offset = 0){
		if(offset + sizeof(T) > readAvailable()) return nullptr;
		size_t pos = (beginning + offset) % size;
		// Check if data spans the wrap boundary
		if(pos + sizeof(T) > size){
			// Linearise into peekBuf
			size_t first = size - pos;
			memcpy(peekBuf, buffer + pos, first);
			memcpy(peekBuf + first, buffer, sizeof(T) - first);
			return (const T*) peekBuf;
		}
		return (const T*) (buffer + pos);
	}

	size_t skip(size_t n);

	void clear();

private:
	size_t size;

	size_t beginning = 0;
	size_t end = 0;

	uint8_t* buffer;
	uint8_t peekBuf[64];
};


#endif //CIRCUITOS_RINGBUFFER_H
