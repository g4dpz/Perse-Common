#ifndef PERSE_COMMON_COMM_H
#define PERSE_COMMON_COMM_H

#include <cstdint>

enum class CommType : uint8_t {
	None,
	DriveDir,
	Headlights,
	ArmPosition,
	ArmPinch,
	CameraRotation,
	Battery,
	FeedQuality,
	ModulePlug,
	ModuleData,
	ModulesEnable,
	ScanMarkers,
	Emergency,
	NoFeed,
	Audio,
	ArmControl,
	ControllerBatteryCritical,
	ConnectionStrength,
	Heartbeat
};

enum class ConnectionStrength : uint8_t {
	None = 4,
	VeryLow = 3,
	Low = 2,
	Medium = 1,
	High = 0
};

enum class ModuleType : uint8_t {
	TempHum, Gyro, AltPress, LED, RGB, PhotoRes, Motion, CO2, Unknown
};
enum class ModuleBus : uint8_t {
	Left = 0, Right = 1
};

struct ModuleData {
	ModuleType type;
	ModuleBus bus;
	union {
		struct {
			int16_t x;
			int16_t y;
		} gyro;

		struct {
			int16_t temp;
			uint16_t humidity;
		} tempHum;

		struct {
			int16_t altitude;
			uint16_t pressure;
		} altPress;

		struct {
			uint8_t isOk; //boolean
		} gas;

		struct {
			uint8_t motionDetected; //boolean
		} motion;

		struct {
			uint8_t level;
		} photoRes;

		struct {
			bool on;
		} ledState;
	};
};

struct ModulePlugData {
	ModuleType type;
	ModuleBus bus;
	bool insert;
};

struct ControlPacket {
	CommType type;
	uint8_t data;
};

struct DriveDir {
	uint8_t dir = 0; //0-7
	float speed = 0.0f; //0 - 1.0
};

enum class HeadlightsMode : uint8_t {
	Off,
	On
};

typedef int8_t ArmPos;
typedef int8_t ArmPinch;
typedef uint8_t CameraRotation;

namespace CommData {
	uint8_t encodeDriveDir(DriveDir dir);
	DriveDir decodeDriveDir(uint8_t raw);

	uint8_t encodeModulePlug(ModulePlugData plugData);
	ModulePlugData decodeModulePlug(uint8_t data);
}

#endif //PERSE_COMMON_COMM_H
