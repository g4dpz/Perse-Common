/**
 * test_adcreader_scaling.cpp
 *
 * Tests ADCReader min/max percentage scaling edge cases.
 * Inlines the getValue() scaling logic with a mock ADC that returns preset values.
 *
 * Requirements: 14.1, 14.2, 14.3, 14.4
 *
 * Compile:
 *   g++ -std=c++17 -o test_adcreader_scaling test_adcreader_scaling.cpp && ./test_adcreader_scaling
 */

#include "freertos_mock.h"
#include <cmath>
#include <algorithm>

// ============================================================
// Inlined ADCReader scaling logic (extracted from ADCReader.cpp)
// ============================================================

struct ScalingConfig {
    float min;
    float max;
    float readingOffset;
    float readingFactor1;
    float readingFactor2;
};

/**
 * Mirrors ADCReader::getValue() logic exactly.
 * @param value The current EMA-filtered ADC value
 * @param cfg The scaling configuration (min, max, offset, factor1, factor2)
 * @return The scaled percentage or adjusted value
 */
float adcreader_getValue(float value, const ScalingConfig& cfg) {
    const float adjusted = cfg.readingOffset + value * cfg.readingFactor1
                         + std::pow(value, 2.0f) * cfg.readingFactor2;

    if(cfg.max == 0 && cfg.min == 0) {
        return adjusted;
    }

    float minimum = cfg.min;
    float maximum = cfg.max;

    if(cfg.min > cfg.max) {
        std::swap(minimum, maximum);
    }

    if(maximum == minimum) {
        return 0.0f;
    }

    float val = std::clamp(adjusted, minimum, maximum);
    val = (val - minimum) / (maximum - minimum);
    val = std::clamp(val * 100.0f, 0.0f, 100.0f);

    if(cfg.min > cfg.max) {
        val = 100.0f - val;
    }

    return val;
}

// ============================================================
// Tests
// ============================================================

/**
 * Requirement 14.1: WHEN min > max, THE ADCReader_Filter SHALL invert
 * the percentage output (100% becomes 0% and vice versa).
 *
 * With min=100, max=0: after swap, minimum=0, maximum=100.
 * For a value at 0 (bottom of range): percentage = 0%, then inversion → 100%.
 * For a value at 100 (top of range): percentage = 100%, then inversion → 0%.
 */
void test_min_greater_than_max_inverts_percentage() {
    // Config: min=100, max=0, identity transform (offset=0, factor1=1, factor2=0)
    ScalingConfig cfg = {100, 0, 0.0f, 1.0f, 0.0f};

    // Value at the low end of the actual range (0): should produce 100% (inverted)
    float result_low = adcreader_getValue(0.0f, cfg);
    ASSERT(std::abs(result_low - 100.0f) < 0.01f);

    // Value at the high end of the actual range (100): should produce 0% (inverted)
    float result_high = adcreader_getValue(100.0f, cfg);
    ASSERT(std::abs(result_high - 0.0f) < 0.01f);

    // Value at midpoint (50): should produce 50% (inversion of 50% is still 50%)
    float result_mid = adcreader_getValue(50.0f, cfg);
    ASSERT(std::abs(result_mid - 50.0f) < 0.01f);
}

/**
 * Requirement 14.2 & 14.4: WHEN min equals max and both are non-zero,
 * THE ADCReader_Filter SHALL produce a division by zero.
 *
 * BUG: Division by zero when min == max (non-zero)
 * EXPECTED (correct behavior): Return a sensible default (e.g., 0% or clamped value)
 * ACTUAL (current behavior): (maximum - minimum) == 0, causing division by zero
 *   which produces NaN or Inf depending on the numerator
 * ROOT CAUSE: No guard against min == max in getValue(). After the min > max check,
 *   if they are equal, the swap is a no-op, and the denominator becomes 0.
 */
void test_min_equals_max_nonzero_division_by_zero_bug() {
    // BUG: min == max (non-zero) causes division by zero
    // EXPECTED (correct behavior): Should return a safe default (e.g., 0 or 50)
    // ACTUAL (current behavior): Produces NaN or Inf
    // ROOT CAUSE: (maximum - minimum) is 0, no guard against this case

    ScalingConfig cfg = {50, 50, 0.0f, 1.0f, 0.0f};

    // FIXED: min == max now returns 0.0f instead of NaN/Inf
    float result = adcreader_getValue(50.0f, cfg);
    ASSERT(result == 0.0f);

    float result2 = adcreader_getValue(25.0f, cfg);
    ASSERT(result2 == 0.0f);
}

/**
 * Requirement 14.3: WHEN min and max are both 0, THE ADCReader_Filter
 * SHALL return the adjusted value directly without percentage scaling.
 */
void test_min_zero_max_zero_returns_adjusted_directly() {
    // Config: min=0, max=0, with identity transform
    ScalingConfig cfg = {0, 0, 0.0f, 1.0f, 0.0f};

    // Value=42 → adjusted = 0 + 42*1 + 42^2*0 = 42
    float result = adcreader_getValue(42.0f, cfg);
    ASSERT(std::abs(result - 42.0f) < 0.01f);

    // Value=0 → adjusted = 0
    float result_zero = adcreader_getValue(0.0f, cfg);
    ASSERT(std::abs(result_zero - 0.0f) < 0.01f);

    // With polynomial transform: offset=10, factor1=2, factor2=0.01
    // Value=100 → adjusted = 10 + 100*2 + 100^2*0.01 = 10 + 200 + 100 = 310
    ScalingConfig cfg2 = {0, 0, 10.0f, 2.0f, 0.01f};
    float result_poly = adcreader_getValue(100.0f, cfg2);
    ASSERT(std::abs(result_poly - 310.0f) < 0.1f);
}

// ============================================================
// Main
// ============================================================

int main() {
    printf("test_adcreader_scaling\n");

    RUN_TEST(test_min_greater_than_max_inverts_percentage);
    RUN_TEST(test_min_equals_max_nonzero_division_by_zero_bug);
    RUN_TEST(test_min_zero_max_zero_returns_adjusted_directly);

    TEST_SUMMARY();
}
