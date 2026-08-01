/**
 * test_hysteresis_boundary.cpp
 *
 * Tests Hysteresis threshold boundary behavior:
 * - Value at exact threshold assigns to lower level (Req 11.1)
 * - Value within margin maintains current level / no oscillation (Req 11.2)
 * - Value beyond margin transitions to adjacent level (Req 11.3)
 * - Single threshold pair returns level 0 for values in range (Req 11.4)
 *
 * Compile: g++ -std=c++17 -o test_hysteresis_boundary test_hysteresis_boundary.cpp && ./test_hysteresis_boundary
 */

#include "freertos_mock.h"

// Include Hysteresis source inline
#include "../src/Hysteresis.cpp"

// --- Tests for Requirement 11.1 ---
// WHEN a value is exactly at a threshold boundary between two levels,
// THE Hysteresis_Controller SHALL assign the value to the lower level

void test_exact_threshold_assigns_lower_level() {
    // Thresholds: {0, 10, 20, 30} → levels 0 (0-10), 1 (10-20), 2 (20-30)
    Hysteresis h({0, 10, 20, 30}, 0); // margin=0 to test pure threshold behavior

    // Value exactly at threshold 10 (boundary between level 0 and level 1)
    // findLevel: i=0 checks val>=0 && val<=10 → true, returns level 0
    int level = h.reset(10);
    ASSERT(level == 0);

    // Value exactly at threshold 20 (boundary between level 1 and level 2)
    // findLevel: i=0 checks val>=0 && val<=20 → true... wait, no.
    // Actually i=0: val>=0 && val<=10 → 20<=10 is false
    // i=1: val>=10 && val<=20 → true, returns level 1
    level = h.reset(20);
    ASSERT(level == 1);

    // Value exactly at threshold 0 (lower bound of level 0)
    level = h.reset(0);
    ASSERT(level == 0);

    // Value exactly at threshold 30 (upper bound of level 2)
    level = h.reset(30);
    ASSERT(level == 2);
}

void test_exact_threshold_with_multiple_levels() {
    // Thresholds: {0, 4, 15, 30, 70, 100} → 5 levels (like Rover battery)
    Hysteresis h({0, 4, 15, 30, 70, 100}, 0);

    // Threshold 4: boundary between level 0 and level 1
    // findLevel: i=0 checks val>=0 && val<=4 → true, returns level 0
    int level = h.reset(4);
    ASSERT(level == 0);

    // Threshold 15: boundary between level 1 and level 2
    // findLevel: i=1 checks val>=4 && val<=15 → true, returns level 1
    level = h.reset(15);
    ASSERT(level == 1);

    // Threshold 30: boundary between level 2 and level 3
    level = h.reset(30);
    ASSERT(level == 2);

    // Threshold 70: boundary between level 3 and level 4
    level = h.reset(70);
    ASSERT(level == 3);
}

// --- Tests for Requirement 11.2 ---
// WHEN a value is within the margin of a threshold,
// THE Hysteresis_Controller SHALL maintain the current level (no oscillation)

void test_within_margin_maintains_level() {
    // Thresholds: {0, 10, 20, 30}, margin=3
    // Level 0: [0, 10], level 1: [10, 20], level 2: [20, 30]
    Hysteresis h({0, 10, 20, 30}, 3);

    // Start at level 1 (value=15, well within level 1)
    h.reset(15);
    ASSERT(h.get() == 1);

    // Move to value 11: still above lower bound (10 - 3 = 7), within upper margin
    // Level 1 bounds with margin: lb = Thresholds[1] - Margin = 10 - 3 = 7
    //                             ub = Thresholds[2] + Margin = 20 + 3 = 23
    // 11 is within [7, 23], so no transition
    h.update(11);
    ASSERT(h.get() == 1);

    // Move to value 9: still within [7, 23], no transition (hysteresis prevents it)
    h.update(9);
    ASSERT(h.get() == 1);

    // Move to value 8: still within [7, 23], no transition
    h.update(8);
    ASSERT(h.get() == 1);
}

