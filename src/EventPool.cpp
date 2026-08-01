#include "EventPool.h"
#include <cstdlib>
#include <esp_log.h>

static const char* TAG = "EventPool";

EventPool::EventPool(size_t blockSize, size_t blockCount)
	: freeCount(blockCount), blockSize(blockSize), blockCount(blockCount){

	storage = static_cast<uint8_t*>(malloc(blockSize * blockCount));
	if(storage == nullptr){
		ESP_LOGE(TAG, "Failed to allocate pool storage (%zu bytes)", blockSize * blockCount);
		abort();
	}

	freeList = static_cast<void**>(malloc(blockCount * sizeof(void*)));
	if(freeList == nullptr){
		ESP_LOGE(TAG, "Failed to allocate free list (%zu bytes)", blockCount * sizeof(void*));
		free(storage);
		abort();
	}

	// Initialize free list with pointers to each block
	for(size_t i = 0; i < blockCount; i++){
		freeList[i] = storage + i * blockSize;
	}

	mutex = xSemaphoreCreateMutex();
	if(mutex == nullptr){
		ESP_LOGE(TAG, "Failed to create mutex");
		free(freeList);
		free(storage);
		abort();
	}
}

EventPool::~EventPool(){
	if(mutex != nullptr){
		vSemaphoreDelete(mutex);
	}
	free(freeList);
	free(storage);
}

void* EventPool::allocate(){
	xSemaphoreTake(mutex, portMAX_DELAY);

	void* block = nullptr;
	if(freeCount > 0){
		freeCount--;
		block = freeList[freeCount];
	}

	xSemaphoreGive(mutex);
	return block;
}

void EventPool::deallocate(void* ptr){
	if(ptr == nullptr) return;

	xSemaphoreTake(mutex, portMAX_DELAY);

	freeList[freeCount] = ptr;
	freeCount++;

	xSemaphoreGive(mutex);
}

size_t EventPool::getFreeCount() const{
	// Reading freeCount under mutex for consistency
	xSemaphoreTake(mutex, portMAX_DELAY);
	size_t count = freeCount;
	xSemaphoreGive(mutex);
	return count;
}
