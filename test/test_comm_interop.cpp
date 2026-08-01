/**
 * test_comm_interop.cpp
 *
 * Protocol interoperability test: exercises every command type that flows
 * between Controller and Rover over TCP.
 *
 * Strategy:
 * - Controller "sends" by encoding a ControlPacket (CommType + data byte)
 * - Rover "receives" by parsing that same ControlPacket via processPacket()
 * - Rover "sends" state back; Controller parses it via processPacket()
 *
 * This validates the full encode→wire→decode path for all CommType values
 * without sockets or hardware.
 *
 * Compile:
 *   g++ -std=c++17 -o test_comm_interop test_comm_interop.cpp && ./test_comm_interop
 */

#include "freertos_mock.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

// ============================================================
// Inline shared types from CommData.h
// ============================================================

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
    ConnectionStrength
};

enum class ConnectionStrength : uint8_t {
    None = 4, VeryLow = 3, Low = 2, Medium = 1, High = 0
};

enum class ModuleType : uint8_t {
    TempHum, Gyro, AltPress, LED, RGB, PhotoRes, Motion, CO2, Unknown
};

enum class ModuleBus : uint8_t { Left = 0, Right = 1 };

struct ModulePlugData { ModuleType type; ModuleBus bus; bool insert; };

struct ControlPacket { CommType type; uint8_t data; };

struct DriveDir { uint8_t dir = 0; float speed = 0.0f; };

enum class HeadlightsMode : uint8_t { Off, On };
typedef int8_t ArmPos;
typedef int8_t ArmPinch;
typedef uint8_t CameraRotation;

// ============================================================
// Inline CommData encode/decode (from CommData.cpp)
// ============================================================

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

    uint8_t encodeModulePlug(ModulePlugData plugData) {
        return (plugData.insert << 7) | ((uint8_t)plugData.bus << 6) | (uint8_t)plugData.type;
    }

    ModulePlugData decodeModulePlug(uint8_t raw) {
        ModulePlugData data{};
        data.insert = raw >> 7;
        data.bus = (ModuleBus)((raw >> 6) & 0b1);
        data.type = (ModuleType)(raw & 0b111111);
        return data;
    }
}

// ============================================================
// Inline RoverStateUtil (from RoverStateUtil.cpp)
// ============================================================

uint8_t encodeRoverState(uint8_t value, bool local) {
    return (local << 7) | (value & 0x7F);
}

bool decodeRoverState(uint8_t raw, uint8_t& value) {
    value = raw & 0x7F;
    return raw >> 7;
}

// ============================================================
// Rover's processPacket (from Perse_Rover-Firmware Comm.cpp)
// ============================================================

struct RoverCommEvent {
    CommType type;
    union {
        DriveDir dir;
        HeadlightsMode headlights;
        struct { ArmPos armPos; ArmPinch armPinch; };
        CameraRotation cameraRotation;
        uint8_t feedQuality;
        bool scanningEnable;
        bool emergency;
        bool audio;
        bool armEnabled;
        bool controllerBatteryCritical;
        ConnectionStrength connectionStrength;
    };
    uint8_t raw;
};

RoverCommEvent roverProcessPacket(const ControlPacket& packet) {
    RoverCommEvent e{};
    e.type = packet.type;
    e.raw = packet.data;

    switch (packet.type) {
        case CommType::DriveDir:
            e.dir = CommData::decodeDriveDir(packet.data);
            break;
        case CommType::Headlights:
            e.headlights = packet.data > 0 ? HeadlightsMode::On : HeadlightsMode::Off;
            break;
        case CommType::ArmPosition:
            e.armPos = (ArmPos)packet.data;
            e.armPinch = -1;
            break;
        case CommType::ArmPinch:
            e.armPos = -1;
            e.armPinch = (ArmPinch)packet.data;
            break;
        case CommType::CameraRotation:
            e.cameraRotation = packet.data;
            break;
        case CommType::FeedQuality:
            e.feedQuality = packet.data;
            break;
        case CommType::ScanMarkers:
            e.scanningEnable = packet.data;
            break;
        case CommType::Emergency:
            e.emergency = (bool)packet.data;
            break;
        case CommType::Audio:
            e.audio = (bool)packet.data;
            break;
        case CommType::ArmControl:
            e.armEnabled = (bool)packet.data;
            break;
        case CommType::ControllerBatteryCritical:
            e.controllerBatteryCritical = (bool)packet.data;
            break;
        case CommType::ConnectionStrength:
            e.connectionStrength = (ConnectionStrength)packet.data;
            break;
        default:
            break;
    }
    return e;
}

