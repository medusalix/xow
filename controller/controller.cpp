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
#include "../utils/bytes.h"

#include <linux/input.h>

// Hardware ID for the Xbox One S controller
#define CONTROLLER_VID 0x045e
#define CONTROLLER_PID 0x02ea
#define CONTROLLER_NAME "Xbox One Wireless Controller"

// 16 bits (signed) for the stick
#define STICK_MIN -32768
#define STICK_MAX 32767

// 10 bits (unsigned) for the trigger
#define TRIGGER_MIN 0
#define TRIGGER_MAX 1023

Controller::Controller(SendPacket sendPacket) : sendPacket(sendPacket)
{
    if (!acknowledgePacket() || !setPowerMode(POWER_ON))
    {
        throw ControllerException("Failed to execute handshake");
    }

    LedModeData ledMode = {};

    // Dim the LED a little bit, like the original driver
    // Brightness ranges from 0x00 to 0x20
    ledMode.mode = LED_ON;
    ledMode.brightness = 0x14;

    if (!setLedMode(ledMode))
    {
        throw ControllerException("Failed to set LED mode");
    }

    if (!requestSerialNumber())
    {
        throw ControllerException("Failed to request serial number");
    }

    addKey(BTN_MODE);
    addKey(BTN_START);
    addKey(BTN_SELECT);
    addKey(BTN_NORTH);
    addKey(BTN_EAST);
    addKey(BTN_SOUTH);
    addKey(BTN_WEST);
    addKey(BTN_DPAD_UP);
    addKey(BTN_DPAD_DOWN);
    addKey(BTN_DPAD_LEFT);
    addKey(BTN_DPAD_RIGHT);
    addKey(BTN_TL);
    addKey(BTN_TR);
    addKey(BTN_THUMBL);
    addKey(BTN_THUMBR);
    addAxis(ABS_X, STICK_MIN, STICK_MAX);
    addAxis(ABS_RX, STICK_MIN, STICK_MAX);
    addAxis(ABS_Y, STICK_MIN, STICK_MAX);
    addAxis(ABS_RY, STICK_MIN, STICK_MAX);
    addAxis(ABS_Z, TRIGGER_MIN, TRIGGER_MAX);
    addAxis(ABS_RZ, TRIGGER_MIN, TRIGGER_MAX);
    addFeedback(FF_RUMBLE);
    create(CONTROLLER_VID, CONTROLLER_PID, CONTROLLER_NAME);
    readEvents();
}

void Controller::feedbackReceived(ff_effect effect, uint8_t gain)
{
    RumbleData rumble = {};

    uint8_t weak = static_cast<uint8_t>(effect.u.rumble.weak_magnitude >> 8);
    uint8_t strong = static_cast<uint8_t>(effect.u.rumble.strong_magnitude >> 8);

    // Scale magnitudes with gain
    weak *= gain * 0.01;
    strong *= gain * 0.01;

    Log::debug(
        "Feedback length: %d, delay: %d, weak: %d, strong: %d",
        effect.replay.length,
        effect.replay.delay,
        weak,
        strong
    );

    rumble.motors = RUMBLE_ALL;
    rumble.left = strong;
    rumble.right = weak;
    rumble.triggerLeft = strong;
    rumble.triggerRight = weak;
    rumble.duration = 0xff;

    performRumble(rumble);
}

void Controller::reportInput(const InputData *input)
{
    setKey(BTN_START, input->buttons.start);
    setKey(BTN_SELECT, input->buttons.select);
    setKey(BTN_NORTH, input->buttons.y);
    setKey(BTN_EAST, input->buttons.b);
    setKey(BTN_SOUTH, input->buttons.a);
    setKey(BTN_WEST, input->buttons.x);
    setKey(BTN_DPAD_UP, input->buttons.dpadUp);
    setKey(BTN_DPAD_DOWN, input->buttons.dpadDown);
    setKey(BTN_DPAD_LEFT, input->buttons.dpadLeft);
    setKey(BTN_DPAD_RIGHT, input->buttons.dpadRight);
    setKey(BTN_TL, input->buttons.bumperLeft);
    setKey(BTN_TR, input->buttons.bumperRight);
    setKey(BTN_THUMBL, input->buttons.stickLeft);
    setKey(BTN_THUMBR, input->buttons.stickRight);
    setAxis(ABS_X, input->stickLeftX);
    setAxis(ABS_RX, input->stickRightX);
    setAxis(ABS_Y, ~input->stickLeftY);
    setAxis(ABS_RY, ~input->stickRightY);
    setAxis(ABS_Z, input->triggerLeft);
    setAxis(ABS_RZ, input->triggerRight);
    report();
}

