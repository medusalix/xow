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

#include "controller.h"
#include "../utils/log.h"

#include <cstdlib>
#include <cmath>
#include <linux/input.h>

// Configuration for the compatibility mode
#define COMPATIBILITY_ENV "XOW_COMPATIBILITY"
#define COMPATIBILITY_NAME "Microsoft X-Box 360 pad"
#define COMPATIBILITY_PID 0x028e
#define COMPATIBILITY_VERSION 0x0104

// Accessories use IDs greater than zero
#define DEVICE_ID_CONTROLLER 0
#define DEVICE_NAME "Xbox One Wireless Controller"

#define INPUT_STICK_FUZZ 255
#define INPUT_STICK_FLAT 4095
#define INPUT_TRIGGER_FUZZ 3
#define INPUT_TRIGGER_FLAT 63

#define RUMBLE_MAX_POWER 100
#define RUMBLE_DELAY std::chrono::milliseconds(10)

Controller::Controller(
    SendPacket sendPacket
) : GipDevice(sendPacket),
    inputDevice(std::bind(
        &Controller::inputFeedbackReceived,
        this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3
    )),
    stopRumbleThread(false) {}

Controller::~Controller()
{
    stopRumbleThread = true;
    rumbleCondition.notify_one();

    if (rumbleThread.joinable())
    {
        rumbleThread.join();
    }

    if (!setPowerMode(DEVICE_ID_CONTROLLER, POWER_OFF))
    {
        Log::error("Failed to turn off controller");
    }
}

void Controller::deviceAnnounced(uint8_t id, const AnnounceData *announce)
{
    Log::info("Device announced, product id: %04x", announce->productId);
    Log::debug(
        "Firmware version: %d.%d.%d.%d",
        announce->firmwareVersion.major,
        announce->firmwareVersion.minor,
        announce->firmwareVersion.build,
        announce->firmwareVersion.revision
    );
    Log::debug(
        "Hardware version: %d.%d.%d.%d",
        announce->hardwareVersion.major,
        announce->hardwareVersion.minor,
        announce->hardwareVersion.build,
        announce->hardwareVersion.revision
    );

    initInput(announce);
}

void Controller::statusReceived(uint8_t id, const StatusData *status)
{
    const std::string levels[] = { "empty", "low", "medium", "full" };

    uint8_t type = status->batteryType;
    uint8_t level = status->batteryLevel;

    // Controller is charging or level hasn't changed
    if (type == BATT_TYPE_CHARGING || level == batteryLevel)
    {
        return;
    }

    Log::info("Battery level: %s", levels[level].c_str());

    batteryLevel = level;
}

void Controller::guideButtonPressed(const GuideButtonData *button)
{
    inputDevice.setKey(BTN_MODE, button->pressed);
    inputDevice.report();
}

void Controller::serialNumberReceived(const SerialData *serial)
{
    const std::string number(
        serial->serialNumber,
        sizeof(serial->serialNumber)
    );

    Log::info("Serial number: %s", number.c_str());
}

void Controller::inputReceived(const InputData *input)
{
    inputDevice.setKey(BTN_START, input->buttons.start);
    inputDevice.setKey(BTN_SELECT, input->buttons.select);
    inputDevice.setKey(BTN_A, input->buttons.a);
    inputDevice.setKey(BTN_B, input->buttons.b);
    inputDevice.setKey(BTN_X, input->buttons.x);
    inputDevice.setKey(BTN_Y, input->buttons.y);
    inputDevice.setKey(BTN_TL, input->buttons.bumperLeft);
    inputDevice.setKey(BTN_TR, input->buttons.bumperRight);
    inputDevice.setKey(BTN_THUMBL, input->buttons.stickLeft);
    inputDevice.setKey(BTN_THUMBR, input->buttons.stickRight);
    inputDevice.setAxis(ABS_X, input->stickLeftX);
    inputDevice.setAxis(ABS_RX, input->stickRightX);
    inputDevice.setAxis(ABS_Y, ~input->stickLeftY);
    inputDevice.setAxis(ABS_RY, ~input->stickRightY);
    inputDevice.setAxis(ABS_Z, input->triggerLeft);
    inputDevice.setAxis(ABS_RZ, input->triggerRight);
    inputDevice.setAxis(
        ABS_HAT0X,
        input->buttons.dpadRight - input->buttons.dpadLeft
    );
    inputDevice.setAxis(
        ABS_HAT0Y,
        input->buttons.dpadDown - input->buttons.dpadUp
    );
    inputDevice.report();
}

