/**
 * test_driveinfo_roundtrip.cpp
 *
 * Tests DriveInfo serialization (toData) and deserialization (deserialize) round-trip:
 * - 0 markers, empty frame
 * - 4 markers with 1024-byte frame
 * - Verifies markerInfo.action, marker IDs, projected points, and frame data preserved
 *
 * Validates: Requirements 9.1, 9.2
 *
 * Compile: g++ -std=c++17 -o test_driveinfo_roundtrip test_driveinfo_roundtrip.cpp && ./test_driveinfo_roundtrip
 */

// Stub ESP log functions to no-ops before including sources
#define ESP_LOGE(tag, ...)
#define ESP_LOGD(tag, ...)
#define ESP_LOGW(tag, ...)
#define ESP_LOG_H_

#include "freertos_mock.h"

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <memory>
#include <algorithm>
#include <array>

// --- Inline RingBuffer implementation (avoids malloc.h issue on macOS) ---

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
            return reinterpret_cast<const T*>(buffer + startPos);
        }

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

// --- Inline MarkerInfo and DriveInfo ---

#include "../src/MarkerInfo.h"

#define FEED_ENV_LEN 8
const uint8_t FrameHeader[FEED_ENV_LEN] = { 0x18, 0x20, 0x55, 0xf2, 0x5a, 0xc0, 0x4d, 0xaa };
const uint8_t FrameTrailer[FEED_ENV_LEN] = { 0x42, 0x2c, 0xd9, 0xe3, 0xff, 0xa0, 0x11, 0x01 };
const uint8_t FrameSizeShift[4] = { 2, 3, 1, 0 };

struct CamFrame {
    size_t size = 0;
    void* data = nullptr;
};

struct MotorInfo {
    int8_t left;
    int8_t right;
};

struct DriveInfo {
    CamFrame frame = {};
    MarkerInfo markerInfo = {};

    ~DriveInfo() {
        if (frame.data) {
            free(frame.data);
            frame.data = nullptr;
        }
    }

    static constexpr size_t baseSize = sizeof(CamFrame::size) + sizeof(MarkerInfo::action) + sizeof(size_t);

    size_t size() const {
        size_t s = baseSize + frame.size + markerInfo.markers.size() * (sizeof(Marker::id) + 4 * 2 * sizeof(int16_t));
        return s;
    }

    void toData(void* dest) const {
        auto data = (uint8_t*)dest;

        memcpy(data, &markerInfo.action, sizeof(MarkerAction));
        data += sizeof(MarkerAction);

        const size_t markerNum = markerInfo.markers.size();
        memcpy(data, &markerNum, sizeof(size_t));
        data += sizeof(size_t);

        for (size_t i = 0; i < markerNum; ++i) {
            const Marker& marker = markerInfo.markers[i];

            memcpy(data, &marker.id, sizeof(Marker::id));
            data += sizeof(Marker::id);

            for (const std::pair<int16_t, int16_t>& point : marker.projected) {
                memcpy(data, &point.first, sizeof(int16_t));
                data += sizeof(int16_t);

                memcpy(data, &point.second, sizeof(int16_t));
                data += sizeof(int16_t);
            }
        }

        memcpy(data, &frame.size, sizeof(CamFrame::size));
        data += sizeof(CamFrame::size);

        if (frame.size > 0 && frame.data) {
            memcpy(data, frame.data, frame.size);
            data += frame.size;
        }
    }

    static std::unique_ptr<DriveInfo> deserialize(RingBuffer& buf, size_t size) {
        if (size < baseSize) {
            return nullptr;
        }

        MarkerInfo markerInfo;
        buf.read((uint8_t*)(&markerInfo.action), sizeof(MarkerAction));

        size_t markerNum = 0;
        buf.read((uint8_t*)(&markerNum), sizeof(size_t));

        for (size_t i = 0; i < markerNum; ++i) {
            Marker marker;

            buf.read((uint8_t*)&marker.id, sizeof(Marker::id));

            for (std::pair<int16_t, int16_t>& point : marker.projected) {
                buf.read((uint8_t*)&point.first, sizeof(int16_t));
                buf.read((uint8_t*)&point.second, sizeof(int16_t));
            }

            markerInfo.markers.emplace_back(marker);
        }

        std::unique_ptr<DriveInfo> info = std::make_unique<DriveInfo>();
        info->markerInfo = markerInfo;

        buf.read((uint8_t*)&info->frame.size, sizeof(CamFrame::size));

        if (buf.readAvailable() < info->frame.size) {
            return nullptr;
        }

        if (info->frame.size > 0) {
            info->frame.data = malloc(info->frame.size);
            if (info->frame.data == nullptr) {
                return nullptr;
            }
            buf.read((uint8_t*)info->frame.data, info->frame.size);
        }

        return info;
    }
};

