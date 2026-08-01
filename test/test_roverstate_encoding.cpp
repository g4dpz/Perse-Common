// Task 1.3: RoverState Encoding Round-Trip Tests
// Validates: Requirements 16.1, 16.2, 16.3, 16.4, 16.5
//
// Tests that encodeRoverState/decodeRoverState correctly pack a 7-bit value
// and a local flag into a single byte and recover them losslessly for all
// valid inputs (0–127). Also documents the lossy masking for values > 127.

#include "freertos_mock.h"
#include <cstdio>
#include <cstdint>

// --- Inline RoverStateUtil implementation for host compilation ---
#include "../src/RoverStateUtil.cpp"

// --- Tests ---

void test_all_values_local_true_roundtrip() {
    // Requirement 16.1: FOR ALL values 0–127 with local=true,
    // encoding then decoding SHALL reproduce the original value and return local=true
    for (uint8_t val = 0; val < 128; val++) {
        uint8_t encoded = encodeRoverState(val, true);
        uint8_t decoded_val = 0;
        bool decoded_local = decodeRoverState(encoded, decoded_val);

        ASSERT(decoded_val == val);
        ASSERT(decoded_local == true);
    }
}

void test_all_values_local_false_roundtrip() {
    // Requirement 16.2: FOR ALL values 0–127 with local=false,
    // encoding then decoding SHALL reproduce the original value and return local=false
    for (uint8_t val = 0; val < 128; val++) {
        uint8_t encoded = encodeRoverState(val, false);
        uint8_t decoded_val = 0;
        bool decoded_local = decodeRoverState(encoded, decoded_val);

        ASSERT(decoded_val == val);
        ASSERT(decoded_local == false);
    }
}

void test_value0_local_false_produces_0x00() {
    // Requirement 16.4: WHEN value is 0 and local is false,
    // THE RoverState_Encoder SHALL produce byte 0x00
    uint8_t encoded = encodeRoverState(0, false);
    ASSERT(encoded == 0x00);

    // Also verify decode
    uint8_t decoded_val = 0xFF;
    bool decoded_local = decodeRoverState(encoded, decoded_val);
    ASSERT(decoded_val == 0);
    ASSERT(decoded_local == false);
}

void test_value127_local_true_produces_0xFF() {
    // Requirement 16.5: WHEN value is 127 and local is true,
    // THE RoverState_Encoder SHALL produce byte 0xFF
    uint8_t encoded = encodeRoverState(127, true);
    ASSERT(encoded == 0xFF);

    // Also verify decode
    uint8_t decoded_val = 0;
    bool decoded_local = decodeRoverState(encoded, decoded_val);
    ASSERT(decoded_val == 127);
    ASSERT(decoded_local == true);
}

void test_value_exceeding_127_masked_to_7_bits() {
    // Requirement 16.3: WHEN a value exceeds 127 (bit 7 set),
    // THE RoverState_Encoder SHALL mask it to 7 bits (lossy, intentional behavior)
    //
    // This documents the lossy behavior: the high bit of the value is discarded
    // because bit 7 is reserved for the local flag.

    // 128 (0x80) -> masked to 0 (0x80 & 0x7F = 0x00)
    uint8_t encoded = encodeRoverState(128, false);
    uint8_t decoded_val = 0xFF;
    bool decoded_local = decodeRoverState(encoded, decoded_val);
    ASSERT(decoded_val == 0);  // 128 & 0x7F = 0
    ASSERT(decoded_local == false);

    // 129 (0x81) -> masked to 1 (0x81 & 0x7F = 0x01)
    encoded = encodeRoverState(129, true);
    decoded_val = 0;
    decoded_local = decodeRoverState(encoded, decoded_val);
    ASSERT(decoded_val == 1);  // 129 & 0x7F = 1
    ASSERT(decoded_local == true);

    // 255 (0xFF) -> masked to 127 (0xFF & 0x7F = 0x7F)
    encoded = encodeRoverState(255, false);
    decoded_val = 0;
    decoded_local = decodeRoverState(encoded, decoded_val);
    ASSERT(decoded_val == 127);  // 255 & 0x7F = 127
    ASSERT(decoded_local == false);

    // 255 with local=true -> byte should be 0xFF (0x7F | 0x80)
    encoded = encodeRoverState(255, true);
    ASSERT(encoded == 0xFF);
    decoded_val = 0;
    decoded_local = decodeRoverState(encoded, decoded_val);
    ASSERT(decoded_val == 127);
    ASSERT(decoded_local == true);

    // 200 (0xC8) -> masked to 72 (0xC8 & 0x7F = 0x48)
    encoded = encodeRoverState(200, false);
    decoded_val = 0;
    decoded_local = decodeRoverState(encoded, decoded_val);
    ASSERT(decoded_val == 72);  // 200 & 0x7F = 72
    ASSERT(decoded_local == false);
}

int main() {
    printf("RoverState Encoding Round-Trip Tests\n");
    printf("Validates: Requirements 16.1, 16.2, 16.3, 16.4, 16.5\n\n");

    RUN_TEST(test_all_values_local_true_roundtrip);
    RUN_TEST(test_all_values_local_false_roundtrip);
    RUN_TEST(test_value0_local_false_produces_0x00);
    RUN_TEST(test_value127_local_true_produces_0xFF);
    RUN_TEST(test_value_exceeding_127_masked_to_7_bits);

    TEST_SUMMARY();
}