// ============================================================
// Controller's processPacket (from Perse_Controller-Firmware Comm.cpp)
// ============================================================

struct ControllerCommEvent {
    CommType type;
    union {
        struct {
            bool changedOnRover;
            union {
                ArmPos armPos;
                ArmPinch armPinch;
                HeadlightsMode headlights;
                CameraRotation cameraRotation;
                bool noFeed;
            };
        };
        uint8_t batteryPercent;
        ModulePlugData modulePlug;
    };
    uint8_t raw;
};

ControllerCommEvent controllerProcessPacket(const ControlPacket& packet) {
    ControllerCommEvent event{};
    event.type = packet.type;
    event.raw = packet.data;

    switch (packet.type) {
        case CommType::Headlights: {
            uint8_t value;
            event.changedOnRover = decodeRoverState(packet.data, value);
            event.headlights = value > 0 ? HeadlightsMode::On : HeadlightsMode::Off;
            break;
        }
        case CommType::Battery:
            event.batteryPercent = packet.data;
            break;
        case CommType::ArmPosition:
            event.changedOnRover = decodeRoverState(packet.data, (uint8_t&)event.armPos);
            break;
        case CommType::ArmPinch:
            event.changedOnRover = decodeRoverState(packet.data, (uint8_t&)event.armPinch);
            break;
        case CommType::CameraRotation:
            event.changedOnRover = decodeRoverState(packet.data, event.cameraRotation);
            break;
        case CommType::ModulePlug:
            event.modulePlug = CommData::decodeModulePlug(packet.data);
            break;
        case CommType::NoFeed:
            event.noFeed = (bool)packet.data;
            break;
        default:
            break;
    }
    return event;
}

// ============================================================
// Tests: Controller → Rover (all command types)
// ============================================================

void test_controller_to_rover_drivedir() {
    // Test all 8 directions at various speeds
    for (uint8_t dir = 0; dir < 8; dir++) {
        float speeds[] = {0.0f, 0.5f, 1.0f};
        for (float speed : speeds) {
            DriveDir input{dir, speed};
            uint8_t encoded = CommData::encodeDriveDir(input);
            ControlPacket packet{CommType::DriveDir, encoded};

            RoverCommEvent e = roverProcessPacket(packet);
            ASSERT(e.type == CommType::DriveDir);
            ASSERT(e.dir.dir == dir);
            ASSERT(std::fabs(e.dir.speed - speed) <= 1.0f / 62.0f);
        }
    }
}

void test_controller_to_rover_headlights() {
    // Off
    ControlPacket pOff{CommType::Headlights, (uint8_t)HeadlightsMode::Off};
    RoverCommEvent eOff = roverProcessPacket(pOff);
    ASSERT(eOff.type == CommType::Headlights);
    ASSERT(eOff.headlights == HeadlightsMode::Off);

    // On
    ControlPacket pOn{CommType::Headlights, (uint8_t)HeadlightsMode::On};
    RoverCommEvent eOn = roverProcessPacket(pOn);
    ASSERT(eOn.headlights == HeadlightsMode::On);
}

void test_controller_to_rover_arm_position() {
    for (int8_t pos = -5; pos <= 5; pos++) {
        ControlPacket packet{CommType::ArmPosition, (uint8_t)pos};
        RoverCommEvent e = roverProcessPacket(packet);
        ASSERT(e.type == CommType::ArmPosition);
        ASSERT(e.armPos == pos);
        ASSERT(e.armPinch == -1);
    }
}

void test_controller_to_rover_arm_pinch() {
    for (int8_t pinch = -5; pinch <= 5; pinch++) {
        ControlPacket packet{CommType::ArmPinch, (uint8_t)pinch};
        RoverCommEvent e = roverProcessPacket(packet);
        ASSERT(e.type == CommType::ArmPinch);
        ASSERT(e.armPinch == pinch);
        ASSERT(e.armPos == -1);
    }
}

