// Unit Test: RingBuffer Wrap-Around Integrity
// Requirements: 5.1, 5.2
//
// Verifies that alternating write/read cycles that wrap the internal pointer
// multiple times (3+) produce correct data in order, and that readAvailable
// and writeAvailable remain correct throughout.

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
    uint8_t peekBuf[64];
};

// --- Test Functions ---

// Requirement 5.1: Data written matches data read after multiple wraps
void test_wraparound_data_integrity_3_wraps() {
    // Use a small buffer (capacity=10, internal size=11) to force frequent wrapping.
    // Each full write/read cycle of 10 bytes advances both pointers by 10.
    // Internal size is 11, so after 2 cycles the pointers wrap past the end.
    // We do enough cycles to wrap 3+ times.
    const size_t capacity = 10;
    RingBuffer rb(capacity);

    // Each cycle: write capacity bytes, read them back, verify.
    // Internal size = 11. Pointer advances by 10 per cycle.
    // After cycle 1: pointers at 10. After cycle 2: pointers at 10+10=20 mod 11 = 9.
    // After cycle 3: 9+10=19 mod 11 = 8. After cycle 4: 8+10=18 mod 11 = 7.
    // Wraps occur at cycles 2, 3, 4... so 4 cycles guarantees 3+ wraps.
    const int numCycles = 6; // Guarantees well over 3 wraps

    for (int cycle = 0; cycle < numCycles; cycle++) {
        uint8_t writeData[10];
        uint8_t readData[10];

        // Fill with predictable pattern based on cycle
        for (size_t i = 0; i < capacity; i++) {
            writeData[i] = static_cast<uint8_t>((cycle * 10) + i);
        }

        size_t written = rb.write(writeData, capacity);
        ASSERT(written == capacity);

        size_t bytesRead = rb.read(readData, capacity);
        ASSERT(bytesRead == capacity);

        // Verify data matches what was written
        for (size_t i = 0; i < capacity; i++) {
            ASSERT(readData[i] == writeData[i]);
        }
    }
}

// Requirement 5.1: Partial write/read cycles with data integrity across wraps
void test_wraparound_partial_cycles_data_integrity() {
    // Use capacity=7 (internal size=8). Write 5 bytes, read 5 bytes per cycle.
    // Pointer advances 5 per cycle: 5, 10%8=2, 7%8=7, 12%8=4, 9%8=1...
    // Many wraps happen quickly.
    const size_t capacity = 7;
    RingBuffer rb(capacity);
    const size_t chunkSize = 5;
    const int numCycles = 10; // Many more wraps than 3

    uint8_t counter = 0;

    for (int cycle = 0; cycle < numCycles; cycle++) {
        uint8_t writeData[5];
        uint8_t readData[5];

        for (size_t i = 0; i < chunkSize; i++) {
            writeData[i] = counter++;
        }

        size_t written = rb.write(writeData, chunkSize);
        ASSERT(written == chunkSize);

        size_t bytesRead = rb.read(readData, chunkSize);
        ASSERT(bytesRead == chunkSize);

        // Verify sequential counter pattern survived the wrap
        for (size_t i = 0; i < chunkSize; i++) {
            uint8_t expected = static_cast<uint8_t>((cycle * chunkSize) + i);
            ASSERT(readData[i] == expected);
        }
    }
}

// Requirement 5.2: readAvailable and writeAvailable correct after 3+ wraps
void test_wraparound_counts_correct_after_multiple_wraps() {
    const size_t capacity = 8;
    RingBuffer rb(capacity);

    // Perform enough write/read cycles to wrap 3+ times
    // Internal size = 9. Each cycle advances by chunkSize.
    const size_t chunkSize = 6;
    const int numCycles = 8; // 6*8=48 bytes total, wraps at 9 → many wraps

    for (int cycle = 0; cycle < numCycles; cycle++) {
        // Before write: buffer should be empty
        ASSERT(rb.readAvailable() == 0);
        ASSERT(rb.writeAvailable() == capacity);

        uint8_t writeData[6];
        for (size_t i = 0; i < chunkSize; i++) {
            writeData[i] = static_cast<uint8_t>(cycle + i);
        }

        size_t written = rb.write(writeData, chunkSize);
        ASSERT(written == chunkSize);

        // After write: should have chunkSize readable, capacity-chunkSize writable
        ASSERT(rb.readAvailable() == chunkSize);
        ASSERT(rb.writeAvailable() == capacity - chunkSize);

        uint8_t readData[6];
        size_t bytesRead = rb.read(readData, chunkSize);
        ASSERT(bytesRead == chunkSize);

        // After read: buffer empty again
        ASSERT(rb.readAvailable() == 0);
        ASSERT(rb.writeAvailable() == capacity);
    }
}

