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

#include <cmath>
#include <linux/input.h>

// Hardware ID for the Xbox One S controller
#define CONTROLLER_VID 0x045e
#define CONTROLLER_PID 0x02ea
#define CONTROLLER_NAME "Xbox One Wireless Controller"

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

    setupInput();
    addFeedback(FF_RUMBLE);
    create(CONTROLLER_VID, CONTROLLER_PID, CONTROLLER_NAME);
    readEvents();
}

Controller::~Controller()
{
    if (!setPowerMode(POWER_OFF))
    {
        Log::error("Failed to power off controller");
    }
}

void Controller::feedbackReceived(ff_effect effect, uint16_t gain)
{
    if (effect.type != FF_RUMBLE)
    {
        return;
    }

    if (!rumbling && gain == 0)
    {
        return;
    }

    // Map Linux' magnitudes to rumble power
    uint8_t weak = static_cast<uint32_t>(
        effect.u.rumble.weak_magnitude
    ) * gain / 0xffffff;
    uint8_t strong = static_cast<uint32_t>(
        effect.u.rumble.strong_magnitude
    ) * gain / 0xffffff;

    Log::debug(
        "Feedback length: %d, delay: %d, direction: %d, weak: %d, strong: %d",
        effect.replay.length,
        effect.replay.delay,
        effect.direction,
        weak,
        strong
    );

    RumbleData rumble = {};

    rumble.motors = RUMBLE_ALL;
    rumble.left = strong;
    rumble.right = weak;
    rumble.duration = 0xff;

    // Upper half of the controller (from left to right)
    if (effect.direction >= 0x4000 && effect.direction <= 0xc000)
    {
        // Angle shifted by an eighth of a full circle
        float angle = static_cast<float>(effect.direction) / 0xffff - 0.125;
        float left = sin(2 * M_PI * angle);
        float right = cos(2 * M_PI * angle);
        uint8_t maxPower = strong > weak ? strong : weak;

        // Limit values to the left and right areas
        left = left > 0 ? left : 0;
        right = right < 0 ? -right : 0;

        // The trigger motors are very strong
        rumble.triggerLeft = left * maxPower / 4;
        rumble.triggerRight = right * maxPower / 4;
    }

    performRumble(rumble);

    rumbling = gain > 0;
}

void Controller::setupInput()
{
    AxisConfig stickConfig = {};

    // 16 bits (signed) for the sticks
    stickConfig.minimum = -32768;
    stickConfig.maximum = 32767;
    stickConfig.fuzz = 255;
    stickConfig.flat = 4095;

    AxisConfig triggerConfig = {};

    // 10 bits (unsigned) for the triggers
    triggerConfig.minimum = 0;
    triggerConfig.maximum = 1023;
    triggerConfig.fuzz = 3;
    triggerConfig.flat = 63;

    AxisConfig dpadConfig = {};

    // 1 bit for the DPAD buttons
    dpadConfig.minimum = -1;
    dpadConfig.maximum = 1;

    addKey(BTN_MODE);
    addKey(BTN_START);
    addKey(BTN_SELECT);
    addKey(BTN_A);
    addKey(BTN_B);
    addKey(BTN_X);
    addKey(BTN_Y);
    addKey(BTN_TL);
    addKey(BTN_TR);
    addKey(BTN_THUMBL);
    addKey(BTN_THUMBR);
    addAxis(ABS_X, stickConfig);
    addAxis(ABS_RX, stickConfig);
    addAxis(ABS_Y, stickConfig);
    addAxis(ABS_RY, stickConfig);
    addAxis(ABS_Z, triggerConfig);
    addAxis(ABS_RZ, triggerConfig);
    addAxis(ABS_HAT0X, dpadConfig);
    addAxis(ABS_HAT0Y, dpadConfig);
}

void Controller::reportInput(const InputData *input)
{
    setKey(BTN_START, input->buttons.start);
    setKey(BTN_SELECT, input->buttons.select);
    setKey(BTN_A, input->buttons.a);
    setKey(BTN_B, input->buttons.b);
    setKey(BTN_X, input->buttons.x);
    setKey(BTN_Y, input->buttons.y);
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
    setAxis(ABS_HAT0X, input->buttons.dpadRight - input->buttons.dpadLeft);
    setAxis(ABS_HAT0Y, input->buttons.dpadDown - input->buttons.dpadUp);
    report();
}

void Controller::packetReceived(const Bytes &packet)
{
    const ControllerFrame *frame = packet.toStruct<ControllerFrame>();

    if (
        frame->command == CMD_SERIAL_NUM &&
        frame->length == sizeof(SerialData)
    ) {
        if (!acknowledgePacket(frame))
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

    // Elite controllers send a larger input packet
    // The button remapping is done in hardware
    // The "non-remapped" input is appended to the packet
    else if (
        frame->command == CMD_INPUT &&
        frame->length >= sizeof(InputData)
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
        if (!acknowledgePacket(frame))
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

bool Controller::acknowledgePacket(const ControllerFrame *packet)
{
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
