// Test: RingBuffer peek<T> overflow documentation
// Requirements: 6.1, 6.2
//
// Documents the latent buffer overflow risk in RingBuffer::peek<T>() when
// sizeof(T) > 64. The internal peekBuf[64] scratch buffer is used when data
// wraps the ring boundary, and memcpy will overflow if sizeof(T) exceeds 64.
//
// This test:
// - Documents the overflow risk with a compile-time comment and static_assert
// - Verifies that peek with sizeof(T) == 64 succeeds (boundary of safe usage)
// - Does NOT trigger the actual overflow (that would be undefined behavior)

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

    template<typename T>
    const T* peek(size_t offset = 0) {
        if (offset + sizeof(T) > readAvailable()) return nullptr;

        size_t startPos = (beginning + offset) % size;
        size_t endPos = startPos + sizeof(T);

        if (endPos <= size) {
            // Contiguous — return direct pointer into buffer directly
            return reinterpret_cast<const T*>(buffer + startPos);
        }

        // Wraps — linearise into scratch buffer
        // BUG: If sizeof(T) > 64, this overflows peekBuf[64]!
        // The memcpy below copies sizeof(T) bytes total into a 64-byte buffer.
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
    uint8_t peekBuf[64];  // scratch buffer — ONLY safe for sizeof(T) <= 64
};

// ============================================================================
// OVERFLOW RISK DOCUMENTATION
// ============================================================================
//
// BUG: RingBuffer::peek<T>() uses an internal uint8_t peekBuf[64] as a scratch
// buffer when the peeked data wraps the ring buffer boundary. If sizeof(T) > 64,
// the memcpy into peekBuf overflows, corrupting adjacent stack/member memory.
//
// AFFECTED CODE (RingBuffer.h, peek<T> template):
//   size_t firstPart = size - startPos;
//   memcpy(peekBuf, buffer + startPos, firstPart);
//   memcpy(peekBuf + firstPart, buffer, sizeof(T) - firstPart);
//   // Total bytes written = firstPart + (sizeof(T) - firstPart) = sizeof(T)
//   // If sizeof(T) > 64, writes past end of peekBuf[64]
//
// IMPACT: Undefined behavior (stack corruption, silent data corruption, crash)
// MITIGATION: Add static_assert(sizeof(T) <= 64) in peek<T>, or increase
//             peekBuf size, or dynamically allocate when T is large.
//
// NOTE: The overflow only occurs when peek data WRAPS the buffer boundary.
// If the data is contiguous, peek returns a direct pointer into the ring buffer
// and peekBuf is not used.
// ============================================================================

// Compile-time documentation: static_assert that would prevent the overflow
// if added to the actual RingBuffer::peek<T>() implementation.
struct ExactlyFit64 {
    uint8_t data[64];
};

struct TooBig65 {
    uint8_t data[65];
};

struct TooBig128 {
    uint8_t data[128];
};

// These static_asserts document the safe/unsafe boundary:
static_assert(sizeof(ExactlyFit64) == 64, "ExactlyFit64 must be exactly 64 bytes");
static_assert(sizeof(TooBig65) == 65, "TooBig65 must be 65 bytes — would overflow peekBuf");
static_assert(sizeof(TooBig128) == 128, "TooBig128 must be 128 bytes — would overflow peekBuf");

// This is the static_assert that SHOULD be added to RingBuffer::peek<T>():
// static_assert(sizeof(T) <= sizeof(peekBuf), "peek<T>: sizeof(T) exceeds peekBuf capacity (64 bytes)");

// ============================================================================
// TEST: peek with sizeof(T) == 64 succeeds at wrap boundary (safe boundary)
// ============================================================================

void test_peek_64_byte_struct_at_wrap_boundary() {
    // Use a buffer size that forces wrapping when we peek a 64-byte struct.
    // Buffer capacity = 128 bytes. We'll advance the read/write pointers so
    // that a 64-byte write spans the internal boundary.
    const size_t capacity = 128;
    RingBuffer rb(capacity);

    // Advance the buffer position so the next write wraps around the boundary.
    // Internal size = capacity + 1 = 129. Write 100 bytes and read them back
    // to advance beginning/end to position 100. Then writing 64 bytes will
    // span positions 100..163, wrapping at 129 back to position 34.
    uint8_t junk[100];
    memset(junk, 0xAA, sizeof(junk));
    size_t written = rb.write(junk, 100);
    ASSERT(written == 100);
    uint8_t trash[100];
    size_t readBack = rb.read(trash, 100);
    ASSERT(readBack == 100);

    // Now beginning == end == 100 (internal position)
    // Writing 64 bytes will wrap: positions 100..128 (29 bytes) + 0..34 (35 bytes)
    ExactlyFit64 original;
    for (int i = 0; i < 64; i++) {
        original.data[i] = static_cast<uint8_t>(i * 3 + 7);  // deterministic pattern
    }

    written = rb.write(reinterpret_cast<uint8_t*>(&original), sizeof(ExactlyFit64));
    ASSERT(written == sizeof(ExactlyFit64));

    // Peek should succeed — sizeof(ExactlyFit64) == 64 == sizeof(peekBuf)
    const ExactlyFit64* peeked = rb.peek<ExactlyFit64>(0);
    ASSERT(peeked != nullptr);

    // Verify all 64 bytes match
    for (int i = 0; i < 64; i++) {
        ASSERT(peeked->data[i] == original.data[i]);
    }
}

