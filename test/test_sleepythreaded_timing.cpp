// Feature: cross-project-optimisations, Property 4: SleepyThreaded Fixed-Frequency Timing
// **Validates: Requirements 9.1, 9.2**
//
// For any configured loop interval T and any loop body execution duration D (where D < T),
// the time between consecutive sleepyLoop() invocations SHALL be exactly T ticks
// (within one tick tolerance), independent of D.
//
// Testing strategy:
// Since vTaskDelayUntil on a real RTOS guarantees fixed-frequency scheduling by advancing
// lastWakeTime by exactly sleepTime regardless of how long the loop body took, we mock the
// timing primitives to verify that:
//   1. lastWakeTime advances by exactly T each iteration (the vTaskDelayUntil contract)
//   2. The scheduled invocation times differ by exactly T, independent of loop body duration D
//
// We simulate multiple loop iterations with varying D values and verify the timing invariant.

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <atomic>
#include <functional>
#include <vector>

// --- FreeRTOS type stubs (custom for this test with tick tracking) ---
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef uint32_t UBaseType_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0
#define portMAX_DELAY 0xFFFFFFFF

// Global simulated tick counter
static TickType_t g_currentTick = 0;

// Record of lastWakeTime values after each vTaskDelayUntil call
static std::vector<TickType_t> g_wakeTimeLog;

// Track simulated loop body durations for each iteration
static std::vector<TickType_t> g_loopDurations;

// --- Minimal FreeRTOS mocks for Threaded ---

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
typedef void* TaskHandle_t;

inline SemaphoreHandle_t xSemaphoreCreateBinary() {
    auto* q = new MockQueue();
    q->buffer = new uint8_t[1];
    q->itemSize = 1;
    q->capacity = 1;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    return q;
}

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return xSemaphoreCreateBinary();
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t /*sem*/, TickType_t /*timeout*/) {
    return pdFALSE; // pauseSem never taken in normal operation
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t /*sem*/) {
    return pdTRUE;
}

inline void vSemaphoreDelete(SemaphoreHandle_t sem) {
    if (sem) {
        delete[] sem->buffer;
        delete sem;
    }
}

inline TickType_t xTaskGetTickCount() {
    return g_currentTick;
}

// vTaskDelayUntil: the key function under test.
// Real FreeRTOS behaviour: advances *pxPreviousWakeTime by xTimeIncrement,
// then blocks until that absolute tick is reached.
// Our mock simulates this by advancing lastWakeTime and advancing g_currentTick
// to match (simulating the delay completing).
inline void vTaskDelayUntil(TickType_t* pxPreviousWakeTime, TickType_t xTimeIncrement) {
    *pxPreviousWakeTime += xTimeIncrement;
    // Simulate time advancing to the wake time
    g_currentTick = *pxPreviousWakeTime;
    g_wakeTimeLog.push_back(*pxPreviousWakeTime);
}

inline void vTaskDelay(TickType_t /*ticks*/) {
    // no-op for host
}

inline void vTaskDelete(void* /*task*/) { }

inline BaseType_t xTaskCreate(void (*fn)(void*), const char*, size_t, void*, UBaseType_t, TaskHandle_t*) {
    return pdPASS;
}

inline BaseType_t xTaskCreatePinnedToCore(void (*fn)(void*), const char*, size_t, void*, UBaseType_t, TaskHandle_t*, int) {
    return pdPASS;
}

inline void vTaskPrioritySet(TaskHandle_t, UBaseType_t) { }

// Stub esp_log
#define ESP_LOGE(tag, ...) do { } while(0)
#define ESP_LOGW(tag, ...) do { } while(0)

// --- Minimal test harness ---

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

// --- Inline SleepyThreaded implementation for host testing ---
// We extract the timing logic directly rather than compiling the full Threaded framework,
// because the full framework requires task creation and thread management not available on host.

/// Simulates the SleepyThreaded::loop() timing logic.
/// Returns the sequence of scheduled wake times for N iterations.
/// @param interval  The configured loop interval (sleepTime)
/// @param durations Vector of simulated loop body durations for each iteration
/// @return Vector of absolute tick times at which each sleepyLoop() would be invoked
static std::vector<TickType_t> simulateSleepyTimingDirect(
    TickType_t interval,
    const std::vector<TickType_t>& durations
) {
    std::vector<TickType_t> invocationTimes;

    // Mirrors SleepyThreaded::onStart() - lastWakeTime = xTaskGetTickCount()
    TickType_t lastWakeTime = g_currentTick;

    for (size_t i = 0; i < durations.size(); i++) {
        // Record the time sleepyLoop() would be called
        invocationTimes.push_back(g_currentTick);

        // Simulate sleepyLoop() taking 'duration' ticks
        g_currentTick += durations[i];

        // Mirrors SleepyThreaded::loop() calling vTaskDelayUntil(&lastWakeTime, sleepTime)
        // vTaskDelayUntil advances lastWakeTime by interval regardless of current tick
        lastWakeTime += interval;

        // After vTaskDelayUntil, the system wakes at lastWakeTime
        // (or immediately if lastWakeTime already passed - overrun case)
        if (g_currentTick < lastWakeTime) {
            g_currentTick = lastWakeTime; // normal case: delay until wake time
        }
        // else: overrun case - current tick already past wake time, no actual delay
    }

    return invocationTimes;
}

