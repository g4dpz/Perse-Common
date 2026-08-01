#ifndef FREERTOS_MOCK_H
#define FREERTOS_MOCK_H

/**
 * Minimal FreeRTOS mock for host-based property tests.
 * Implements queue and semaphore primitives using a simple ring buffer.
 */

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>

// --- FreeRTOS type stubs ---
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef uint32_t UBaseType_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0
#define portMAX_DELAY 0xFFFFFFFF

// --- Queue mock (ring-buffer based) ---

struct MockQueue {
    uint8_t* buffer;
    size_t itemSize;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
};

typedef MockQueue* QueueHandle_t;
typedef QueueHandle_t SemaphoreHandle_t;

inline QueueHandle_t xQueueCreate(size_t count, size_t itemSize) {
    auto* q = new MockQueue();
    q->buffer = new uint8_t[count * itemSize];
    q->itemSize = itemSize;
    q->capacity = count;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    return q;
}

inline BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t /*timeout*/) {
    if (q->count >= q->capacity) return pdFALSE;
    memcpy(q->buffer + q->tail * q->itemSize, item, q->itemSize);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t /*timeout*/) {
    if (q->count == 0) return pdFALSE;
    memcpy(item, q->buffer + q->head * q->itemSize, q->itemSize);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return pdTRUE;
}

inline BaseType_t xQueueReset(QueueHandle_t q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    return pdTRUE;
}

inline void vQueueDelete(QueueHandle_t q) {
    if (q) {
        delete[] q->buffer;
        delete q;
    }
}

inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) {
    return static_cast<UBaseType_t>(q->count);
}

// --- Semaphore mock (uses a queue with 1 item of size 0) ---

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    // Represent mutex as a queue with capacity 1, item size 1
    auto* q = new MockQueue();
    q->buffer = new uint8_t[1];
    q->itemSize = 1;
    q->capacity = 1;
    q->head = 0;
    q->tail = 0;
    q->count = 0; // unlocked state
    return q;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t /*sem*/, TickType_t /*timeout*/) {
    // In single-threaded test, mutex always succeeds
    return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t /*sem*/) {
    // In single-threaded test, mutex always succeeds
    return pdTRUE;
}

inline void vSemaphoreDelete(SemaphoreHandle_t sem) {
    if (sem) {
        delete[] sem->buffer;
        delete sem;
    }
}

// --- Minimal test harness ---

#include <cstdio>

static int _test_pass_count = 0;
static int _test_fail_count = 0;

#define RUN_TEST(testFn) do { \
    printf("  Running %s...", #testFn); \
    testFn(); \
    printf(" PASSED\n"); \
    _test_pass_count++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        _test_fail_count++; \
        return; \
    } \
} while(0)

#define TEST_SUMMARY() do { \
    printf("\n%d passed, %d failed\n", _test_pass_count, _test_fail_count); \
    return _test_fail_count > 0 ? 1 : 0; \
} while(0)

#endif // FREERTOS_MOCK_H