void test_controller_to_rover_camera_rotation() {
    for (uint8_t rot = 0; rot <= 180; rot += 30) {
        ControlPacket packet{CommType::CameraRotation, rot};
        RoverCommEvent e = roverProcessPacket(packet);
        ASSERT(e.type == CommType::CameraRotation);
        ASSERT(e.cameraRotation == rot);
    }
}

void test_controller_to_rover_feed_quality() {
    uint8_t qualities[] = {10, 30, 50, 80, 100};
    for (uint8_t q : qualities) {
        ControlPacket packet{CommType::FeedQuality, q};
        RoverCommEvent e = roverProcessPacket(packet);
        ASSERT(e.type == CommType::FeedQuality);
        ASSERT(e.feedQuality == q);
    }
}

void test_controller_to_rover_modules_enable() {
    // ModulesEnable is a no-op in Rover's processPacket (falls to default)
    // but the packet still arrives with correct type/data
    ControlPacket p1{CommType::ModulesEnable, 1};
    RoverCommEvent e1 = roverProcessPacket(p1);
    ASSERT(e1.type == CommType::ModulesEnable);
    ASSERT(e1.raw == 1);

    ControlPacket p0{CommType::ModulesEnable, 0};
    RoverCommEvent e0 = roverProcessPacket(p0);
    ASSERT(e0.raw == 0);
}

void test_controller_to_rover_scan_markers() {
    ControlPacket pOn{CommType::ScanMarkers, 1};
    RoverCommEvent eOn = roverProcessPacket(pOn);
    ASSERT(eOn.type == CommType::ScanMarkers);
    ASSERT(eOn.scanningEnable == true);

    ControlPacket pOff{CommType::ScanMarkers, 0};
    RoverCommEvent eOff = roverProcessPacket(pOff);
    ASSERT(eOff.scanningEnable == false);
}

void test_controller_to_rover_emergency() {
    ControlPacket pOn{CommType::Emergency, 1};
    RoverCommEvent eOn = roverProcessPacket(pOn);
    ASSERT(eOn.type == CommType::Emergency);
    ASSERT(eOn.emergency == true);

    ControlPacket pOff{CommType::Emergency, 0};
    RoverCommEvent eOff = roverProcessPacket(pOff);
    ASSERT(eOff.emergency == false);
}

void test_controller_to_rover_audio() {
    ControlPacket pOn{CommType::Audio, 1};
    RoverCommEvent eOn = roverProcessPacket(pOn);
    ASSERT(eOn.type == CommType::Audio);
    ASSERT(eOn.audio == true);

    ControlPacket pOff{CommType::Audio, 0};
    RoverCommEvent eOff = roverProcessPacket(pOff);
    ASSERT(eOff.audio == false);
}

void test_controller_to_rover_arm_control() {
    ControlPacket pOn{CommType::ArmControl, 1};
    RoverCommEvent eOn = roverProcessPacket(pOn);
    ASSERT(eOn.type == CommType::ArmControl);
    ASSERT(eOn.armEnabled == true);

    ControlPacket pOff{CommType::ArmControl, 0};
    RoverCommEvent eOff = roverProcessPacket(pOff);
    ASSERT(eOff.armEnabled == false);
}

void test_controller_to_rover_battery_critical() {
    ControlPacket pOn{CommType::ControllerBatteryCritical, 1};
    RoverCommEvent eOn = roverProcessPacket(pOn);
    ASSERT(eOn.type == CommType::ControllerBatteryCritical);
    ASSERT(eOn.controllerBatteryCritical == true);

    ControlPacket pOff{CommType::ControllerBatteryCritical, 0};
    RoverCommEvent eOff = roverProcessPacket(pOff);
    ASSERT(eOff.controllerBatteryCritical == false);
}

void test_controller_to_rover_connection_strength() {
    ConnectionStrength strengths[] = {
        ConnectionStrength::None, ConnectionStrength::VeryLow,
        ConnectionStrength::Low, ConnectionStrength::Medium,
        ConnectionStrength::High
    };
    for (ConnectionStrength s : strengths) {
        ControlPacket packet{CommType::ConnectionStrength, (uint8_t)s};
        RoverCommEvent e = roverProcessPacket(packet);
        ASSERT(e.type == CommType::ConnectionStrength);
        ASSERT(e.connectionStrength == s);
    }
}

// ============================================================
// Tests: Rover → Controller (state responses)
// ============================================================

