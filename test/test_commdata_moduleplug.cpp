// Test: CommData ModulePlug Encoding Round-Trip
// Validates: Requirements 2.1, 2.2
//
// Exhaustively tests all ModulePlugData combinations (type 0-8, bus 0-1, insert 0-1)
// verifying that encode/decode round-trips exactly preserve type, bus, and insert.
// Also verifies the specific bit pattern for insert=true, bus=Right, type=Unknown.

#include "freertos_mock.h"
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <cmath>

// --- Inline CommData implementation for host compilation ---

enum class ModuleType : uint8_t {
    TempHum, Gyro, AltPress, LED, RGB, PhotoRes, Motion, CO2, Unknown
};
enum class ModuleBus : uint8_t {
    Left = 0, Right = 1
};

struct ModulePlugData {
    ModuleType type;
    ModuleBus bus;
    bool insert;
};

namespace CommData {

uint8_t encodeModulePlug(ModulePlugData plugData) {
    uint8_t data = (plugData.insert << 7) | ((uint8_t)plugData.bus << 6) | (uint8_t)plugData.type;
    return data;
}

ModulePlugData decodeModulePlug(uint8_t raw) {
    ModulePlugData data{};
    data.insert = raw >> 7;
    data.bus = (ModuleBus)((raw >> 6) & 0b1);
    data.type = (ModuleType)(raw & 0b111111);
    return data;
}

} // namespace CommData

// --- Tests ---

// Requirement 2.1: FOR ALL ModulePlugData combinations (type 0-8, bus 0-1, insert 0-1),
// decoding the encoded byte SHALL reproduce the original type, bus, and insert values exactly.
void test_exhaustive_round_trip() {
    int tested = 0;
    for (uint8_t t = 0; t <= 8; t++) {
        for (uint8_t b = 0; b <= 1; b++) {
            for (uint8_t ins = 0; ins <= 1; ins++) {
                ModulePlugData original;
                original.type = (ModuleType)t;
                original.bus = (ModuleBus)b;
                original.insert = (bool)ins;

                uint8_t encoded = CommData::encodeModulePlug(original);
                ModulePlugData decoded = CommData::decodeModulePlug(encoded);

                ASSERT((uint8_t)decoded.type == t);
                ASSERT((uint8_t)decoded.bus == b);
                ASSERT(decoded.insert == (bool)ins);

                tested++;
            }
        }
    }
    // 9 types * 2 buses * 2 inserts = 36 combinations
    ASSERT(tested == 36);
}

// Requirement 2.2: WHEN insert=true, bus=Right, type=Unknown are encoded,
// THE CommData_Encoder SHALL set bit 7, bit 6, and the lower 6 bits to the Unknown enum value.
void test_insert_right_unknown_bit_pattern() {
    ModulePlugData plugData;
    plugData.type = ModuleType::Unknown;   // enum value 8
    plugData.bus = ModuleBus::Right;        // enum value 1
    plugData.insert = true;

    uint8_t encoded = CommData::encodeModulePlug(plugData);

    // Bit 7 = insert (1), Bit 6 = bus Right (1), Bits 0-5 = Unknown (8 = 0b001000)
    // Expected: 1_1_001000 = 0b11001000 = 0xC8
    uint8_t expected = (1 << 7) | (1 << 6) | 8;
    ASSERT(encoded == expected);
    ASSERT(encoded == 0xC8);

    // Verify decoding also works
    ModulePlugData decoded = CommData::decodeModulePlug(encoded);
    ASSERT(decoded.insert == true);
    ASSERT(decoded.bus == ModuleBus::Right);
    ASSERT(decoded.type == ModuleType::Unknown);
}

int main() {
    printf("Test: CommData ModulePlug Encoding Round-Trip\n");

    RUN_TEST(test_exhaustive_round_trip);
    RUN_TEST(test_insert_right_unknown_bit_pattern);

    TEST_SUMMARY();
}
