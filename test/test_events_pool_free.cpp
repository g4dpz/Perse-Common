// Feature: cross-project-optimisations, Property 2: Event Post to Full Queue Frees Memory
// **Validates: Requirements 6.1, 6.2**
//
// Create EventQueue with capacity 1, fill it, then call Events::post()
// Assert pool freeCount unchanged (allocated then freed back)
// 100+ iterations

#include "freertos_mock.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

// --- Stubs ---
#define ESP_LOGE(tag, ...) do {} while(0)
#define ESP_LOGW(tag, ...) do {} while(0)

// --- Inline EventPool ---
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

private:
    uint8_t* storage;
    void** freeList;
    size_t freeCount;
    size_t blockSize;
    size_t blockCount;
    SemaphoreHandle_t mutex;
};

// --- Inline Facility enum ---
enum class Facility : uint8_t {
    TestFacility = 0,
    COUNT
};

// Hash for Facility enum (must be before first use in unordered containers)
namespace std {
    template<> struct hash<Facility> {
        size_t operator()(Facility f) const { return static_cast<size_t>(f); }
    };
}

// --- Inline Events/EventQueue system ---
struct Event {
    Facility facility;
    void* data;
    size_t dataSize;
};

class EventQueue;

class Events {
public:
    static void init(size_t poolBlockSize, size_t poolBlockCount) {
        pool = new EventPool(poolBlockSize, poolBlockCount);
    }

    static void cleanup() {
        delete pool;
        pool = nullptr;
        queues.clear();
    }

    static void listen(Facility facility, EventQueue* queue) {
        std::lock_guard<std::mutex> lock(mut);
        queues[facility].insert(queue);
    }

    static void unlisten(EventQueue* queue) {
        std::lock_guard<std::mutex> lock(mut);
        for (auto& pair : queues) {
            pair.second.erase(queue);
        }
    }

    static bool post(Facility facility, const void* data, size_t size);

    static void free(Event& event) {
        if (event.data != nullptr) {
            pool->deallocate(event.data);
            event.data = nullptr;
        }
    }

    static EventPool* getPool() { return pool; }

private:
    static std::unordered_map<Facility, std::unordered_set<EventQueue*>> queues;
    static std::mutex mut;
    static EventPool* pool;
    friend class EventQueue;
};

std::unordered_map<Facility, std::unordered_set<EventQueue*>> Events::queues;
std::mutex Events::mut;
EventPool* Events::pool = nullptr;



class EventQueue {
public:
    explicit EventQueue(size_t count) {
        queue = xQueueCreate(count, sizeof(InternalEvent));
    }

    ~EventQueue() {
        vQueueDelete(queue);
    }

    bool get(Event& event, TickType_t timeout) {
        InternalEvent internal{};
        if (xQueueReceive(queue, &internal, timeout) != pdTRUE) return false;
        event = internal.evt;
        return true;
    }

    bool post(Facility facility, void* data, size_t dataSize) {
        InternalEvent event = {
            .evt = { .facility = facility, .data = data, .dataSize = dataSize },
            .killPill = false
        };
        if (xQueueSend(queue, &event, 0) != pdTRUE) {
            // Queue full: return block to pool
            if (data != nullptr) {
                Events::pool->deallocate(data);
            }
            return false;
        }
        return true;
    }

private:
    struct InternalEvent {
        Event evt;
        bool killPill;
    };
    QueueHandle_t queue;
    friend class Events;
};

bool Events::post(Facility facility, const void* data, size_t size) {
    std::unique_lock<std::mutex> lock(mut);
    auto it = queues.find(facility);
    if (it == queues.end()) return true;
    const std::unordered_set<EventQueue*> subs = it->second;
    lock.unlock();

    if (subs.empty()) return true;

    bool allOk = true;
    for (auto q : subs) {
        void* block = nullptr;
        if (size != 0) {
            block = pool->allocate();
            if (block == nullptr) {
                allOk = false;
                continue;
            }
            memcpy(block, data, size);
        }
        if (!q->post(facility, block, size)) {
            allOk = false;
        }
    }
    return allOk;
}

// --- Property test ---

static unsigned int seed;

static size_t randRange(size_t min, size_t max) {
    return min + (rand_r(&seed) % (max - min + 1));
}

void test_post_to_full_queue_frees_memory() {
    const int iterations = 200;

    for (int iter = 0; iter < iterations; iter++) {
        size_t blockSize = randRange(16, 64);
        size_t blockCount = randRange(4, 32);

        Events::init(blockSize, blockCount);

        // Create an EventQueue with capacity 1
        EventQueue eq(1);
        Events::listen(Facility::TestFacility, &eq);

        // Fill the queue with one event
        uint8_t dummyData[64] = {};
        size_t dataSize = randRange(1, blockSize);
        bool firstPost = Events::post(Facility::TestFacility, dummyData, dataSize);
        ASSERT(firstPost == true);

        // Record free count after first post (one block consumed)
        size_t freeCountBefore = Events::getPool()->getFreeCount();

        // Post again — queue is full, should allocate then free back
        bool secondPost = Events::post(Facility::TestFacility, dummyData, dataSize);
        ASSERT(secondPost == false);

        // Pool freeCount should be unchanged (alloc + dealloc happened)
        size_t freeCountAfter = Events::getPool()->getFreeCount();
        ASSERT(freeCountAfter == freeCountBefore);

        Events::unlisten(&eq);
        // Drain the queue to free the first event
        Event evt{};
        while (eq.get(evt, 0)) {
            Events::free(evt);
        }
        Events::cleanup();
    }
}

void test_post_with_no_subscribers_no_alloc() {
    const int iterations = 100;

    for (int iter = 0; iter < iterations; iter++) {
        size_t blockSize = randRange(16, 64);
        size_t blockCount = randRange(4, 32);

        Events::init(blockSize, blockCount);

        size_t freeCountBefore = Events::getPool()->getFreeCount();

        uint8_t dummyData[64] = {};
        bool result = Events::post(Facility::TestFacility, dummyData, randRange(1, blockSize));
        ASSERT(result == true);

        // No subscribers, so no allocation should happen
        size_t freeCountAfter = Events::getPool()->getFreeCount();
        ASSERT(freeCountAfter == freeCountBefore);

        Events::cleanup();
    }
}

int main() {
    seed = static_cast<unsigned int>(time(nullptr));
    printf("Property 2: Event Post to Full Queue Frees Memory (seed=%u)\n", seed);

    RUN_TEST(test_post_to_full_queue_frees_memory);
    RUN_TEST(test_post_with_no_subscribers_no_alloc);

    TEST_SUMMARY();
}
