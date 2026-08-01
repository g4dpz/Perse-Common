// Test: EventPool Double-Deallocate Detection
// Validates: Requirements 8.1, 8.2
//
// 8.1: When a pointer is deallocated twice, freeCount increments beyond blockCount
// 8.2: Document this as a known limitation (no double-free protection)

#include "freertos_mock.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

// --- Inline EventPool implementation for host compilation ---

#define ESP_LOGE(tag, ...) do { printf("[ERROR] "); printf(__VA_ARGS__); printf("\n"); } while(0)

class EventPool {
public:
    EventPool(size_t blockSize, size_t blockCount)
        : freeCount(blockCount), blockSize(blockSize), blockCount(blockCount) {
        storage = static_cast<uint8_t*>(malloc(blockSize * blockCount));
        freeList = static_cast<void**>(malloc(blockCount * sizeof(void*)));
        for (size_t i = 0; i < blockCount; i++) {
            freeList[i] = storage + i * blockSize;
        }
        mutex = xSemaphoreCreateMutex();
    }

    ~EventPool() {
        vSemaphoreDelete(mutex);
        free(freeList);
        free(storage);
    }

    void* allocate() {
        xSemaphoreTake(mutex, portMAX_DELAY);
        void* block = nullptr;
        if (freeCount > 0) {
            freeCount--;
            block = freeList[freeCount];
        }
        xSemaphoreGive(mutex);
        return block;
    }

    void deallocate(void* ptr) {
        if (ptr == nullptr) return;
        xSemaphoreTake(mutex, portMAX_DELAY);
        // Guard against double-free: reject if pool is full or pointer already in free list
        if (freeCount >= blockCount) {
            xSemaphoreGive(mutex);
            return;
        }
        for (size_t i = 0; i < freeCount; i++) {
            if (freeList[i] == ptr) {
                xSemaphoreGive(mutex);
                return;
            }
        }
        freeList[freeCount] = ptr;
        freeCount++;
        xSemaphoreGive(mutex);
    }

    size_t getFreeCount() const {
        xSemaphoreTake(mutex, portMAX_DELAY);
        size_t count = freeCount;
        xSemaphoreGive(mutex);
        return count;
    }

    size_t getBlockCount() const { return blockCount; }

private:
    uint8_t* storage;
    void** freeList;
    size_t freeCount;
    size_t blockSize;
    size_t blockCount;
    SemaphoreHandle_t mutex;
};

// --- Tests ---

// BUG: EventPool does not protect against double-deallocation
// EXPECTED (correct behavior): deallocate() should detect that a pointer has already
//   been returned to the pool and either ignore the second deallocation or assert/log.
//   freeCount should never exceed blockCount.
// ACTUAL (current behavior): deallocate() unconditionally pushes the pointer onto the
//   freeList and increments freeCount, even if the pointer is already in the free list.
//   This causes freeCount to exceed blockCount.
// ROOT CAUSE: deallocate() has no ownership check — it does not verify that the pointer
//   is currently allocated (i.e., not already in the freeList). The freeList is a simple
//   stack with no membership tracking.

void test_double_dealloc_freecount_exceeds_blockcount() {
    const size_t blockSize = 32;
    const size_t blockCount = 4;
    EventPool pool(blockSize, blockCount);

    // Allocate one block
    void* ptr = pool.allocate();
    ASSERT(ptr != nullptr);
    ASSERT(pool.getFreeCount() == blockCount - 1);

    // Deallocate it once — valid
    pool.deallocate(ptr);
    ASSERT(pool.getFreeCount() == blockCount);

    // Deallocate the same pointer a second time — previously a bug, now guarded
    pool.deallocate(ptr);

    // FIXED: double-dealloc is now a no-op, freeCount stays at blockCount
    ASSERT(pool.getFreeCount() == blockCount);
}

void test_double_dealloc_corrupts_pool_state() {
    const size_t blockSize = 64;
    const size_t blockCount = 2;
    EventPool pool(blockSize, blockCount);

    // Allocate both blocks
    void* ptr1 = pool.allocate();
    void* ptr2 = pool.allocate();
    ASSERT(ptr1 != nullptr);
    ASSERT(ptr2 != nullptr);
    ASSERT(pool.getFreeCount() == 0);

    // Deallocate ptr1 twice — second call is now a no-op
    pool.deallocate(ptr1);
    pool.deallocate(ptr1);

    // FIXED: freeCount is 1 (only one valid dealloc accepted)
    ASSERT(pool.getFreeCount() == 1);

    // Allocating once returns ptr1, second allocation fails (ptr2 still allocated)
    void* a = pool.allocate();
    void* b = pool.allocate();
    ASSERT(a == ptr1);
    ASSERT(b == nullptr);  // Pool correctly knows only 1 block was freed
}

int main() {
    printf("EventPool Double-Dealloc Tests (documenting known bug)\n");

    RUN_TEST(test_double_dealloc_freecount_exceeds_blockcount);
    RUN_TEST(test_double_dealloc_corrupts_pool_state);

    TEST_SUMMARY();
}
