// Test: EventPool Rapid Alloc/Dealloc Cycling
// Validates: Requirements 7.1, 7.2, 7.3
//
// 7.1: Allocate all blocks then deallocate in random order; verify freeCount restored
// 7.2: Interleave alloc-dealloc 1000 times; verify freeCount == blockCount at end
// 7.3: Allocate all, deallocate all, allocate all again; verify all pointers non-null

#include "freertos_mock.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <ctime>

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

static unsigned int seed;

// Fisher-Yates shuffle
static void shuffle(std::vector<void*>& vec) {
    for (size_t i = vec.size() - 1; i > 0; i--) {
        size_t j = rand_r(&seed) % (i + 1);
        std::swap(vec[i], vec[j]);
    }
}

// 7.1: Allocate all blocks then deallocate in random order; verify freeCount restored
void test_alloc_all_dealloc_random_order() {
    const size_t blockSize = 32;
    const size_t blockCount = 16;
    EventPool pool(blockSize, blockCount);

    // Allocate all blocks
    std::vector<void*> ptrs;
    ptrs.reserve(blockCount);
    for (size_t i = 0; i < blockCount; i++) {
        void* p = pool.allocate();
        ASSERT(p != nullptr);
        ptrs.push_back(p);
    }
    ASSERT(pool.getFreeCount() == 0);

    // Shuffle to get random deallocation order
    shuffle(ptrs);

    // Deallocate all in random order
    for (void* p : ptrs) {
        pool.deallocate(p);
    }

    // Verify freeCount is fully restored
    ASSERT(pool.getFreeCount() == blockCount);
}

// 7.2: Interleave alloc-dealloc 1000 times; verify freeCount == blockCount at end
void test_interleaved_alloc_dealloc_1000() {
    const size_t blockSize = 64;
    const size_t blockCount = 8;
    EventPool pool(blockSize, blockCount);

    for (int i = 0; i < 1000; i++) {
        void* p = pool.allocate();
        ASSERT(p != nullptr);
        pool.deallocate(p);
    }

    // After equal alloc/dealloc counts, freeCount should equal blockCount
    ASSERT(pool.getFreeCount() == blockCount);
}

// 7.3: Allocate all, deallocate all, allocate all again; verify all pointers non-null
void test_alloc_dealloc_alloc_again() {
    const size_t blockSize = 48;
    const size_t blockCount = 10;
    EventPool pool(blockSize, blockCount);

    // First round: allocate all
    std::vector<void*> firstRound;
    firstRound.reserve(blockCount);
    for (size_t i = 0; i < blockCount; i++) {
        void* p = pool.allocate();
        ASSERT(p != nullptr);
        firstRound.push_back(p);
    }
    ASSERT(pool.getFreeCount() == 0);

    // Deallocate all
    for (void* p : firstRound) {
        pool.deallocate(p);
    }
    ASSERT(pool.getFreeCount() == blockCount);

    // Second round: allocate all again — all must be non-null
    std::vector<void*> secondRound;
    secondRound.reserve(blockCount);
    for (size_t i = 0; i < blockCount; i++) {
        void* p = pool.allocate();
        ASSERT(p != nullptr);
        secondRound.push_back(p);
    }
    ASSERT(pool.getFreeCount() == 0);
}

int main() {
    seed = static_cast<unsigned int>(time(nullptr));
    printf("EventPool Cycling Tests (seed=%u)\n", seed);

    RUN_TEST(test_alloc_all_dealloc_random_order);
    RUN_TEST(test_interleaved_alloc_dealloc_1000);
    RUN_TEST(test_alloc_dealloc_alloc_again);

    TEST_SUMMARY();
}