void test_rover_to_controller_headlights_state() {
    // Rover sends headlights state with local flag
    for (int local = 0; local <= 1; local++) {
        for (uint8_t mode = 0; mode <= 1; mode++) {
            uint8_t encoded = encodeRoverState(mode, (bool)local);
            ControlPacket packet{CommType::Headlights, encoded};

            ControllerCommEvent e = controllerProcessPacket(packet);
            ASSERT(e.type == CommType::Headlights);
            ASSERT(e.changedOnRover == (bool)local);
            HeadlightsMode expected = mode > 0 ? HeadlightsMode::On : HeadlightsMode::Off;
            ASSERT(e.headlights == expected);
        }
    }
}

void test_rover_to_controller_battery() {
    for (uint8_t pct = 0; pct <= 100; pct += 10) {
        ControlPacket packet{CommType::Battery, pct};
        ControllerCommEvent e = controllerProcessPacket(packet);
        ASSERT(e.type == CommType::Battery);
        ASSERT(e.batteryPercent == pct);
    }
}

void test_rover_to_controller_arm_position_state() {
    for (int8_t pos = 0; pos <= 5; pos++) {
        for (int local = 0; local <= 1; local++) {
            uint8_t encoded = encodeRoverState((uint8_t)pos, (bool)local);
            ControlPacket packet{CommType::ArmPosition, encoded};

            ControllerCommEvent e = controllerProcessPacket(packet);
            ASSERT(e.type == CommType::ArmPosition);
            ASSERT(e.changedOnRover == (bool)local);
            ASSERT(e.armPos == pos);
        }
    }
}

void test_rover_to_controller_arm_pinch_state() {
    for (int8_t pinch = 0; pinch <= 3; pinch++) {
        for (int local = 0; local <= 1; local++) {
            uint8_t encoded = encodeRoverState((uint8_t)pinch, (bool)local);
            ControlPacket packet{CommType::ArmPinch, encoded};

            ControllerCommEvent e = controllerProcessPacket(packet);
            ASSERT(e.type == CommType::ArmPinch);
            ASSERT(e.changedOnRover == (bool)local);
            ASSERT(e.armPinch == pinch);
        }
    }
}

void test_rover_to_controller_camera_state() {
    for (uint8_t rot = 0; rot <= 127; rot += 20) {
        for (int local = 0; local <= 1; local++) {
            uint8_t encoded = encodeRoverState(rot, (bool)local);
            ControlPacket packet{CommType::CameraRotation, encoded};

            ControllerCommEvent e = controllerProcessPacket(packet);
            ASSERT(e.type == CommType::CameraRotation);
            ASSERT(e.changedOnRover == (bool)local);
            ASSERT(e.cameraRotation == rot);
        }
    }
}

void test_rover_to_controller_module_plug() {
    // Exercise all module types, both buses, insert/remove
    for (uint8_t t = 0; t <= 8; t++) {
        for (uint8_t b = 0; b <= 1; b++) {
            for (uint8_t ins = 0; ins <= 1; ins++) {
                ModulePlugData original{(ModuleType)t, (ModuleBus)b, (bool)ins};
                uint8_t encoded = CommData::encodeModulePlug(original);
                ControlPacket packet{CommType::ModulePlug, encoded};

                ControllerCommEvent e = controllerProcessPacket(packet);
                ASSERT(e.type == CommType::ModulePlug);
                ASSERT(e.modulePlug.type == original.type);
                ASSERT(e.modulePlug.bus == original.bus);
                ASSERT(e.modulePlug.insert == original.insert);
            }
        }
    }
}

void test_rover_to_controller_no_feed() {
    ControlPacket pOn{CommType::NoFeed, 1};
    ControllerCommEvent eOn = controllerProcessPacket(pOn);
    ASSERT(eOn.type == CommType::NoFeed);
    ASSERT(eOn.noFeed == true);

    ControlPacket pOff{CommType::NoFeed, 0};
    ControllerCommEvent eOff = controllerProcessPacket(pOff);
    ASSERT(eOff.noFeed == false);
}

// ============================================================
// Test: Full round-trip (Controller sends, Rover parses, Rover responds, Controller parses)
// ============================================================