void Controller::initInput(const AnnounceData *announce)
{
    LedModeData ledMode = {};

    // Dim the LED a little bit, like the original driver
    // Brightness ranges from 0x00 to 0x20
    ledMode.mode = LED_ON;
    ledMode.brightness = 0x14;

    if (!setPowerMode(DEVICE_ID_CONTROLLER, POWER_ON))
    {
        Log::error("Failed to set initial power mode");

        return;
    }

    if (!setLedMode(ledMode))
    {
        Log::error("Failed to set initial LED mode");

        return;
    }

    if (!requestSerialNumber())
    {
        Log::error("Failed to request serial number");

        return;
    }

    InputDevice::AxisConfig stickConfig = {};

    // 16 bits (signed) for the sticks
    stickConfig.minimum = -32768;
    stickConfig.maximum = 32767;
    stickConfig.fuzz = INPUT_STICK_FUZZ;
    stickConfig.flat = INPUT_STICK_FLAT;

    InputDevice::AxisConfig triggerConfig = {};

    // 10 bits (unsigned) for the triggers
    triggerConfig.minimum = 0;
    triggerConfig.maximum = 1023;
    triggerConfig.fuzz = INPUT_TRIGGER_FUZZ;
    triggerConfig.flat = INPUT_TRIGGER_FLAT;

    InputDevice::AxisConfig dpadConfig = {};

    // 1 bit for the DPAD buttons
    dpadConfig.minimum = -1;
    dpadConfig.maximum = 1;

    inputDevice.addKey(BTN_MODE);
    inputDevice.addKey(BTN_START);
    inputDevice.addKey(BTN_SELECT);
    inputDevice.addKey(BTN_A);
    inputDevice.addKey(BTN_B);
    inputDevice.addKey(BTN_X);
    inputDevice.addKey(BTN_Y);
    inputDevice.addKey(BTN_TL);
    inputDevice.addKey(BTN_TR);
    inputDevice.addKey(BTN_THUMBL);
    inputDevice.addKey(BTN_THUMBR);
    inputDevice.addAxis(ABS_X, stickConfig);
    inputDevice.addAxis(ABS_RX, stickConfig);
    inputDevice.addAxis(ABS_Y, stickConfig);
    inputDevice.addAxis(ABS_RY, stickConfig);
    inputDevice.addAxis(ABS_Z, triggerConfig);
    inputDevice.addAxis(ABS_RZ, triggerConfig);
    inputDevice.addAxis(ABS_HAT0X, dpadConfig);
    inputDevice.addAxis(ABS_HAT0Y, dpadConfig);
    inputDevice.addFeedback(FF_RUMBLE);

    InputDevice::DeviceConfig deviceConfig = {};

    deviceConfig.vendorId = announce->vendorId;

    if (std::getenv(COMPATIBILITY_ENV))
    {
        // Certain games compare the gamepad's name with a hardcoded value
        // Pretending to be an Xbox 360 controller fixes these problems
        deviceConfig.productId = COMPATIBILITY_PID;
        deviceConfig.version = COMPATIBILITY_VERSION;

        inputDevice.create(COMPATIBILITY_NAME, deviceConfig);
    }

    else
    {
        uint16_t version = (announce->firmwareVersion.major << 8) |
            announce->firmwareVersion.minor;

        deviceConfig.productId = announce->productId;
        deviceConfig.version = version;

        inputDevice.create(DEVICE_NAME, deviceConfig);
    }

    rumbleThread = std::thread(&Controller::processRumble, this);
}

void Controller::processRumble()
{
    RumbleData rumble = {};
    std::unique_lock<std::mutex> lock(rumbleMutex);

    while (!stopRumbleThread)
    {
        rumbleCondition.wait(lock);

        while (rumbleBuffer.get(rumble))
        {
            performRumble(rumble);

            // Delay rumble to work around firmware bug
            std::this_thread::sleep_for(RUMBLE_DELAY);
        }
    }
}

void Controller::inputFeedbackReceived(
    uint16_t gain,
    ff_effect effect,
    uint8_t replayCount
) {
    // Ignore other types of force feedback
    if (effect.type != FF_RUMBLE)
    {
        return;
    }

    Log::debug(
        "Rumble count: %d, duration: %d, delay: %d",
        replayCount,
        effect.replay.length,
        effect.replay.delay
    );

    RumbleData rumble = {};

    rumble.setRight = true;
    rumble.setLeft = true;
    rumble.setRightTrigger = true;
    rumble.setLeftTrigger = true;

    if ((replayCount > 0) && (gain > 0)) {
        Log::debug("Rumble strong: %d, weak: %d, direction: %d", effect.u.rumble.strong_magnitude,
                   effect.u.rumble.weak_magnitude, effect.direction);

        // Apply force feedback gain
        uint16_t weak = static_cast<uint32_t>(effect.u.rumble.strong_magnitude) * gain / 0xffff;
        uint16_t strong = static_cast<uint32_t>(effect.u.rumble.weak_magnitude) * gain / 0xffff;

        // Map effect's magnitudes to rumble power
        rumble.left = weak * RUMBLE_MAX_POWER / 0xffff;
        rumble.right = strong * RUMBLE_MAX_POWER / 0xffff;

        // Upper half of the controller (from left to right)
        if (effect.direction >= 0x4000 && effect.direction <= 0xc000) {
            // Angle shifted by an eighth of a full circle
            float angle = static_cast<float>(effect.direction) / 0xffff - 0.125;
            float left = sin(2 * M_PI * angle);
            float right = cos(2 * M_PI * angle);
            uint8_t maxPower = rumble.left > rumble.right ? rumble.left : rumble.right;

            // Limit values to left and right areas
            left = left > 0 ? left : 0;
            right = right < 0 ? -right : 0;

            // The trigger motors are very strong
            rumble.leftTrigger = left * maxPower / 2;
            rumble.rightTrigger = right * maxPower / 2;
        }

        uint16_t duration = effect.replay.length / 10;
        uint16_t delay = effect.replay.delay / 10;

        // Use maximum duration if not specified or out of range
        if (duration == 0x00 || duration > 0xff) {
            duration = 0xff;
        }

        if (delay > 0xff) {
            delay = 0xff;
        }

        // Time in multiples of 10 ms
        rumble.duration = duration;
        rumble.delay = delay;
        rumble.repeat = replayCount - 1;
    }
    rumbleBuffer.put(rumble);
    rumbleCondition.notify_one();
}