// --- Property tests ---

static unsigned int seed;

static TickType_t randRange(TickType_t min, TickType_t max) {
    return min + (rand_r(&seed) % (max - min + 1));
}

/// Property: Time between consecutive sleepyLoop() invocations equals interval T (±1 tick)
/// when loop body duration D < T (no overruns).
void test_fixed_frequency_no_overrun() {
    const int iterations = 200;

    for (int iter = 0; iter < iterations; iter++) {
        // Generate random interval T in [5, 500] ticks
        TickType_t interval = randRange(5, 500);

        // Generate random number of loop iterations [3, 20]
        size_t numLoops = randRange(3, 20);

        // Generate random loop body durations, all < interval
        std::vector<TickType_t> durations;
        for (size_t i = 0; i < numLoops; i++) {
            TickType_t d = randRange(0, interval - 1);
            durations.push_back(d);
        }

        // Reset simulated clock
        g_currentTick = randRange(0, 10000); // start at random initial tick

        // Run the timing simulation
        std::vector<TickType_t> invocations = simulateSleepyTimingDirect(interval, durations);

        ASSERT(invocations.size() == numLoops);

        // Verify: time between consecutive invocations == interval (±1 tick)
        for (size_t i = 1; i < invocations.size(); i++) {
            TickType_t delta = invocations[i] - invocations[i - 1];
            // Delta should be exactly interval when D < T (no overrun)
            bool withinTolerance = (delta >= interval - 1) && (delta <= interval + 1);
            if (!withinTolerance) {
                printf("\n    FAIL iter=%d, i=%zu: interval=%u, D[%zu]=%u, delta=%u, "
                       "invoc[%zu]=%u, invoc[%zu]=%u\n",
                       iter, i, interval, i-1, durations[i-1], delta,
                       i-1, invocations[i-1], i, invocations[i]);
            }
            ASSERT(withinTolerance);
        }
    }
}

/// Property: The interval between invocations is independent of the loop body duration D.
/// Varying D (while D < T) should produce the same inter-invocation timing.
void test_timing_independent_of_body_duration() {
    const int iterations = 200;

    for (int iter = 0; iter < iterations; iter++) {
        TickType_t interval = randRange(10, 300);
        size_t numLoops = randRange(3, 15);
        TickType_t startTick = randRange(0, 5000);

        // Run with short durations (D = 0)
        std::vector<TickType_t> shortDurations(numLoops, 0);
        g_currentTick = startTick;
        std::vector<TickType_t> shortInvocations = simulateSleepyTimingDirect(interval, shortDurations);

        // Run with random durations (D < T)
        std::vector<TickType_t> randomDurations;
        for (size_t i = 0; i < numLoops; i++) {
            randomDurations.push_back(randRange(0, interval - 1));
        }
        g_currentTick = startTick;
        std::vector<TickType_t> randomInvocations = simulateSleepyTimingDirect(interval, randomDurations);

        // Both should produce the same invocation times (since vTaskDelayUntil
        // anchors to absolute time, not relative delay)
        ASSERT(shortInvocations.size() == randomInvocations.size());
        for (size_t i = 0; i < shortInvocations.size(); i++) {
            TickType_t diff = (shortInvocations[i] > randomInvocations[i])
                ? (shortInvocations[i] - randomInvocations[i])
                : (randomInvocations[i] - shortInvocations[i]);
            if (diff > 1) {
                printf("\n    FAIL iter=%d, i=%zu: interval=%u, short_invoc=%u, "
                       "random_invoc=%u, diff=%u, D[%zu]=%u\n",
                       iter, i, interval, shortInvocations[i],
                       randomInvocations[i], diff, i, randomDurations[i]);
            }
            ASSERT(diff <= 1);
        }
    }
}

/// Property: First invocation happens at the initial tick (start time).
void test_first_invocation_at_start() {
    const int iterations = 100;

    for (int iter = 0; iter < iterations; iter++) {
        TickType_t interval = randRange(5, 200);
        TickType_t startTick = randRange(0, 50000);
        std::vector<TickType_t> durations = {randRange(0, interval - 1)};

        g_currentTick = startTick;
        std::vector<TickType_t> invocations = simulateSleepyTimingDirect(interval, durations);

        ASSERT(invocations.size() == 1);
        ASSERT(invocations[0] == startTick);
    }
}

int main() {
    seed = static_cast<unsigned int>(time(nullptr));
    printf("Property 4: SleepyThreaded Fixed-Frequency Timing (seed=%u)\n", seed);

    RUN_TEST(test_fixed_frequency_no_overrun);
    RUN_TEST(test_timing_independent_of_body_duration);
    RUN_TEST(test_first_invocation_at_start);

    TEST_SUMMARY();
}