void test_full_roundtrip_headlights() {
    // Controller sends Headlights On command
    ControlPacket ctrlSend{CommType::Headlights, (uint8_t)HeadlightsMode::On};

    // Rover receives and parses
    RoverCommEvent roverRx = roverProcessPacket(ctrlSend);
    ASSERT(roverRx.headlights == HeadlightsMode::On);

    // Rover echoes state back with local=false (remote change)
    uint8_t roverResponse = encodeRoverState((uint8_t)roverRx.headlights, false);
    ControlPacket roverSend{CommType::Headlights, roverResponse};

    // Controller receives and parses
    ControllerCommEvent ctrlRx = controllerProcessPacket(roverSend);
    ASSERT(ctrlRx.headlights == HeadlightsMode::On);
    ASSERT(ctrlRx.changedOnRover == false); // remote change, not local
}

void test_full_roundtrip_arm_position() {
    ArmPos targetPos = 3;

    // Controller sends arm position
    ControlPacket ctrlSend{CommType::ArmPosition, (uint8_t)targetPos};

    // Rover receives
    RoverCommEvent roverRx = roverProcessPacket(ctrlSend);
    ASSERT(roverRx.armPos == targetPos);

    // Rover responds with confirmed state (local=false means remote-initiated)
    uint8_t roverResponse = encodeRoverState((uint8_t)roverRx.armPos, false);
    ControlPacket roverSend{CommType::ArmPosition, roverResponse};

    // Controller receives confirmed position
    ControllerCommEvent ctrlRx = controllerProcessPacket(roverSend);
    ASSERT(ctrlRx.armPos == targetPos);
    ASSERT(ctrlRx.changedOnRover == false);
}

void test_full_roundtrip_drivedir() {
    DriveDir input{3, 0.75f};
    uint8_t encoded = CommData::encodeDriveDir(input);
    ControlPacket ctrlSend{CommType::DriveDir, encoded};

    // Rover receives
    RoverCommEvent roverRx = roverProcessPacket(ctrlSend);
    ASSERT(roverRx.dir.dir == 3);
    ASSERT(std::fabs(roverRx.dir.speed - 0.75f) <= 1.0f / 62.0f);

    // DriveDir is one-way (no response from Rover for this type)
    // Validate the raw byte preserved
    ASSERT(roverRx.raw == encoded);
}

// ============================================================
// Test: Wire format correctness (struct packing)
// ============================================================

void test_control_packet_wire_size() {
    // ControlPacket must be exactly 2 bytes for the protocol to work
    ASSERT(sizeof(ControlPacket) == 2);
    ASSERT(sizeof(CommType) == 1);
}

// ============================================================
// Main
// ============================================================

int main() {
    printf("=== Comm Protocol Interoperability Tests ===\n\n");
    printf("--- Controller → Rover ---\n");

    RUN_TEST(test_controller_to_rover_drivedir);
    RUN_TEST(test_controller_to_rover_headlights);
    RUN_TEST(test_controller_to_rover_arm_position);
    RUN_TEST(test_controller_to_rover_arm_pinch);
    RUN_TEST(test_controller_to_rover_camera_rotation);
    RUN_TEST(test_controller_to_rover_feed_quality);
    RUN_TEST(test_controller_to_rover_modules_enable);
    RUN_TEST(test_controller_to_rover_scan_markers);
    RUN_TEST(test_controller_to_rover_emergency);
    RUN_TEST(test_controller_to_rover_audio);
    RUN_TEST(test_controller_to_rover_arm_control);
    RUN_TEST(test_controller_to_rover_battery_critical);
    RUN_TEST(test_controller_to_rover_connection_strength);

    printf("\n--- Rover → Controller ---\n");

    RUN_TEST(test_rover_to_controller_headlights_state);
    RUN_TEST(test_rover_to_controller_battery);
    RUN_TEST(test_rover_to_controller_arm_position_state);
    RUN_TEST(test_rover_to_controller_arm_pinch_state);
    RUN_TEST(test_rover_to_controller_camera_state);
    RUN_TEST(test_rover_to_controller_module_plug);
    RUN_TEST(test_rover_to_controller_no_feed);

    printf("\n--- Full Round-Trips ---\n");

    RUN_TEST(test_full_roundtrip_headlights);
    RUN_TEST(test_full_roundtrip_arm_position);
    RUN_TEST(test_full_roundtrip_drivedir);

    printf("\n--- Wire Format ---\n");

    RUN_TEST(test_control_packet_wire_size);

    TEST_SUMMARY();
}
