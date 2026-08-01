// Feature: cross-project-optimisations, Property 3: PtrQueue Reset Frees All Items
// **Validates: Requirements 12.1, 12.2**
//
// Post N random items, call reset(), assert destructor called N times (use a counter)
// 100+ iterations

#include "freertos_mock.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <memory>

// --- Inline PtrQueue (matching Perse-Common/src/Queue.h) ---

template<typename T>
class PtrQueue {
public:
    PtrQueue(size_t size) : size(size) {
        queue = xQueueCreate(size, sizeof(T*));
    }

    virtual ~PtrQueue() {
        vQueueDelete(queue);
    }

    std::unique_ptr<T> get(TickType_t timeout = portMAX_DELAY) {
        T* ptr;
        if (xQueueReceive(queue, &ptr, timeout) != pdTRUE) return nullptr;
        return std::unique_ptr<T>(ptr);
    }

    std::unique_ptr<T> post(std::unique_ptr<T> item, TickType_t timeout = portMAX_DELAY) {
        T* ptr = item.release();
        if (xQueueSend(queue, &ptr, timeout) == pdTRUE) {
            ptr = nullptr;
        }
        return std::unique_ptr<T>(ptr);
    }

    void reset() {
        T* ptr;
        while (xQueueReceive(queue, &ptr, 0) == pdTRUE) {
            delete ptr;
        }
        xQueueReset(queue);
    }

    const size_t size;

private:
    QueueHandle_t queue;
};

// --- Test item that tracks destructor calls ---

static int destructorCount = 0;

struct TestItem {
    int value;
    TestItem(int v) : value(v) {}
    ~TestItem() { destructorCount++; }
};

// --- Property test ---

static unsigned int seed;

static size_t randRange(size_t min, size_t max) {
    return min + (rand_r(&seed) % (max - min + 1));
}

void test_reset_frees_all_items() {
    const int iterations = 200;

    for (int iter = 0; iter < iterations; iter++) {
        size_t capacity = randRange(4, 64);
        size_t itemCount = randRange(1, capacity);

        PtrQueue<TestItem> q(capacity);
        destructorCount = 0;

        // Post N items
        for (size_t i = 0; i < itemCount; i++) {
            auto item = std::make_unique<TestItem>(static_cast<int>(i));
            auto returned = q.post(std::move(item));
            // All posts should succeed since itemCount <= capacity
            ASSERT(returned == nullptr);
        }

        // Verify no destructors called yet
        ASSERT(destructorCount == 0);

        // Reset should destroy all items
        q.reset();

        // Destructor should have been called exactly N times
        ASSERT(destructorCount == static_cast<int>(itemCount));
    }
}

void test_reset_empty_queue_no_crash() {
    const int iterations = 100;

    for (int iter = 0; iter < iterations; iter++) {
        size_t capacity = randRange(4, 32);
        PtrQueue<TestItem> q(capacity);
        destructorCount = 0;

        // Reset an empty queue
        q.reset();

        ASSERT(destructorCount == 0);
    }
}

void test_partial_get_then_reset() {
    const int iterations = 150;

    for (int iter = 0; iter < iterations; iter++) {
        size_t capacity = randRange(4, 32);
        size_t postCount = randRange(2, capacity);
        size_t getCount = randRange(1, postCount - 1);

        PtrQueue<TestItem> q(capacity);
        destructorCount = 0;

        // Post items
        for (size_t i = 0; i < postCount; i++) {
            auto item = std::make_unique<TestItem>(static_cast<int>(i));
            q.post(std::move(item));
        }

        // Get some items (these are destroyed by unique_ptr going out of scope)
        for (size_t i = 0; i < getCount; i++) {
            auto item = q.get(0);
            ASSERT(item != nullptr);
            // item destroyed here
        }

        int destroyedByGet = destructorCount;
        ASSERT(destroyedByGet == static_cast<int>(getCount));

        // Reset should destroy remaining items
        q.reset();

        size_t remaining = postCount - getCount;
        ASSERT(destructorCount == static_cast<int>(postCount));
        (void)remaining; // suppress unused warning
    }
}

int main() {
    seed = static_cast<unsigned int>(time(nullptr));
    printf("Property 3: PtrQueue Reset Frees All Items (seed=%u)\n", seed);

    RUN_TEST(test_reset_frees_all_items);
    RUN_TEST(test_reset_empty_queue_no_crash);
    RUN_TEST(test_partial_get_then_reset);

    TEST_SUMMARY();
}
