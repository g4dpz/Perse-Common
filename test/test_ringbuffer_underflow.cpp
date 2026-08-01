// Task 2.2: RingBuffer Read Underflow Edge Cases
// Requirements: 4.1, 4.2, 4.3
//
// Tests that RingBuffer handles read underflow gracefully:
// - Reading from empty buffer returns 0 and does not modify destination
// - Reading N bytes when only M available returns M
// - Skipping more than readAvailable skips only available bytes

#include "freertos_mock.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <algorithm>

// --- Inline RingBuffer implementation for host compilation ---

class RingBuffer {
public:
    RingBuffer(size_t sz) : size(sz + 1) {
        buffer = static_cast<uint8_t*>(malloc(size));
        memset(buffer, 0, size);
    }

    ~RingBuffer() {
        ::free(buffer);
    }

    size_t writeAvailable() {
        if (end >= beginning) return size + beginning - end - 1;
        else return beginning - end - 1;
    }

    size_t readAvailable() {
        if (end >= beginning) return end - beginning;
        else return size + end - beginning;
    }

    size_t read(uint8_t* destination, size_t n) {
        n = std::min(n, readAvailable());
        if (n == 0) return 0;

        if (end > beginning) {
            memcpy(destination, buffer + beginning, n);
        } else {
            size_t first = std::min(size - beginning, n);
            memcpy(destination, buffer + beginning, first);
            memcpy(destination + first, buffer, n - first);
        }
        beginning = (beginning + n) % size;
        return n;
    }

    size_t write(uint8_t* source, size_t n) {
        n = std::min(n, writeAvailable());
        if (n == 0) return 0;

        if (beginning > end) {
            memcpy(buffer + end, source, n);
        } else {
            size_t first = std::min(size - end, n);
            memcpy(buffer + end, source, first);
            memcpy(buffer, source + first, n - first);
        }
        end = (end + n) % size;
        return n;
    }

    size_t skip(size_t n) {
        n = std::min(n, readAvailable());
        if (n == 0) return 0;
        beginning = (beginning + n) % size;
        return n;
    }

    void clear() {
        beginning = end = 0;
    }

private:
    size_t size;
    size_t beginning = 0;
    size_t end = 0;
    uint8_t* buffer;
};

// --- Tests ---

// Requirement 4.1: Read from empty buffer returns 0 and does not modify destination
void test_read_empty_returns_zero_no_modify() {
    RingBuffer rb(64);

    // Fill destination with a sentinel pattern
    uint8_t dest[16];
    memset(dest, 0xAB, sizeof(dest));

    // Read from empty buffer
    size_t result = rb.read(dest, sizeof(dest));

    // Should return 0
    ASSERT(result == 0);

    // Destination must be unmodified
    for (size_t i = 0; i < sizeof(dest); i++) {
        ASSERT(dest[i] == 0xAB);
    }

    // Buffer state should still be empty
    ASSERT(rb.readAvailable() == 0);
    ASSERT(rb.writeAvailable() == 64);
}

// Requirement 4.2: Read of N bytes when only M < N available returns M
void test_read_partial_returns_available() {
    RingBuffer rb(64);

    // Write 5 bytes
    uint8_t src[5] = {10, 20, 30, 40, 50};
    size_t written = rb.write(src, 5);
    ASSERT(written == 5);

    // Request 16 bytes but only 5 are available
    uint8_t dest[16];
    memset(dest, 0xFF, sizeof(dest));
    size_t result = rb.read(dest, 16);

    // Should return only 5
    ASSERT(result == 5);

    // First 5 bytes should match what was written
    ASSERT(dest[0] == 10);
    ASSERT(dest[1] == 20);
    ASSERT(dest[2] == 30);
    ASSERT(dest[3] == 40);
    ASSERT(dest[4] == 50);

    // Remaining bytes in dest should be untouched
    for (size_t i = 5; i < 16; i++) {
        ASSERT(dest[i] == 0xFF);
    }

    // Buffer should now be empty
    ASSERT(rb.readAvailable() == 0);
}

// Requirement 4.2 (additional): Read partial at various fill levels
void test_read_partial_various_sizes() {
    RingBuffer rb(32);

    // Write 10 bytes
    uint8_t src[10];
    for (int i = 0; i < 10; i++) src[i] = (uint8_t)(i + 1);
    size_t written = rb.write(src, 10);
    ASSERT(written == 10);

    // Request exactly 10 - should succeed fully
    uint8_t dest[20];
    memset(dest, 0, sizeof(dest));
    size_t result = rb.read(dest, 10);
    ASSERT(result == 10);
    for (int i = 0; i < 10; i++) {
        ASSERT(dest[i] == (uint8_t)(i + 1));
    }

    // Now buffer is empty, write 3 bytes and request 7
    uint8_t src2[3] = {0xAA, 0xBB, 0xCC};
    written = rb.write(src2, 3);
    ASSERT(written == 3);

    memset(dest, 0, sizeof(dest));
    result = rb.read(dest, 7);
    ASSERT(result == 3);
    ASSERT(dest[0] == 0xAA);
    ASSERT(dest[1] == 0xBB);
    ASSERT(dest[2] == 0xCC);
}

// Requirement 4.3: Skip with count exceeding readAvailable skips only available bytes
void test_skip_exceeding_available() {
    RingBuffer rb(64);

    // Write 8 bytes
    uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t written = rb.write(src, 8);
    ASSERT(written == 8);
    ASSERT(rb.readAvailable() == 8);

    // Skip 20 bytes (only 8 available)
    size_t skipped = rb.skip(20);

    // Should skip only 8
    ASSERT(skipped == 8);

    // Buffer should now be empty
    ASSERT(rb.readAvailable() == 0);
}

// Requirement 4.3 (additional): Skip on empty buffer
void test_skip_empty_buffer() {
    RingBuffer rb(32);

    // Skip from empty buffer
    size_t skipped = rb.skip(10);
    ASSERT(skipped == 0);
    ASSERT(rb.readAvailable() == 0);
}

// Requirement 4.3 (additional): Skip partial then verify remaining data
void test_skip_partial_then_read() {
    RingBuffer rb(64);

    // Write 10 bytes
    uint8_t src[10];
    for (int i = 0; i < 10; i++) src[i] = (uint8_t)(i + 100);
    rb.write(src, 10);

    // Skip 4 bytes
    size_t skipped = rb.skip(4);
    ASSERT(skipped == 4);
    ASSERT(rb.readAvailable() == 6);

    // Now try to skip 100 bytes (only 6 available)
    skipped = rb.skip(100);
    ASSERT(skipped == 6);
    ASSERT(rb.readAvailable() == 0);
}

int main() {
    printf("Test: RingBuffer Read Underflow (Requirements 4.1, 4.2, 4.3)\n");

    RUN_TEST(test_read_empty_returns_zero_no_modify);
    RUN_TEST(test_read_partial_returns_available);
    RUN_TEST(test_read_partial_various_sizes);
    RUN_TEST(test_skip_exceeding_available);
    RUN_TEST(test_skip_empty_buffer);
    RUN_TEST(test_skip_partial_then_read);

    TEST_SUMMARY();
}
