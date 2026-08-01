// Unit Test: CommData DriveDir Encoding/Decoding Edge Cases
// Requirements: 1.1, 1.2, 1.3, 1.4
//
// Tests boundary behavior of encodeDriveDir/decodeDriveDir:
// - Exact byte values at extremes (0x00, 0xFF)
// - Direction round-trip for all 8 directions
// - Speed quantization within ±1/62 bound

#include "freertos_mock.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <algorithm>

// --- Inline CommData implementation for host compilation ---

struct DriveDir {
    uint8_t dir = 0;   // 0-7
    float speed = 0.0f; // 0.0 - 1.0
};

namespace CommData {

uint8_t encodeDriveDir(DriveDir dir) {
    dir.dir = std::clamp(dir.dir, (uint8_t)0, (uint8_t)7);
    dir.speed = std::clamp(dir.speed, 0.0f, 1.0f);

    const auto speed = (uint8_t)std::round(dir.speed * 31.0f);

    return (speed << 3 | dir.dir);
}

DriveDir decodeDriveDir(uint8_t raw) {
    uint8_t speed = raw >> 3;
    uint8_t dir = raw & 0b111;

    return { dir, (float)speed / 31.0f };
}

} // namespace CommData

// --- Tests ---

void test_dir7_speed1_encodes_to_0xFF() {
    // Requirement 1.1: dir=7/speed=1.0 → 0xFF
    DriveDir input{7, 1.0f};
    uint8_t encoded = CommData::encodeDriveDir(input);
    ASSERT(encoded == 0xFF);
}

void test_dir0_speed0_encodes_to_0x00_and_roundtrips() {
    // Requirement 1.2: dir=0/speed=0.0 → 0x00, decode returns dir=0, speed=0.0 exactly
    DriveDir input{0, 0.0f};
    uint8_t encoded = CommData::encodeDriveDir(input);
    ASSERT(encoded == 0x00);

    DriveDir decoded = CommData::decodeDriveDir(encoded);
    ASSERT(decoded.dir == 0);
    ASSERT(decoded.speed == 0.0f);
}

void test_all_8_directions_roundtrip() {
    // Requirement 1.4: direction survives round-trip for all valid dirs
    for (uint8_t dir = 0; dir < 8; dir++) {
        // Test with several speed values to ensure dir is independent
        float speeds[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        for (float speed : speeds) {
            DriveDir input{dir, speed};
            uint8_t encoded = CommData::encodeDriveDir(input);
            DriveDir decoded = CommData::decodeDriveDir(encoded);
            ASSERT(decoded.dir == dir);
        }
    }
}

void test_speed_quantization_boundary() {
    // Requirement 1.3: decoded speed within ±1/62 of original for boundary speeds
    const float tolerance = 1.0f / 62.0f;
    float boundary_speeds[] = {0.0f, 0.5f, 1.0f};

    for (uint8_t dir = 0; dir < 8; dir++) {
        for (float speed : boundary_speeds) {
            DriveDir input{dir, speed};
            uint8_t encoded = CommData::encodeDriveDir(input);
            DriveDir decoded = CommData::decodeDriveDir(encoded);

            float error = std::fabs(decoded.speed - speed);
            ASSERT(error <= tolerance);
        }
    }
}

int main() {
    printf("CommData DriveDir Encoding/Decoding Edge Cases\n");

    RUN_TEST(test_dir7_speed1_encodes_to_0xFF);
    RUN_TEST(test_dir0_speed0_encodes_to_0x00_and_roundtrips);
    RUN_TEST(test_all_8_directions_roundtrip);
    RUN_TEST(test_speed_quantization_boundary);

    TEST_SUMMARY();
}
