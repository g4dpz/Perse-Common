// Unit Test: ADCReader EMA Filter Edge Cases
// Requirements: 13.1, 13.2, 13.3
//
// Tests EMA filter behavior at parameter extremes:
// - ema_a=0: value freezes at first sample, ignores subsequent readings
// - ema_a=1: value tracks each new reading instantly
// - initial value (-1): first sample sets value directly without EMA formula

#include "freertos_mock.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>

// --- Stubs for ESP-IDF types and functions ---

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

typedef int adc_unit_t;
typedef int adc_channel_t;
typedef int gpio_num_t;

// Stub adc_oneshot_io_to_channel - not needed for test since we bypass constructor
inline esp_err_t adc_oneshot_io_to_channel(gpio_num_t pin, adc_unit_t* unit, adc_channel_t* chan) {
    *unit = 0;
    *chan = 0;
    return ESP_OK;
}

// --- Mock ADC class ---

class ADC {
public:
    explicit ADC(adc_unit_t unit) : unit(unit) {}
    virtual ~ADC() = default;

    adc_unit_t getUnit() const { return unit; }

    esp_err_t read(adc_channel_t chan, int& valueOut) {
        if (readIndex >= presetValues.size()) {
            return ESP_FAIL;
        }
        valueOut = presetValues[readIndex++];
        return ESP_OK;
    }

    void setPresetValues(const std::vector<int>& values) {
        presetValues = values;
        readIndex = 0;
    }

private:
    adc_unit_t unit;
    std::vector<int> presetValues;
    size_t readIndex = 0;
};

// --- Inline ADCReader implementation ---

class ADCReader {
public:
    ADCReader(ADC& adc, gpio_num_t pin, float ema_a = 1, int min = 0, int max = 0,
              float readingOffset = 0.0f, float readingFactor1 = 1.0f, float readingFactor2 = 0.0f)
        : adc(adc), emaA(ema_a), min(min), max(max),
          readingOffset(readingOffset), readingFactor1(readingFactor1), readingFactor2(readingFactor2) {
        adc_unit_t unit;
        adc_oneshot_io_to_channel(pin, &unit, &chan);
    }

    float sample() {
        int raw = 0;
        if (adc.read(chan, raw) != ESP_OK) {
            return getValue();
        }

        if (value == -1 || emaA == 1) {
            value = raw;
        } else {
            value = value * (1.0f - emaA) + emaA * raw;
        }

        return getValue();
    }

    float getValue() const {
        const float adjusted = readingOffset + value * readingFactor1 + std::pow(value, 2.0f) * readingFactor2;

        if (max == 0 && min == 0) {
            return adjusted;
        }

        float minimum = min;
        float maximum = max;

        if (min > max) {
            std::swap(minimum, maximum);
        }

        float val = std::clamp(adjusted, minimum, maximum);
        val = (val - minimum) / (maximum - minimum);
        val = std::clamp(val * 100.0f, 0.0f, 100.0f);

        if (min > max) {
            val = 100.0f - val;
        }

        return val;
    }

    void resetEma() {
        value = -1;
        sample();
    }

    void setEmaA(float ema_a) {
        emaA = ema_a;
    }

    // Expose internal value for test verification
    float getRawValue() const { return value; }

private:
    ADC& adc;
    adc_channel_t chan;

    float emaA;
    const float min;
    const float max;
    const float readingOffset;
    const float readingFactor1;
    const float readingFactor2;

    float value = -1.0f;
};

// --- Tests ---

void test_ema_a_zero_freezes_at_first_sample() {
    // Requirement 13.1: When ema_a is 0, value freezes at first sample and ignores subsequent
    ADC mockAdc(0);
    mockAdc.setPresetValues({500, 1000, 2000, 3000});

    // ema_a=0, min/max=0 means no percentage scaling, returns adjusted value directly
    ADCReader reader(mockAdc, 0, 0.0f, 0, 0);

    // First sample: value == -1, so sets value directly to raw (500)
    float result = reader.sample();
    ASSERT(result == 500.0f);

    // Subsequent samples: ema_a=0 means value = value*(1-0) + 0*raw = value (unchanged)
    result = reader.sample();
    ASSERT(result == 500.0f);

    result = reader.sample();
    ASSERT(result == 500.0f);

    result = reader.sample();
    ASSERT(result == 500.0f);
}