// ============================================================================
// TEST: peek with sizeof(T) == 64 succeeds when data is contiguous (no wrap)
// ============================================================================

void test_peek_64_byte_struct_contiguous() {
    // When data doesn't wrap, peek returns a direct pointer and peekBuf is not
    // used at all. This should always work regardless of type size.
    const size_t capacity = 256;
    RingBuffer rb(capacity);

    ExactlyFit64 original;
    for (int i = 0; i < 64; i++) {
        original.data[i] = static_cast<uint8_t>(i ^ 0x5A);
    }

    size_t written = rb.write(reinterpret_cast<uint8_t*>(&original), sizeof(ExactlyFit64));
    ASSERT(written == sizeof(ExactlyFit64));

    const ExactlyFit64* peeked = rb.peek<ExactlyFit64>(0);
    ASSERT(peeked != nullptr);

    for (int i = 0; i < 64; i++) {
        ASSERT(peeked->data[i] == original.data[i]);
    }
}

// ============================================================================
// TEST: Document that peek with sizeof(T) > 64 would overflow (compile-time)
// ============================================================================

void test_document_overflow_risk() {
    // This test documents the overflow risk WITHOUT triggering it.
    // We verify that peek works for types up to 64 bytes but document
    // that types > 64 bytes would cause undefined behavior when data wraps.

    // The overflow condition is:
    //   sizeof(T) > sizeof(peekBuf)  AND  data wraps the buffer boundary
    //
    // For sizeof(T) <= 64: SAFE (peekBuf can hold the linearized data)
    // For sizeof(T) > 64:  UNSAFE (memcpy overflows peekBuf)
    //
    // Note: When data is contiguous (doesn't wrap), peekBuf is not used
    // and any sizeof(T) works. The bug only manifests at wrap boundaries.

    // Verify the unsafe types are indeed > 64 bytes
    ASSERT(sizeof(TooBig65) > 64);
    ASSERT(sizeof(TooBig128) > 64);
    ASSERT(sizeof(ExactlyFit64) <= 64);

    // We intentionally do NOT call rb.peek<TooBig65>() at a wrap boundary
    // because that would trigger undefined behavior (buffer overflow).

    // Demonstrate that peek<TooBig65> at a contiguous position still works
    // (because peekBuf is not used for contiguous data):
    const size_t capacity = 256;
    RingBuffer rb(capacity);

    TooBig65 bigData;
    for (int i = 0; i < 65; i++) {
        bigData.data[i] = static_cast<uint8_t>(i);
    }
    size_t written = rb.write(reinterpret_cast<uint8_t*>(&bigData), sizeof(TooBig65));
    ASSERT(written == sizeof(TooBig65));

    // This peek succeeds because data is contiguous (no wrap), so the direct
    // pointer path is taken and peekBuf is never written to.
    const TooBig65* peeked = rb.peek<TooBig65>(0);
    ASSERT(peeked != nullptr);
    for (int i = 0; i < 65; i++) {
        ASSERT(peeked->data[i] == bigData.data[i]);
    }

    // WARNING: If we advanced the buffer to force a wrap, peek<TooBig65>()
    // would overflow peekBuf[64] — 65 bytes into a 64-byte buffer.
    // DO NOT test this at a wrap boundary — it is undefined behavior.

    printf("    [DOCUMENTED] peek<T> with sizeof(T)>64 overflows peekBuf[64] at wrap boundary\n");
}

// ============================================================================

int main() {
    printf("Test: RingBuffer peek overflow documentation (Requirements 6.1, 6.2)\n");

    RUN_TEST(test_peek_64_byte_struct_at_wrap_boundary);
    RUN_TEST(test_peek_64_byte_struct_contiguous);
    RUN_TEST(test_document_overflow_risk);

    TEST_SUMMARY();
}