// ============================================================
// Test: Round-trip with 0 markers, 0-byte frame
// ============================================================
void test_zero_markers_empty_frame() {
    DriveInfo original;
    original.markerInfo.action = MarkerAction::None;
    original.markerInfo.markers.clear();
    original.frame.size = 0;
    original.frame.data = nullptr;

    // Serialize
    size_t dataSize = original.size();
    ASSERT(dataSize == DriveInfo::baseSize);

    std::vector<uint8_t> serialized(dataSize);
    original.toData(serialized.data());

    // Deserialize via RingBuffer
    RingBuffer buf(dataSize + 16);
    buf.write(serialized.data(), dataSize);

    auto deserialized = DriveInfo::deserialize(buf, dataSize);
    ASSERT(deserialized != nullptr);

    // Verify action
    ASSERT(deserialized->markerInfo.action == MarkerAction::None);

    // Verify 0 markers
    ASSERT(deserialized->markerInfo.markers.size() == 0);

    // Verify empty frame
    ASSERT(deserialized->frame.size == 0);
}

// ============================================================
// Test: Round-trip with 4 markers and 1024-byte frame
// ============================================================
void test_four_markers_with_frame() {
    DriveInfo original;
    original.markerInfo.action = MarkerAction::GoTowards;

    // Create 4 markers with distinct data
    for (uint16_t i = 0; i < 4; ++i) {
        Marker m;
        m.id = 100 + i;
        for (int p = 0; p < 4; ++p) {
            m.projected[p] = { static_cast<int16_t>(i * 10 + p), static_cast<int16_t>(-(i * 10 + p)) };
        }
        original.markerInfo.markers.push_back(m);
    }

    // Create 1024-byte frame with known pattern
    const size_t frameSize = 1024;
    original.frame.size = frameSize;
    original.frame.data = malloc(frameSize);
    ASSERT(original.frame.data != nullptr);
    for (size_t i = 0; i < frameSize; ++i) {
        ((uint8_t*)original.frame.data)[i] = (uint8_t)(i & 0xFF);
    }

    // Serialize
    size_t dataSize = original.size();
    std::vector<uint8_t> serialized(dataSize);
    original.toData(serialized.data());

    // Deserialize via RingBuffer
    RingBuffer buf(dataSize + 16);
    buf.write(serialized.data(), dataSize);

    auto deserialized = DriveInfo::deserialize(buf, dataSize);
    ASSERT(deserialized != nullptr);

    // Verify action
    ASSERT(deserialized->markerInfo.action == MarkerAction::GoTowards);

    // Verify marker count
    ASSERT(deserialized->markerInfo.markers.size() == 4);

    // Verify each marker's ID and projected points
    for (uint16_t i = 0; i < 4; ++i) {
        const Marker& m = deserialized->markerInfo.markers[i];
        ASSERT(m.id == 100 + i);

        for (int p = 0; p < 4; ++p) {
            int16_t expectedX = static_cast<int16_t>(i * 10 + p);
            int16_t expectedY = static_cast<int16_t>(-(i * 10 + p));
            ASSERT(m.projected[p].first == expectedX);
            ASSERT(m.projected[p].second == expectedY);
        }
    }

    // Verify frame data
    ASSERT(deserialized->frame.size == frameSize);
    ASSERT(deserialized->frame.data != nullptr);
    for (size_t i = 0; i < frameSize; ++i) {
        ASSERT(((uint8_t*)deserialized->frame.data)[i] == (uint8_t)(i & 0xFF));
    }
}

// ============================================================
// Test: Verify specific marker actions and edge values
// ============================================================
void test_marker_action_and_edge_ids() {
    DriveInfo original;
    original.markerInfo.action = MarkerAction::LifeDetected;

    // Single marker with max uint16 ID and extreme projected values
    Marker m;
    m.id = 65535;
    m.projected[0] = { INT16_MIN, INT16_MAX };
    m.projected[1] = { INT16_MAX, INT16_MIN };
    m.projected[2] = { 0, 0 };
    m.projected[3] = { -1, 1 };
    original.markerInfo.markers.push_back(m);

    // Empty frame
    original.frame.size = 0;
    original.frame.data = nullptr;

    // Serialize
    size_t dataSize = original.size();
    std::vector<uint8_t> serialized(dataSize);
    original.toData(serialized.data());

    // Deserialize via RingBuffer
    RingBuffer buf(dataSize + 16);
    buf.write(serialized.data(), dataSize);

    auto deserialized = DriveInfo::deserialize(buf, dataSize);
    ASSERT(deserialized != nullptr);

    // Verify action preserved
    ASSERT(deserialized->markerInfo.action == MarkerAction::LifeDetected);

    // Verify marker
    ASSERT(deserialized->markerInfo.markers.size() == 1);
    const Marker& dm = deserialized->markerInfo.markers[0];
    ASSERT(dm.id == 65535);
    ASSERT(dm.projected[0].first == INT16_MIN);
    ASSERT(dm.projected[0].second == INT16_MAX);
    ASSERT(dm.projected[1].first == INT16_MAX);
    ASSERT(dm.projected[1].second == INT16_MIN);
    ASSERT(dm.projected[2].first == 0);
    ASSERT(dm.projected[2].second == 0);
    ASSERT(dm.projected[3].first == -1);
    ASSERT(dm.projected[3].second == 1);
}

int main() {
    printf("=== DriveInfo Round-Trip Tests ===\n");

    RUN_TEST(test_zero_markers_empty_frame);
    RUN_TEST(test_four_markers_with_frame);
    RUN_TEST(test_marker_action_and_edge_ids);

    TEST_SUMMARY();
}