void test_oscillation_at_threshold_prevented() {
    // Thresholds: {0, 10, 20, 30}, margin=3
    Hysteresis h({0, 10, 20, 30}, 3);

    // Start at level 1
    h.reset(15);
    ASSERT(h.get() == 1);

    // Oscillate around threshold 10 within margin: 9, 11, 9, 11
    // All values are within [7, 23] so level should stay at 1
    h.update(9);
    ASSERT(h.get() == 1);
    h.update(11);
    ASSERT(h.get() == 1);
    h.update(9);
    ASSERT(h.get() == 1);
    h.update(11);
    ASSERT(h.get() == 1);
}

// --- Tests for Requirement 11.3 ---
// WHEN a value exceeds the margin beyond a threshold,
// THE Hysteresis_Controller SHALL transition to the adjacent level

void test_beyond_margin_transitions_down() {
    // Thresholds: {0, 10, 20, 30}, margin=3
    Hysteresis h({0, 10, 20, 30}, 3);

    // Start at level 1
    h.reset(15);
    ASSERT(h.get() == 1);

    // Level 1 lower bound with margin: lb = Thresholds[1] - Margin = 10 - 3 = 7
    // Value 6 is below lb (6 < 7), should trigger transition
    h.update(6);
    ASSERT(h.get() == 0);
}

void test_beyond_margin_transitions_up() {
    // Thresholds: {0, 10, 20, 30}, margin=3
    Hysteresis h({0, 10, 20, 30}, 3);

    // Start at level 1
    h.reset(15);
    ASSERT(h.get() == 1);

    // Level 1 upper bound with margin: ub = Thresholds[2] + Margin = 20 + 3 = 23
    // Value 24 is above ub (24 > 23), should trigger transition
    h.update(24);
    ASSERT(h.get() == 2);
}

void test_large_jump_transitions_multiple_levels() {
    // Thresholds: {0, 10, 20, 30}, margin=3
    Hysteresis h({0, 10, 20, 30}, 3);

    // Start at level 0
    h.reset(5);
    ASSERT(h.get() == 0);

    // Jump to 25 (beyond level 0 upper bound 10+3=13), findLevel(25) = level 2
    h.update(25);
    ASSERT(h.get() == 2);
}

// --- Tests for Requirement 11.4 ---
// WHEN a single threshold pair defines one level,
// THE Hysteresis_Controller SHALL return level 0 for values within the range

void test_single_threshold_pair_returns_level_0() {
    // Single threshold pair: {0, 100} → 1 level (level 0)
    Hysteresis h({0, 100}, 5);

    // Value in range → level 0
    int level = h.reset(50);
    ASSERT(level == 0);

    // Value at lower threshold → level 0
    level = h.reset(0);
    ASSERT(level == 0);

    // Value at upper threshold → level 0
    level = h.reset(100);
    ASSERT(level == 0);

    // Update within range → level 0
    h.update(25);
    ASSERT(h.get() == 0);

    h.update(75);
    ASSERT(h.get() == 0);
}

void test_single_threshold_pair_stays_level_0_at_boundaries() {
    // Single threshold pair: {10, 90} → 1 level (level 0)
    Hysteresis h({10, 90}, 0);

    int level = h.reset(10);
    ASSERT(level == 0);

    level = h.reset(90);
    ASSERT(level == 0);

    level = h.reset(50);
    ASSERT(level == 0);
}

int main() {
    printf("=== Hysteresis Boundary Tests ===\n\n");

    // Requirement 11.1: Exact threshold assigns to lower level
    RUN_TEST(test_exact_threshold_assigns_lower_level);
    RUN_TEST(test_exact_threshold_with_multiple_levels);

    // Requirement 11.2: Within margin maintains current level
    RUN_TEST(test_within_margin_maintains_level);
    RUN_TEST(test_oscillation_at_threshold_prevented);

    // Requirement 11.3: Beyond margin transitions to adjacent level
    RUN_TEST(test_beyond_margin_transitions_down);
    RUN_TEST(test_beyond_margin_transitions_up);
    RUN_TEST(test_large_jump_transitions_multiple_levels);

    // Requirement 11.4: Single threshold pair returns level 0
    RUN_TEST(test_single_threshold_pair_returns_level_0);
    RUN_TEST(test_single_threshold_pair_stays_level_0_at_boundaries);

    TEST_SUMMARY();
}
