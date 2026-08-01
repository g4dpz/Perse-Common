// Feature: cross-project-optimisations, Property 5: RingBuffer Peek Across Wrap Boundary
// **Validates: Requirements 20.1**
//
// Write enough data to wrap the buffer, then peek at various offsets
// Assert peeked bytes match written bytes regardless of wrap position
// 100+ iterations with random buffer sizes and data

#include "freertos_mock.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <vector>

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

    template<typename T>
    const T* peek(size_t offset = 0) {
        if (offset + sizeof(T) > readAvailable()) return nullptr;

        size_t startPos = (beginning + offset) % size;
        size_t endPos = startPos + sizeof(T);

        if (endPos <= size) {
            // Contiguous — return direct pointer
            return reinterpret_cast<const T*>(buffer + startPos);
        }

        // Wraps — linearise into scratch buffer
        size_t firstPart = size - startPos;
        memcpy(peekBuf, buffer + startPos, firstPart);
        memcpy(peekBuf + firstPart, buffer, sizeof(T) - firstPart);
        return reinterpret_cast<const T*>(peekBuf);
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

// --- Property test ---

static unsigned int seed;

static size_t randRange(size_t min, size_t max) {
    return min + (rand_r(&seed) % (max - min + 1));
}

void test_peek_across_wrap_boundary() {
    const int iterations = 200;

    for (int iter = 0; iter < iterations; iter++) {
        // Use small buffer sizes to force wrapping
        size_t bufSize = randRange(16, 64);
        RingBuffer rb(bufSize);

        // Generate random data larger than buffer to force wrapping
        size_t totalDataLen = randRange(bufSize, bufSize * 3);
        std::vector<uint8_t> allData(totalDataLen);
        for (size_t i = 0; i < totalDataLen; i++) {
            allData[i] = static_cast<uint8_t>(rand_r(&seed) % 256);
        }

        // Write and read in chunks to advance the buffer position (cause wrap)
        size_t advanceAmount = randRange(1, bufSize - 1);
        std::vector<uint8_t> advanceData(advanceAmount);
        for (size_t i = 0; i < advanceAmount; i++) {
            advanceData[i] = static_cast<uint8_t>(rand_r(&seed) % 256);
        }

        // Write advance data and read it back to move the head/tail
        size_t written = rb.write(advanceData.data(), advanceAmount);
        std::vector<uint8_t> trash(advanceAmount);
        rb.read(trash.data(), written);

        // Now write the actual test data (as much as fits)
        size_t dataToWrite = std::min(totalDataLen, rb.writeAvailable());
        size_t actualWritten = rb.write(allData.data(), dataToWrite);

        // Peek at single bytes at various offsets
        for (size_t offset = 0; offset < actualWritten; offset++) {
            const uint8_t* peeked = rb.peek<uint8_t>(offset);
            ASSERT(peeked != nullptr);
            ASSERT(*peeked == allData[offset]);
        }

        // Peek at uint32_t across potential wrap boundaries
        if (actualWritten >= sizeof(uint32_t)) {
            for (size_t offset = 0; offset <= actualWritten - sizeof(uint32_t); offset++) {
                const uint32_t* peeked = rb.peek<uint32_t>(offset);
                ASSERT(peeked != nullptr);

                uint32_t expected;
                memcpy(&expected, allData.data() + offset, sizeof(uint32_t));
                ASSERT(*peeked == expected);
            }
        }
    }
}

void test_peek_insufficient_data_returns_nullptr() {
    const int iterations = 100;

    for (int iter = 0; iter < iterations; iter++) {
        size_t bufSize = randRange(16, 64);
        RingBuffer rb(bufSize);

        // Write some data, less than sizeof(uint32_t)
        uint8_t data[3] = {1, 2, 3};
        rb.write(data, 3);

        // Peek at uint32_t should fail (only 3 bytes available)
        const uint32_t* p = rb.peek<uint32_t>(0);
        ASSERT(p == nullptr);

        // Peek at offset beyond available
        const uint8_t* p2 = rb.peek<uint8_t>(3);
        ASSERT(p2 == nullptr);

        // Peek at valid offset should work
        const uint8_t* p3 = rb.peek<uint8_t>(0);
        ASSERT(p3 != nullptr);
        ASSERT(*p3 == 1);
    }
}

void test_peek_after_wrap_with_struct() {
    const int iterations = 150;

    struct TestStruct {
        uint16_t a;
        uint32_t b;
        uint8_t c;
    };

    for (int iter = 0; iter < iterations; iter++) {
        // Small buffer to guarantee wrapping
        size_t bufSize = randRange(sizeof(TestStruct) + 2, sizeof(TestStruct) * 3);
        RingBuffer rb(bufSize);

        // Advance past the wrap point
        size_t advanceBy = randRange(bufSize / 2, bufSize - 1);
        std::vector<uint8_t> junk(advanceBy);
        rb.write(junk.data(), advanceBy);
        rb.read(junk.data(), advanceBy);

        // Write a struct that will span the wrap boundary
        TestStruct original;
        original.a = static_cast<uint16_t>(rand_r(&seed));
        original.b = static_cast<uint32_t>(rand_r(&seed));
        original.c = static_cast<uint8_t>(rand_r(&seed));

        size_t written = rb.write(reinterpret_cast<uint8_t*>(&original), sizeof(TestStruct));
        if (written < sizeof(TestStruct)) continue; // Skip if buffer too small

        const TestStruct* peeked = rb.peek<TestStruct>(0);
        ASSERT(peeked != nullptr);
        ASSERT(peeked->a == original.a);
        ASSERT(peeked->b == original.b);
        ASSERT(peeked->c == original.c);
    }
}

int main() {
    seed = static_cast<unsigned int>(time(nullptr));
    printf("Property 5: RingBuffer Peek Across Wrap Boundary (seed=%u)\n", seed);

    RUN_TEST(test_peek_across_wrap_boundary);
    RUN_TEST(test_peek_insufficient_data_returns_nullptr);
    RUN_TEST(test_peek_after_wrap_with_struct);

    TEST_SUMMARY();
}
