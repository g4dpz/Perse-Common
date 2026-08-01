// Feature: cross-project-optimisations, Property 1: Event Pool Allocation Round-Trip
// **Validates: Requirements 11.1, 11.2**
//
// For random sequences of N allocations (N <= blockCount):
//   - Each pointer is within [storage, storage + blockSize * blockCount)
//   - After deallocating all, freeCount == blockCount
// 100+ iterations with varying blockSize (16-128) and blockCount (4-64)

#include "freertos_mock.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <ctime>

// --- Inline EventPool implementation for host compilation ---

// Stub esp_log
#define ESP_LOGE(tag, ...) do { printf("[ERROR] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#define ESP_LOGW(tag, ...) do {} while(0)

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
        freeList[freeCount] = ptr;
        freeCount++;
        xSemaphoreGive(mutex);
    }

    size_t getBlockSize() const { return blockSize; }

    size_t getFreeCount() const {
        xSemaphoreTake(mutex, portMAX_DELAY);
        size_t count = freeCount;
        xSemaphoreGive(mutex);
        return count;
    }

    // Expose storage for bounds checking in tests
    uint8_t* getStorage() const { return storage; }
    size_t getBlockCount() const { return blockCount; }

private:
    uint8_t* storage;
    void** freeList;
    size_t freeCount;
    size_t blockSize;
    size_t blockCount;
    SemaphoreHandle_t mutex;
};

// --- Property test ---

static unsigned int seed;

static size_t randRange(size_t min, size_t max) {
    return min + (rand_r(&seed) % (max - min + 1));
}

void test_allocation_round_trip() {
    const int iterations = 200;

    for (int iter = 0; iter < iterations; iter++) {
        size_t blockSize = randRange(16, 128);
        size_t blockCount = randRange(4, 64);
        size_t allocCount = randRange(1, blockCount);

        EventPool pool(blockSize, blockCount);
        uint8_t* storageBase = pool.getStorage();
        uint8_t* storageEnd = storageBase + blockSize * blockCount;

        std::vector<void*> ptrs;
        ptrs.reserve(allocCount);

        // Allocate N blocks
        for (size_t i = 0; i < allocCount; i++) {
            void* ptr = pool.allocate();
            ASSERT(ptr != nullptr);

            // Each pointer must be within storage range
            uint8_t* p = static_cast<uint8_t*>(ptr);
            ASSERT(p >= storageBase);
            ASSERT(p < storageEnd);

            // Pointer should be aligned to block boundary
            size_t offset = static_cast<size_t>(p - storageBase);
            ASSERT(offset % blockSize == 0);

            ptrs.push_back(ptr);
        }

        // All pointers should be distinct
        for (size_t i = 0; i < ptrs.size(); i++) {
            for (size_t j = i + 1; j < ptrs.size(); j++) {
                ASSERT(ptrs[i] != ptrs[j]);
            }
        }

        // Verify free count decreased
        ASSERT(pool.getFreeCount() == blockCount - allocCount);

        // Deallocate all
        for (void* ptr : ptrs) {
            pool.deallocate(ptr);
        }

        // After deallocating all, freeCount == blockCount
        ASSERT(pool.getFreeCount() == blockCount);
    }
}

void test_exhaustion_returns_nullptr() {
    const int iterations = 100;

    for (int iter = 0; iter < iterations; iter++) {
        size_t blockSize = randRange(16, 64);
        size_t blockCount = randRange(4, 32);

        EventPool pool(blockSize, blockCount);

        // Allocate all blocks
        for (size_t i = 0; i < blockCount; i++) {
            void* ptr = pool.allocate();
            ASSERT(ptr != nullptr);
        }

        // Next allocation should return nullptr
        void* ptr = pool.allocate();
        ASSERT(ptr == nullptr);
        ASSERT(pool.getFreeCount() == 0);
    }
}

int main() {
    seed = static_cast<unsigned int>(time(nullptr));
    printf("Property 1: Event Pool Allocation Round-Trip (seed=%u)\n", seed);

    RUN_TEST(test_allocation_round_trip);
    RUN_TEST(test_exhaustion_returns_nullptr);

    TEST_SUMMARY();
}
