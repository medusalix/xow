/*
 * Copyright (C) 2019 Medusalix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include "input.h"

#include <cstdint>
#include <functional>
#include <string>
#include <stdexcept>

enum FrameCommand
{
    CMD_ACK = 0x01,
    CMD_STATUS = 0x03,
    CMD_POWER_MODE = 0x05,
    CMD_GUIDE_BTN = 0x07,
    CMD_RUMBLE = 0x09,
    CMD_LED_MODE = 0x0a,
    CMD_SERIAL_NUM = 0x1e,
    CMD_INPUT = 0x20,
};

// Different frame types
// Command: controller doesn't respond
// Request: controller responds with data
// Request (ACK): controller responds with ack + data
enum FrameType
{
    TYPE_COMMAND = 0x00,
    TYPE_REQUEST = 0x20,
    TYPE_REQUEST_ACK = 0x30,
};

// Controller input can be paused temporarily
enum PowerMode
{
    POWER_ON = 0x00,
    POWER_SLEEP = 0x01,
    POWER_OFF = 0x04,
};

enum LedMode
{
    LED_OFF = 0x00,
    LED_ON = 0x01,
    LED_BLINK_FAST = 0x02,
    LED_BLINK_MED = 0x03,
    LED_BLINK_SLOW = 0x04,
    LED_FADE_SLOW = 0x08,
    LED_FADE_FAST = 0x09,
};

enum BatteryType
{
    BATT_TYPE_ALKALINE = 0x01,
    BATT_TYPE_NIMH = 0x02,
};

enum BatteryLevel
{
    BATT_LEVEL_EMPTY = 0x00,
    BATT_LEVEL_LOW = 0x01,
    BATT_LEVEL_MED = 0x02,
    BATT_LEVEL_HIGH = 0x03,
};

enum RumbleMotors
{
    RUMBLE_RIGHT = 0x01,
    RUMBLE_LEFT = 0x02,
    RUMBLE_LT = 0x04,
    RUMBLE_RT = 0x08,
    RUMBLE_ALL = 0x0f,
};

struct SerialData
{
    uint16_t unknown;
    char serialNumber[14];
} __attribute__((packed));

struct LedModeData
{
    uint8_t unknown;
    uint8_t mode;
    uint8_t brightness;
} __attribute__((packed));

struct StatusData
{
    uint32_t batteryLevel : 2;
    uint32_t batteryType : 2;
    uint32_t connectionInfo : 4;
    uint8_t unknown1;
    uint16_t unknown2;
} __attribute__((packed));

struct Buttons
{
    uint32_t unknown : 2;
    uint32_t start : 1;
    uint32_t select : 1;
    uint32_t a : 1;
    uint32_t b : 1;
    uint32_t x : 1;
    uint32_t y : 1;
    uint32_t dpadUp : 1;
    uint32_t dpadDown : 1;
    uint32_t dpadLeft : 1;
    uint32_t dpadRight : 1;
    uint32_t bumperLeft : 1;
    uint32_t bumperRight : 1;
    uint32_t stickLeft : 1;
    uint32_t stickRight : 1;
} __attribute__((packed));

struct InputData
{
    Buttons buttons;
    uint16_t triggerLeft;
    uint16_t triggerRight;
    int16_t stickLeftX;
    int16_t stickLeftY;
    int16_t stickRightX;
    int16_t stickRightY;
} __attribute__((packed));

struct GuideButtonData
{
    uint8_t pressed;
    uint8_t unknown;
} __attribute__((packed));

struct RumbleData
{
    uint8_t unknown1;
    uint8_t motors;
    uint8_t triggerLeft;
    uint8_t triggerRight;
    uint8_t left;
    uint8_t right;
    uint8_t duration;
    uint8_t delay;
    uint8_t repeat;
} __attribute__((packed));

struct ControllerFrame
{
    uint8_t command;
    uint8_t type;
    uint8_t sequence;
    uint8_t length;
} __attribute__((packed));

class Bytes;

/*
 * Implements the GIP (Game Input Protocol)
 * Maps input packets to Linux joystick events
 */
class Controller : private InputDevice
{
private:
    using SendPacket = std::function<bool(const Bytes &data)>;

public:
    Controller(SendPacket sendPacket);

    void packetReceived(const Bytes &packet);

private:
    void feedbackReceived(
        ff_effect effect,
        uint16_t gain
    ) override;

    void reportInput(const InputData *input);

    bool acknowledgePacket(const ControllerFrame *packet = nullptr);
    bool requestSerialNumber();
    bool setPowerMode(PowerMode mode);
    bool setLedMode(LedModeData data);
    bool performRumble(RumbleData data);

    SendPacket sendPacket;
    bool rumbling = false;
};

class ControllerException : public std::runtime_error
{
public:
    ControllerException(std::string message);
};
