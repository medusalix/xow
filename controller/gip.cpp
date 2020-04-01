/*
 * Copyright (C) 2020 Medusalix
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

#include "gip.h"
#include "../utils/log.h"
#include "../utils/bytes.h"

enum FrameCommand
{
    CMD_ACKNOWLEDGE = 0x01,
    CMD_ANNOUNCE = 0x02,
    CMD_STATUS = 0x03,
    CMD_AUTHENTICATE = 0x04,
    CMD_POWER_MODE = 0x05,
    CMD_READ_EEPROM = 0x06,
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

struct Frame
{
    uint8_t command;
    uint8_t type;
    uint8_t sequence;
    uint8_t length;
} __attribute__((packed));

GipDevice::GipDevice(SendPacket sendPacket) : sendPacket(sendPacket) {}

bool GipDevice::handlePacket(const Bytes &packet)
{
    const Frame *frame = packet.toStruct<Frame>();

    if (
        frame->command == CMD_ANNOUNCE &&
        frame->length == sizeof(AnnounceData)
    ) {
        deviceAnnounced(
            packet.toStruct<AnnounceData>(sizeof(Frame))
        );
    }

    else if (
        frame->command == CMD_STATUS &&
        frame->length == sizeof(StatusData)
    ) {
        statusReceived(
            packet.toStruct<StatusData>(sizeof(Frame))
        );
    }

    else if (
        frame->command == CMD_GUIDE_BTN &&
        frame->length == sizeof(GuideButtonData)
    ) {
        if (!acknowledgePacket(frame))
        {
            Log::error("Failed to acknowledge guide button packet");

            return false;
        }

        guideButtonPressed(
            packet.toStruct<GuideButtonData>(sizeof(Frame))
        );
    }

    else if (
        frame->command == CMD_SERIAL_NUM &&
        frame->length == sizeof(SerialData)
    ) {
        if (!acknowledgePacket(frame))
        {
            Log::error("Failed to acknowledge serial number packet");

            return false;
        }

        serialNumberReceived(
            packet.toStruct<SerialData>(sizeof(Frame))
        );
    }

    // Elite controllers send a larger input packet
    // The button remapping is done in hardware
    // The "non-remapped" input is appended to the packet
    else if (
        frame->command == CMD_INPUT &&
        frame->length >= sizeof(InputData)
    ) {
        inputReceived(
            packet.toStruct<InputData>(sizeof(Frame))
        );
    }

    // Ignore any unknown packets
    return true;
}

bool GipDevice::setPowerMode(PowerMode mode)
{
    Frame frame = {};
    const Bytes data = { mode };

    frame.command = CMD_POWER_MODE;
    frame.type = TYPE_REQUEST;
    frame.length = data.size();

    Bytes out;

    out.append(frame);
    out.append(data);

    return sendPacket(out);
}

bool GipDevice::performRumble(RumbleData data)
{
    Frame frame = {};

    frame.command = CMD_RUMBLE;
    frame.type = TYPE_COMMAND;
    frame.length = sizeof(data);

    Bytes out;

    out.append(frame);
    out.append(data);

    return sendPacket(out);
}

bool GipDevice::setLedMode(LedModeData data)
{
    Frame frame = {};

    frame.command = CMD_LED_MODE;
    frame.type = TYPE_REQUEST;
    frame.length = sizeof(data);

    Bytes out;

    out.append(frame);
    out.append(data);

    return sendPacket(out);
}

bool GipDevice::requestSerialNumber()
{
    Frame frame = {};
    const Bytes data = { 0x04 };

    frame.command = CMD_SERIAL_NUM;
    frame.type = TYPE_REQUEST_ACK;
    frame.length = data.size();

    Bytes out;

    out.append(frame);
    out.append(data);

    return sendPacket(out);
}

bool GipDevice::acknowledgePacket(const Frame *packet)
{
    Frame frame = {};

    frame.command = CMD_ACKNOWLEDGE;
    frame.type = TYPE_REQUEST;
    frame.sequence = packet->sequence;
    frame.length = sizeof(frame) + 5;

    Frame innerFrame = {};

    // Acknowledgement includes the received frame
    innerFrame.type = packet->command;
    innerFrame.sequence = TYPE_REQUEST;
    innerFrame.length = packet->length;

    Bytes out;

    out.append(frame);
    out.append(innerFrame);
    out.pad(5);

    return sendPacket(out);
}
