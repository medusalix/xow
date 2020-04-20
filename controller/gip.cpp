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
    CMD_CUSTOM = 0x06,
    CMD_GUIDE_BTN = 0x07,
    CMD_AUDIO_ENABLE = 0x08,
    CMD_RUMBLE = 0x09,
    CMD_LED_MODE = 0x0a,
    CMD_SERIAL_NUM = 0x1e,
    CMD_INPUT = 0x20,
    CMD_AUDIO_SAMPLES = 0x60,
};

// Different frame types
// Command: controller doesn't respond
// Request: controller responds with data
// Request (ACK): controller responds with ack + data
enum FrameType
{
    TYPE_COMMAND = 0x00,
    TYPE_REQUEST = 0x02,
    TYPE_REQUEST_ACK = 0x03,
};

struct Frame
{
    uint8_t command;
    uint8_t deviceId : 4;
    uint8_t type : 4;
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
            frame->deviceId,
            packet.toStruct<AnnounceData>(sizeof(Frame))
        );
    }

    else if (
        frame->command == CMD_STATUS &&
        frame->length == sizeof(StatusData)
    ) {
        statusReceived(
            frame->deviceId,
            packet.toStruct<StatusData>(sizeof(Frame))
        );
    }

    else if (
        frame->command == CMD_GUIDE_BTN &&
        frame->length == sizeof(GuideButtonData)
    ) {
        if (!acknowledgePacket(*frame))
        {
            Log::error("Failed to acknowledge guide button packet");

            return false;
        }

        guideButtonPressed(
            packet.toStruct<GuideButtonData>(sizeof(Frame))
        );
    }

    else if (
        frame->command == CMD_AUDIO_ENABLE &&
        frame->length == sizeof(AudioEnableData)
    ) {
        audioEnabled(
            frame->deviceId,
            packet.toStruct<AudioEnableData>(sizeof(Frame))
        );
    }

    else if (
        frame->command == CMD_SERIAL_NUM &&
        frame->length == sizeof(SerialData)
    ) {
        if (!acknowledgePacket(*frame))
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

bool GipDevice::setPowerMode(uint8_t id, PowerMode mode)
{
    Frame frame = {};
    const Bytes data = { mode };

    frame.command = CMD_POWER_MODE;
    frame.deviceId = id;
    frame.type = TYPE_REQUEST;
    frame.sequence = getSequence();
    frame.length = data.size();

    Bytes out;

    out.append(frame);
    out.append(data);

    return sendPacket(out);
}

bool GipDevice::enableAccessoryDetection()
{
    Frame frame = {};
    const Bytes data = { 0x01, 0x00 };

    frame.command = CMD_CUSTOM;
    frame.type = TYPE_REQUEST;
    frame.sequence = getSequence();
    frame.length = data.size();

    Bytes out;

    out.append(frame);
    out.append(data);

    return sendPacket(out);
}

bool GipDevice::enableAudio(uint8_t id, AudioEnableData enable)
{
    Frame frame = {};

    frame.command = CMD_AUDIO_ENABLE;
    frame.deviceId = id;
    frame.type = TYPE_REQUEST;
    frame.sequence = getSequence();
    frame.length = sizeof(enable);

    Bytes out;

    out.append(frame);
    out.append(enable);

    return sendPacket(out);
}

bool GipDevice::performRumble(RumbleData rumble)
{
    Frame frame = {};

    frame.command = CMD_RUMBLE;
    frame.type = TYPE_COMMAND;
    frame.sequence = getSequence();
    frame.length = sizeof(rumble);

    Bytes out;

    out.append(frame);
    out.append(rumble);

    return sendPacket(out);
}

bool GipDevice::setLedMode(LedModeData mode)
{
    Frame frame = {};

    frame.command = CMD_LED_MODE;
    frame.type = TYPE_REQUEST;
    frame.sequence = getSequence();
    frame.length = sizeof(mode);

    Bytes out;

    out.append(frame);
    out.append(mode);

    return sendPacket(out);
}

bool GipDevice::requestSerialNumber()
{
    Frame frame = {};
    const Bytes data = { 0x04 };

    frame.command = CMD_SERIAL_NUM;
    frame.type = TYPE_REQUEST_ACK;
    frame.sequence = getSequence();
    frame.length = data.size();

    Bytes out;

    out.append(frame);
    out.append(data);

    return sendPacket(out);
}

bool GipDevice::sendAudioSamples(uint8_t id, const Bytes &samples)
{
    // The frame data is somehow related to the sample rate
    Frame frame = {};
    const Bytes data = { 0x8c, 0x00 };

    frame.command = CMD_AUDIO_SAMPLES;
    frame.deviceId = id;
    frame.type = TYPE_REQUEST;
    frame.sequence = getSequence(true);
    frame.length = samples.size() / 12;

    Bytes out;

    out.append(frame);
    out.append(data);
    out.append(samples);

    return sendPacket(out);
}

bool GipDevice::acknowledgePacket(Frame packet)
{
    Frame frame = {};

    frame.command = CMD_ACKNOWLEDGE;
    frame.type = TYPE_REQUEST;
    frame.sequence = packet.sequence;
    frame.length = sizeof(frame) + 5;

    packet.type = TYPE_REQUEST;
    packet.sequence = packet.length;
    packet.length = 0;

    Bytes out;

    out.append(frame);
    out.pad(1);
    out.append(packet);
    out.pad(5);

    return sendPacket(out);
}

uint8_t GipDevice::getSequence(bool accessory)
{
    if (accessory)
    {
        // Zero is an invalid sequence number
        if (accessorySequence == 0x00)
        {
            accessorySequence = 0x01;
        }

        return accessorySequence++;
    }

    if (sequence == 0x00)
    {
        sequence = 0x01;
    }

    return sequence++;
}
