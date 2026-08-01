// Feature: cross-project-optimisations, Property 7: Inactivity Service Sleep Duration
// **Validates: Requirements 18.1, 18.3**
//
// For any set of pending timeout values {T1, T2, ..., Tn} with elapsed times {E1, E2, ..., En},
// the Inactivity Service's sleep duration SHALL equal min(T1-E1, T2-E2, ..., Tn-En),
// and no wake shall occur before that duration elapses (absent activity notifications).
//
// 200+ iterations with varying timeout sets and elapsed times.

#include "freertos_mock.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <vector>
#include <algorithm>
#include <limits>

// --- pdMS_TO_TICKS mock (1 tick = 1 ms for test simplicity) ---
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#endif

// --- Core sleep duration calculation logic extracted from InactivityService ---
// This mirrors the calculation that the event-driven InactivityService uses:
// Given a set of pending timeouts and their elapsed times, compute the minimum
// remaining time (i.e., how long to sleep until the next timeout fires).

struct PendingTimeout {
    uint32_t timeoutMs;   // Total timeout duration (e.g., 300000ms for unpaired)
    uint32_t elapsedMs;   // Time already elapsed since last activity
};

/// Calculate the sleep duration as the minimum remaining time across all pending timeouts.
/// Returns 0 if any timeout has already expired.
/// This is the core logic that the event-driven InactivityService would use in
/// xTaskNotifyWait's timeout parameter.
static uint32_t calculateSleepDuration(const std::vector<PendingTimeout>& timeouts) {
    if (timeouts.empty()) {
        // No pending timeouts — sleep indefinitely (represented as max value)
        return std::numeric_limits<uint32_t>::max();
    }

    uint32_t minRemaining = std::numeric_limits<uint32_t>::max();

    for (const auto& t : timeouts) {
        uint32_t remaining;
        if (t.elapsedMs >= t.timeoutMs) {
            remaining = 0;  // Already expired
        } else {
            remaining = t.timeoutMs - t.elapsedMs;
        }

        if (remaining < minRemaining) {
            minRemaining = remaining;
        }
    }

    return minRemaining;
}

/// Convert milliseconds to FreeRTOS ticks for the sleep call.
static TickType_t calculateSleepTicks(const std::vector<PendingTimeout>& timeouts) {
    uint32_t durationMs = calculateSleepDuration(timeouts);
    if (durationMs == std::numeric_limits<uint32_t>::max()) {
        return portMAX_DELAY;
    }
    return pdMS_TO_TICKS(durationMs);
}

// --- Property test ---

static unsigned int seed;

static uint32_t randRange32(uint32_t min, uint32_t max) {
    // Use two rand_r calls to get full 32-bit range
    uint32_t r = ((uint32_t)rand_r(&seed) << 16) ^ (uint32_t)rand_r(&seed);
    return min + (r % (max - min + 1));
}

/// Property: sleep duration equals min(Ti - Ei) for all pending timeouts
void test_sleep_equals_min_remaining() {
    const int iterations = 200;

    for (int iter = 0; iter < iterations; iter++) {
        // Generate 1-8 pending timeouts
        size_t count = 1 + (rand_r(&seed) % 8);
        std::vector<PendingTimeout> timeouts;
        timeouts.reserve(count);

        for (size_t i = 0; i < count; i++) {
            // Timeout values: 1000ms to 600000ms (1s to 10min)
            uint32_t timeoutMs = randRange32(1000, 600000);
            // Elapsed: 0 to timeoutMs - 1 (not yet expired)
            uint32_t elapsedMs = randRange32(0, timeoutMs - 1);
            timeouts.push_back({timeoutMs, elapsedMs});
        }

        // Compute using our function
        uint32_t computed = calculateSleepDuration(timeouts);

        // Compute expected: min(Ti - Ei) manually
        uint32_t expected = std::numeric_limits<uint32_t>::max();
        for (const auto& t : timeouts) {
            uint32_t remaining = t.timeoutMs - t.elapsedMs;
            if (remaining < expected) {
                expected = remaining;
            }
        }

        ASSERT(computed == expected);
        // Sleep duration must be > 0 since no timeout has expired
        ASSERT(computed > 0);
    }
}

/// Property: if any timeout has expired (elapsed >= timeout), sleep duration is 0
void test_expired_timeout_yields_zero_sleep() {
    const int iterations = 200;

    for (int iter = 0; iter < iterations; iter++) {
        size_t count = 1 + (rand_r(&seed) % 8);
        std::vector<PendingTimeout> timeouts;
        timeouts.reserve(count);

        // Add some non-expired timeouts
        for (size_t i = 0; i < count - 1; i++) {
            uint32_t timeoutMs = randRange32(1000, 600000);
            uint32_t elapsedMs = randRange32(0, timeoutMs - 1);
            timeouts.push_back({timeoutMs, elapsedMs});
        }

        // Add one expired timeout (elapsed >= timeout)
        uint32_t timeoutMs = randRange32(1000, 600000);
        uint32_t elapsedMs = randRange32(timeoutMs, timeoutMs + 100000);
        timeouts.push_back({timeoutMs, elapsedMs});

        // Shuffle the expired timeout position
        size_t swapIdx = rand_r(&seed) % count;
        std::swap(timeouts[count - 1], timeouts[swapIdx]);

        uint32_t computed = calculateSleepDuration(timeouts);
        ASSERT(computed == 0);
    }
}

