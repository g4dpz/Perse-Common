// Test: RingBuffer Write Overflow (Requirements 3.1, 3.2, 3.3)
//
// Verifies RingBuffer behavior when writes exceed available capacity:
// - Partial write returns actual bytes written (M < N)
// - Write to full buffer returns 0 and leaves data intact
// - Filling to exact capacity reports correct readAvailable/writeAvailable

#include "freertos_mock.h"
#include <cstdio>
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
        free(buffer);
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

// Requirement 3.1: WHEN a write of N bytes is requested and only M < N bytes
// of space are available, THE RingBuffer SHALL write exactly M bytes and return M
void test_write_partial_when_space_limited() {
    const size_t capacity = 16;
    RingBuffer rb(capacity);

    // Fill most of the buffer, leaving only 5 bytes available
    uint8_t fillData[11];
    memset(fillData, 0xAA, sizeof(fillData));
    size_t written = rb.write(fillData, 11);
    ASSERT(written == 11);
    ASSERT(rb.writeAvailable() == 5);

    // Attempt to write 10 bytes when only 5 available
    uint8_t overflowData[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    size_t result = rb.write(overflowData, 10);

    // Should write exactly 5 bytes (M) and return 5
    ASSERT(result == 5);
    ASSERT(rb.writeAvailable() == 0);
    ASSERT(rb.readAvailable() == capacity);

    // Verify the 5 bytes written are correct
    uint8_t readBuf[16];
    rb.read(readBuf, 11); // skip the fill data
    size_t readResult = rb.read(readBuf, 5);
    ASSERT(readResult == 5);
    ASSERT(readBuf[0] == 1);
    ASSERT(readBuf[1] == 2);
    ASSERT(readBuf[2] == 3);
    ASSERT(readBuf[3] == 4);
    ASSERT(readBuf[4] == 5);
}

// Requirement 3.2: WHEN the buffer is full (writeAvailable == 0), THE RingBuffer
// SHALL return 0 from write and leave existing data intact
void test_write_full_buffer_returns_zero() {
    const size_t capacity = 8;
    RingBuffer rb(capacity);

    // Fill the buffer completely
    uint8_t fillData[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    size_t written = rb.write(fillData, capacity);
    ASSERT(written == capacity);
    ASSERT(rb.writeAvailable() == 0);

    // Attempt to write when full
    uint8_t extraData[4] = {99, 99, 99, 99};
    size_t result = rb.write(extraData, 4);

    // Should return 0
    ASSERT(result == 0);

    // Existing data must be intact
    uint8_t readBuf[8];
    size_t readResult = rb.read(readBuf, capacity);
    ASSERT(readResult == capacity);
    for (size_t i = 0; i < capacity; i++) {
        ASSERT(readBuf[i] == fillData[i]);
    }
}

// Requirement 3.3: WHEN the buffer is filled to exact capacity, THE RingBuffer
// SHALL report readAvailable equal to the capacity and writeAvailable equal to 0
void test_fill_exact_capacity() {
    const size_t capacity = 32;
    RingBuffer rb(capacity);

    // Initially empty
    ASSERT(rb.readAvailable() == 0);
    ASSERT(rb.writeAvailable() == capacity);

    // Fill to exact capacity
    uint8_t data[32];
    for (size_t i = 0; i < capacity; i++) {
        data[i] = static_cast<uint8_t>(i);
    }
    size_t written = rb.write(data, capacity);

    ASSERT(written == capacity);
    ASSERT(rb.readAvailable() == capacity);
    ASSERT(rb.writeAvailable() == 0);
}

// Additional edge case: partial write with wrap-around
void test_partial_write_with_wraparound() {
    const size_t capacity = 10;
    RingBuffer rb(capacity);

    // Write 7 bytes, read 5 to advance the head past the midpoint
    uint8_t initial[7] = {1, 2, 3, 4, 5, 6, 7};
    rb.write(initial, 7);
    uint8_t trash[5];
    rb.read(trash, 5);

    // Now: readAvailable=2, writeAvailable=8, head advanced past midpoint
    ASSERT(rb.readAvailable() == 2);
    ASSERT(rb.writeAvailable() == 8);

    // Write 6 bytes (fits), leaving 2 available
    uint8_t data1[6] = {10, 20, 30, 40, 50, 60};
    size_t w1 = rb.write(data1, 6);
    ASSERT(w1 == 6);
    ASSERT(rb.writeAvailable() == 2);

    // Now attempt to write 5 bytes when only 2 available (partial, wraps)
    uint8_t data2[5] = {70, 80, 90, 100, 110};
    size_t w2 = rb.write(data2, 5);
    ASSERT(w2 == 2);
    ASSERT(rb.writeAvailable() == 0);

    // Read all data and verify ordering
    uint8_t readBuf[10];
    // First the remaining 2 from initial write (6, 7)
    size_t r = rb.read(readBuf, 10);
    ASSERT(r == 10);
    ASSERT(readBuf[0] == 6);
    ASSERT(readBuf[1] == 7);
    ASSERT(readBuf[2] == 10);
    ASSERT(readBuf[3] == 20);
    ASSERT(readBuf[4] == 30);
    ASSERT(readBuf[5] == 40);
    ASSERT(readBuf[6] == 50);
    ASSERT(readBuf[7] == 60);
    ASSERT(readBuf[8] == 70);
    ASSERT(readBuf[9] == 80);
}

int main() {
    printf("Test: RingBuffer Write Overflow (Requirements 3.1, 3.2, 3.3)\n");

    RUN_TEST(test_write_partial_when_space_limited);
    RUN_TEST(test_write_full_buffer_returns_zero);
    RUN_TEST(test_fill_exact_capacity);
    RUN_TEST(test_partial_write_with_wraparound);

    TEST_SUMMARY();
}
