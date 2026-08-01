#ifndef PERSE_COMMON_EVENTPOOL_H
#define PERSE_COMMON_EVENTPOOL_H

#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/// Fixed-size block memory pool. Thread-safe via FreeRTOS mutex.
/// Allocates a single contiguous region at construction and manages
/// a free-list stack for O(1) allocate/deallocate operations.
class EventPool {
public:
	/// @param blockSize  Size of each block (must be >= largest event data)
	/// @param blockCount Number of blocks in the pool
	EventPool(size_t blockSize, size_t blockCount);
	~EventPool();

	/// Allocate a block. Returns nullptr if pool exhausted.
	void* allocate();

	/// Return a block to the pool.
	void deallocate(void* ptr);

	size_t getBlockSize() const { return blockSize; }
	size_t getFreeCount() const;

private:
	uint8_t* storage;          // contiguous allocation
	void** freeList;           // stack of free block pointers
	size_t freeCount;
	size_t blockSize;
	size_t blockCount;
	SemaphoreHandle_t mutex;
};

#endif //PERSE_COMMON_EVENTPOOL_H