void Controller::packetReceived(const Bytes &packet)
{
    const ControllerFrame *frame = packet.toStruct<ControllerFrame>();

    if (
        frame->command == CMD_SERIAL_NUM &&
        frame->length == sizeof(SerialData)
    ) {
        if (!acknowledgePacket(*frame))
        {
            Log::error("Failed to acknowledge serial number packet");

            return;
        }

        const SerialData *serial = packet.toStruct<SerialData>(
            sizeof(ControllerFrame)
        );
        const std::string number(
            serial->serialNumber,
            sizeof(serial->serialNumber)
        );

        Log::info("Serial number: %s", number.c_str());
    }

    if (
        frame->command == CMD_STATUS &&
        frame->length == sizeof(StatusData)
    ) {
        const StatusData *status = packet.toStruct<StatusData>(
            sizeof(ControllerFrame)
        );

        Log::debug(
            "Battery type: %d, level: %d",
            status->batteryType,
            status->batteryLevel
        );
    }

    else if (
        frame->command == CMD_INPUT &&
        frame->length == sizeof(InputData)
    ) {
        const InputData *input = packet.toStruct<InputData>(
            sizeof(ControllerFrame)
        );

        reportInput(input);
    }

    else if (
        frame->command == CMD_GUIDE_BTN &&
        frame->length == sizeof(GuideButtonData)
    ) {
        if (!acknowledgePacket(*frame))
        {
            Log::error("Failed to acknowledge guide button packet");

            return;
        }

        const GuideButtonData *button = packet.toStruct<GuideButtonData>(
            sizeof(ControllerFrame)
        );

        setKey(BTN_MODE, button->pressed);
        report();
    }
}

bool Controller::acknowledgePacket(
    std::optional<ControllerFrame> packet
) {
    ControllerFrame frame = {};

    frame.command = CMD_ACK;
    frame.type = TYPE_REQUEST;

    Bytes out;

    // Acknowledge empty frame
    if (!packet)
    {
        out.append(frame);

        return sendPacket(out);
    }

    ControllerFrame innerFrame = {};

    // Acknowledgement includes the received frame
    innerFrame.type = packet->command;
    innerFrame.sequence = TYPE_REQUEST;
    innerFrame.length = packet->length;

    frame.sequence = packet->sequence;
    frame.length = sizeof(innerFrame) + 5;

    out.append(frame);
    out.append(innerFrame);
    out.pad(5);

    return sendPacket(out);
}

bool Controller::requestSerialNumber()
{
    ControllerFrame frame = {};
    const Bytes data = { 0x04 };

    frame.command = CMD_SERIAL_NUM;
    frame.type = TYPE_REQUEST_ACK;
    frame.length = data.size();

    Bytes out;

    out.append(frame);
    out.append(data);

    return sendPacket(out);
}

bool Controller::setPowerMode(PowerMode mode)
{
    ControllerFrame frame = {};
    const Bytes data = { mode };

    frame.command = CMD_POWER_MODE;
    frame.type = TYPE_REQUEST;
    frame.length = data.size();

    Bytes out;

    out.append(frame);
    out.append(data);

    return sendPacket(out);
}

bool Controller::setLedMode(LedModeData data)
{
    ControllerFrame frame = {};

    frame.command = CMD_LED_MODE;
    frame.type = TYPE_REQUEST;
    frame.length = sizeof(data);

    Bytes out;

    out.append(frame);
    out.append(data);

    return sendPacket(out);
}

bool Controller::performRumble(RumbleData data)
{
    ControllerFrame frame = {};

    frame.command = CMD_RUMBLE;
    frame.type = TYPE_COMMAND;
    frame.length = sizeof(data);

    Bytes out;

    out.append(frame);
    out.append(data);

    return sendPacket(out);
}

ControllerException::ControllerException(std::string message)
    : std::runtime_error(message) {}
