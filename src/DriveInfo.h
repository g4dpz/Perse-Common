#ifndef PERSE_ROVER_DRIVEINFO_H
#define PERSE_ROVER_DRIVEINFO_H

#include "RingBuffer.h"
#include "MarkerInfo.h"
#include <vector>
#include <memory>

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

	virtual ~DriveInfo();

	static constexpr size_t baseSize = sizeof(CamFrame::size) + sizeof(MarkerInfo::action) + sizeof(size_t);

	/**
	 * Returns size of struct in binary form, including all elements and sub-elements.
	 * Note: data size varies by DriveMode.
	 * @return Struct size.
	 */
	virtual size_t size() const;

	/**
	 * Serializes data to destination buffer. Buffer must be at least DriveInfo::size() bytes long.
	 * @param dest Destination buffer
	 */
	void toData(void* dest) const;

	/**
	 * Deserializes data from a RingBuffer to a DriveInfo struct.
	 * Buffer is expected to have the binary data on the first byte, and contains 'size' bytes of binary DriveInfo data.
	 * @param buf RingBuffer containing the binary data, starting from the first byte.
	 * @param size Size of DriveInfo binary data (bytes from header to trailer in a UDP packet)
	 * @return unique_ptr to a DriveInfo struct
	 */
	static std::unique_ptr<DriveInfo> deserialize(RingBuffer& buf, size_t size);
};

#define FEED_ENV_LEN 8
const extern uint8_t FrameHeader[FEED_ENV_LEN];
const extern uint8_t FrameTrailer[FEED_ENV_LEN];
const extern uint8_t FrameSizeShift[4];

// Simple CRC8 (polynomial 0x31, init 0xFF)
inline uint8_t crc8(const uint8_t* data, size_t len) {
	uint8_t crc = 0xFF;
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int j = 0; j < 8; j++) {
			crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
		}
	}
	return crc;
}

static constexpr size_t FrameCRCSize = 1;

#endif //PERSE_ROVER_DRIVEINFO_H