// Requirement 5.1 + 5.2: Accumulate data across wraps, verify order and counts
void test_wraparound_accumulated_data_order() {
    // Write in small chunks, read in larger chunks, across many wraps.
    // This tests that data order is preserved when writes and reads are misaligned.
    const size_t capacity = 12;
    RingBuffer rb(capacity);

    // Write 4 bytes at a time, 20 times = 80 bytes total through a 12-byte buffer
    const size_t writeChunk = 4;
    const int totalWrites = 20;
    uint8_t allWritten[80];
    uint8_t allRead[80];
    size_t totalWritten = 0;
    size_t totalRead = 0;

    for (int i = 0; i < totalWrites; i++) {
        uint8_t data[4];
        for (size_t j = 0; j < writeChunk; j++) {
            data[j] = static_cast<uint8_t>(totalWritten + j);
            allWritten[totalWritten + j] = data[j];
        }

        size_t written = rb.write(data, writeChunk);
        ASSERT(written == writeChunk);
        totalWritten += written;

        // Read back the same amount
        uint8_t readBuf[4];
        size_t bytesRead = rb.read(readBuf, writeChunk);
        ASSERT(bytesRead == writeChunk);
        for (size_t j = 0; j < bytesRead; j++) {
            allRead[totalRead + j] = readBuf[j];
        }
        totalRead += bytesRead;
    }

    // Verify all data read matches all data written, in order
    ASSERT(totalRead == totalWritten);
    for (size_t i = 0; i < totalRead; i++) {
        ASSERT(allRead[i] == allWritten[i]);
    }
}

// Requirement 5.2: Counts stay correct with asymmetric write/read across wraps
void test_wraparound_asymmetric_write_read_counts() {
    // Write more than we read each cycle, let buffer fill partially,
    // then drain. Verify counts remain consistent.
    const size_t capacity = 16;
    RingBuffer rb(capacity);

    // Phase 1: Write 10 bytes (buffer has 10 readable, 6 writable)
    uint8_t data[16];
    for (size_t i = 0; i < 16; i++) data[i] = static_cast<uint8_t>(i);

    rb.write(data, 10);
    ASSERT(rb.readAvailable() == 10);
    ASSERT(rb.writeAvailable() == 6);

    // Phase 2: Read 7, write 7 - wraps the pointer
    uint8_t readBuf[16];
    rb.read(readBuf, 7);
    ASSERT(rb.readAvailable() == 3);
    ASSERT(rb.writeAvailable() == 13);

    uint8_t data2[7] = {100, 101, 102, 103, 104, 105, 106};
    rb.write(data2, 7);
    ASSERT(rb.readAvailable() == 10);
    ASSERT(rb.writeAvailable() == 6);

    // Phase 3: Read all, write full capacity - another wrap
    rb.read(readBuf, 10);
    ASSERT(rb.readAvailable() == 0);
    ASSERT(rb.writeAvailable() == capacity);

    uint8_t data3[16];
    for (size_t i = 0; i < 16; i++) data3[i] = static_cast<uint8_t>(200 + i);
    rb.write(data3, capacity);
    ASSERT(rb.readAvailable() == capacity);
    ASSERT(rb.writeAvailable() == 0);

    // Phase 4: Read 8, write 8 - yet another wrap
    rb.read(readBuf, 8);
    ASSERT(rb.readAvailable() == 8);
    ASSERT(rb.writeAvailable() == 8);

    uint8_t data4[8] = {50, 51, 52, 53, 54, 55, 56, 57};
    rb.write(data4, 8);
    ASSERT(rb.readAvailable() == 16);
    ASSERT(rb.writeAvailable() == 0);

    // Final drain - verify we're past 3 wraps and counts are still correct
    rb.read(readBuf, 16);
    ASSERT(rb.readAvailable() == 0);
    ASSERT(rb.writeAvailable() == capacity);
}

int main() {
    printf("RingBuffer Wrap-Around Integrity Tests (Requirements 5.1, 5.2)\n");

    RUN_TEST(test_wraparound_data_integrity_3_wraps);
    RUN_TEST(test_wraparound_partial_cycles_data_integrity);
    RUN_TEST(test_wraparound_counts_correct_after_multiple_wraps);
    RUN_TEST(test_wraparound_accumulated_data_order);
    RUN_TEST(test_wraparound_asymmetric_write_read_counts);

    TEST_SUMMARY();
}