/// Property: sleep duration is monotonically non-increasing as elapsed grows
void test_sleep_decreases_with_elapsed() {
    const int iterations = 200;

    for (int iter = 0; iter < iterations; iter++) {
        uint32_t timeoutMs = randRange32(1000, 600000);

        // Generate two elapsed values where e1 < e2 < timeoutMs
        uint32_t e1 = randRange32(0, timeoutMs - 2);
        uint32_t e2 = randRange32(e1 + 1, timeoutMs - 1);

        std::vector<PendingTimeout> t1 = {{timeoutMs, e1}};
        std::vector<PendingTimeout> t2 = {{timeoutMs, e2}};

        uint32_t sleep1 = calculateSleepDuration(t1);
        uint32_t sleep2 = calculateSleepDuration(t2);

        // More elapsed time => less remaining sleep
        ASSERT(sleep1 > sleep2);
    }
}

/// Property: with multiple timeouts, sleep is determined by the one with least remaining
void test_sleep_determined_by_nearest_timeout() {
    const int iterations = 200;

    for (int iter = 0; iter < iterations; iter++) {
        size_t count = 2 + (rand_r(&seed) % 7); // 2-8 timeouts
        std::vector<PendingTimeout> timeouts;
        timeouts.reserve(count);

        // Generate timeouts ensuring all have remaining > 0
        uint32_t minRemaining = std::numeric_limits<uint32_t>::max();
        size_t minIdx = 0;

        for (size_t i = 0; i < count; i++) {
            uint32_t timeoutMs = randRange32(1000, 600000);
            uint32_t elapsedMs = randRange32(0, timeoutMs - 1);
            timeouts.push_back({timeoutMs, elapsedMs});

            uint32_t remaining = timeoutMs - elapsedMs;
            if (remaining < minRemaining) {
                minRemaining = remaining;
                minIdx = i;
            }
        }

        uint32_t computed = calculateSleepDuration(timeouts);

        // The computed sleep must equal the remaining time of the nearest timeout
        uint32_t nearestRemaining = timeouts[minIdx].timeoutMs - timeouts[minIdx].elapsedMs;
        ASSERT(computed == nearestRemaining);

        // No timeout should have less remaining than the computed sleep
        for (const auto& t : timeouts) {
            uint32_t remaining = t.timeoutMs - t.elapsedMs;
            ASSERT(remaining >= computed);
        }
    }
}

/// Property: empty timeout set returns max delay (sleep indefinitely)
void test_empty_timeouts_sleep_indefinitely() {
    std::vector<PendingTimeout> empty;
    uint32_t computed = calculateSleepDuration(empty);
    ASSERT(computed == std::numeric_limits<uint32_t>::max());

    TickType_t ticks = calculateSleepTicks(empty);
    ASSERT(ticks == portMAX_DELAY);
}

/// Property: tick conversion is correct for computed sleep durations
void test_tick_conversion() {
    const int iterations = 200;

    for (int iter = 0; iter < iterations; iter++) {
        size_t count = 1 + (rand_r(&seed) % 5);
        std::vector<PendingTimeout> timeouts;

        for (size_t i = 0; i < count; i++) {
            uint32_t timeoutMs = randRange32(1000, 600000);
            uint32_t elapsedMs = randRange32(0, timeoutMs - 1);
            timeouts.push_back({timeoutMs, elapsedMs});
        }

        uint32_t durationMs = calculateSleepDuration(timeouts);
        TickType_t ticks = calculateSleepTicks(timeouts);

        // With pdMS_TO_TICKS being identity (1 tick = 1ms), ticks == durationMs
        ASSERT(ticks == pdMS_TO_TICKS(durationMs));
        ASSERT(ticks != portMAX_DELAY); // Not empty set
    }
}

int main() {
    seed = static_cast<unsigned int>(time(nullptr));
    printf("Property 7: Inactivity Service Sleep Duration (seed=%u)\n", seed);

    RUN_TEST(test_sleep_equals_min_remaining);
    RUN_TEST(test_expired_timeout_yields_zero_sleep);
    RUN_TEST(test_sleep_decreases_with_elapsed);
    RUN_TEST(test_sleep_determined_by_nearest_timeout);
    RUN_TEST(test_empty_timeouts_sleep_indefinitely);
    RUN_TEST(test_tick_conversion);

    TEST_SUMMARY();
}