void test_ema_a_zero_ignores_large_changes() {
    // Requirement 13.1: Value stays frozen even with drastically different readings
    ADC mockAdc(0);
    mockAdc.setPresetValues({100, 4095, 0, 2048});

    ADCReader reader(mockAdc, 0, 0.0f, 0, 0);

    reader.sample(); // Sets to 100 (initial)
    ASSERT(reader.getRawValue() == 100.0f);

    reader.sample(); // Should ignore 4095
    ASSERT(reader.getRawValue() == 100.0f);

    reader.sample(); // Should ignore 0
    ASSERT(reader.getRawValue() == 100.0f);

    reader.sample(); // Should ignore 2048
    ASSERT(reader.getRawValue() == 100.0f);
}

void test_ema_a_one_tracks_instantly() {
    // Requirement 13.2: When ema_a is 1, value tracks each new reading instantly
    ADC mockAdc(0);
    mockAdc.setPresetValues({100, 500, 1000, 0, 4095});

    ADCReader reader(mockAdc, 0, 1.0f, 0, 0);

    float result = reader.sample();
    ASSERT(result == 100.0f);

    result = reader.sample();
    ASSERT(result == 500.0f);

    result = reader.sample();
    ASSERT(result == 1000.0f);

    result = reader.sample();
    ASSERT(result == 0.0f);

    result = reader.sample();
    ASSERT(result == 4095.0f);
}

void test_ema_a_one_no_smoothing() {
    // Requirement 13.2: Each sample completely replaces the previous value
    ADC mockAdc(0);
    mockAdc.setPresetValues({1000, 2000});

    ADCReader reader(mockAdc, 0, 1.0f, 0, 0);

    reader.sample(); // value = 1000
    ASSERT(reader.getRawValue() == 1000.0f);

    reader.sample(); // value = 2000 (no trace of 1000 remaining)
    ASSERT(reader.getRawValue() == 2000.0f);
}

void test_initial_value_minus_one_first_sample_sets_directly() {
    // Requirement 13.3: When value == -1 (initial state), first sample sets value
    // directly without applying the EMA formula
    ADC mockAdc(0);
    mockAdc.setPresetValues({750, 250});

    // Use ema_a=0.5 to ensure EMA would normally smooth the value
    ADCReader reader(mockAdc, 0, 0.5f, 0, 0);

    // First sample: value is -1, so it should set directly to 750 (not apply EMA)
    // If EMA were applied: value = -1 * (1-0.5) + 0.5 * 750 = -0.5 + 375 = 374.5 (wrong)
    // Correct: value = 750 (set directly)
    float result = reader.sample();
    ASSERT(result == 750.0f);
    ASSERT(reader.getRawValue() == 750.0f);

    // Second sample: EMA is now applied normally
    // value = 750 * (1-0.5) + 0.5 * 250 = 375 + 125 = 500
    result = reader.sample();
    ASSERT(result == 500.0f);
    ASSERT(reader.getRawValue() == 500.0f);
}

void test_initial_value_ema_applied_after_first() {
    // Requirement 13.3: After first sample sets value, subsequent samples use EMA
    ADC mockAdc(0);
    mockAdc.setPresetValues({1000, 1000, 1000});

    // ema_a=0.25 - slow filter
    ADCReader reader(mockAdc, 0, 0.25f, 0, 0);

    // First sample sets directly
    reader.sample();
    ASSERT(reader.getRawValue() == 1000.0f);

    // Second sample: value = 1000*(1-0.25) + 0.25*1000 = 750 + 250 = 1000
    // (Same input, so no change)
    reader.sample();
    ASSERT(reader.getRawValue() == 1000.0f);
}

void test_initial_value_with_different_first_reading() {
    // Requirement 13.3: Verify EMA formula is correctly applied after initialization
    ADC mockAdc(0);
    mockAdc.setPresetValues({1000, 2000});

    // ema_a=0.25
    ADCReader reader(mockAdc, 0, 0.25f, 0, 0);

    // First sample: sets directly to 1000
    reader.sample();
    ASSERT(reader.getRawValue() == 1000.0f);

    // Second sample: EMA applied
    // value = 1000 * (1-0.25) + 0.25 * 2000 = 750 + 500 = 1250
    reader.sample();
    ASSERT(reader.getRawValue() == 1250.0f);
}

// --- Main ---

int main() {
    printf("=== ADCReader EMA Filter Edge Cases ===\n\n");

    RUN_TEST(test_ema_a_zero_freezes_at_first_sample);
    RUN_TEST(test_ema_a_zero_ignores_large_changes);
    RUN_TEST(test_ema_a_one_tracks_instantly);
    RUN_TEST(test_ema_a_one_no_smoothing);
    RUN_TEST(test_initial_value_minus_one_first_sample_sets_directly);
    RUN_TEST(test_initial_value_ema_applied_after_first);
    RUN_TEST(test_initial_value_with_different_first_reading);

    TEST_SUMMARY();
}
