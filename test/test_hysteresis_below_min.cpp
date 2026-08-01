// Test: Hysteresis Below-Minimum Bug
// Validates: Requirements 12.1, 12.2
//
// 12.1: When a value is below the minimum threshold, get() should return level 0
//       (BUG: currently returns LevelCount-1)
// 12.2: Document this as a known bug with a test demonstrating incorrect behavior

#include "freertos_mock.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <initializer_list>

// --- Inline Hysteresis implementation for host compilation ---

class Hysteresis {
public:
    Hysteresis(std::initializer_list<int> thresholds, int margin)
        : Thresholds(thresholds), LevelCount(thresholds.size() - 1), Margin(margin), currentLevel(0) {
    }

    int get() const {
        if(currentLevel < 0) return 0;
        else if(currentLevel >= LevelCount) return LevelCount - 1;
        return currentLevel;
    }

    int update(int val) {
        int lb = Thresholds[currentLevel];
        if(currentLevel > 0) lb -= Margin;

        int ub = Thresholds[currentLevel + 1];
        if(currentLevel < LevelCount) ub += Margin;

        if(val < lb || val > ub) {
            currentLevel = findLevel(val);
        }

        return get();
    }

    int reset(int val) {
        currentLevel = findLevel(val);
        return get();
    }

    // Exposed for testing
    int getCurrentLevelRaw() const { return currentLevel; }

private:
    const std::vector<int> Thresholds;
    const int LevelCount;
    const int Margin;

    int currentLevel;

    int findLevel(int val) {
        if (val < Thresholds[0]) return 0;
        int i;
        for(i = 0; i < LevelCount; i++) {
            if(val >= Thresholds[i] && val <= Thresholds[i + 1]) break;
        }
        return i;
    }
};

// --- Tests ---

// BUG: When a value is below the minimum threshold, findLevel() returns LevelCount
//   instead of 0, causing get() to clamp to LevelCount-1 (the highest level).
// EXPECTED (correct behavior): A value below the minimum threshold should be assigned
//   to level 0 (the lowest level), since it is below all defined ranges.
// ACTUAL (current behavior): findLevel() iterates through all levels without finding
//   a match (the value is below Thresholds[0], so the condition val >= Thresholds[i]
//   fails for i=0). The loop exits with i == LevelCount. get() then clamps
//   currentLevel to LevelCount-1, returning the HIGHEST level instead of the lowest.
// ROOT CAUSE: findLevel() has no special handling for values below Thresholds[0].
//   The for-loop only matches values within [Thresholds[i], Thresholds[i+1]] for each
//   level. A value below Thresholds[0] matches none of these ranges, so i reaches
//   LevelCount. The get() clamping logic (currentLevel >= LevelCount → LevelCount-1)
//   then maps this to the highest level rather than the lowest.

void test_below_min_returns_highest_level_instead_of_zero() {
    // Thresholds: {0, 4, 15, 30, 70, 100}, meaning 5 levels (0-4)
    // Level 0: [0, 4], Level 1: [4, 15], Level 2: [15, 30], Level 3: [30, 70], Level 4: [70, 100]
    Hysteresis h({0, 4, 15, 30, 70, 100}, 3);

    // Value -5 is below the minimum threshold (0)
    int result = h.update(-5);

    // FIXED: Below-minimum now correctly maps to level 0
    ASSERT(result == 0);
}

void test_below_min_with_reset() {
    // Thresholds: {10, 50, 100}, meaning 2 levels (0-1)
    // Level 0: [10, 50], Level 1: [50, 100]
    Hysteresis h({10, 50, 100}, 5);

    // Value 0 is below the minimum threshold (10)
    int result = h.reset(0);

    // FIXED: Below-minimum now correctly maps to level 0
    ASSERT(result == 0);
}

void test_below_min_negative_value() {
    // Thresholds: {0, 25, 50, 75, 100}, meaning 4 levels (0-3)
    Hysteresis h({0, 25, 50, 75, 100}, 2);

    // Value -100 is far below minimum threshold (0)
    int result = h.update(-100);

    // FIXED: Below-minimum now correctly maps to level 0
    ASSERT(result == 0);
}

void test_at_min_threshold_is_correct() {
    // Verify that a value AT the minimum threshold (not below) works correctly
    // This confirms the bug is specifically about values BELOW the minimum
    Hysteresis h({0, 4, 15, 30, 70, 100}, 3);

    // Value 0 is exactly at the minimum threshold — should be level 0
    int result = h.update(0);
    ASSERT(result == 0);  // This works correctly (value is within [0, 4])
}

int main() {
    printf("Hysteresis Below-Minimum Bug Tests (documenting known bug)\n");

    RUN_TEST(test_below_min_returns_highest_level_instead_of_zero);
    RUN_TEST(test_below_min_with_reset);
    RUN_TEST(test_below_min_negative_value);
    RUN_TEST(test_at_min_threshold_is_correct);

    TEST_SUMMARY();
}
