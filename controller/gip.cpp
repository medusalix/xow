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

#include <thread>

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
    CMD_AUDIO_CONFIG = 0x08,
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
    TYPE_ACK = 0x01,
    TYPE_REQUEST = 0x02,
};

struct Frame
{
    uint8_t command;
    uint8_t deviceId : 4;
    uint8_t type : 4;
    uint8_t sequence;
    uint8_t length;
} __attribute__((packed));

GipDevice::GipDevice(SendPacket sendPacket) : sendPacket(sendPacket) {
    rumbleThread = std::thread([&] { rumbleWorker(); });
}

GipDevice::~GipDevice() {
    if (rumbleThread.joinable()) {
        Log::info("Stopping rumble protocol thread 0x%x", rumbleThread.get_id());
        tripleBuffer.threadExit = true;
        tripleBuffer.worker.notify_all();
        rumbleThread.join();
    }
}

void GipDevice::rumbleWorker() {
    std::unique_lock<std::mutex> lock(tripleBuffer.worker_m);
    Log::info("thread %x: Started rumble protocol thread", std::this_thread::get_id());
    while (!tripleBuffer.threadExit) {
        tripleBuffer.worker.wait(lock);
        if (tripleBuffer.queued)
            performRumble();
    }
    Log::info("thread %x: Stopped rumble protocol thread", std::this_thread::get_id());
}

bool GipDevice::handlePacket(const Bytes &packet)
{
    // Ignore invalid packets
    if (packet.size() < sizeof(Frame))
    {
        return true;
    }

    const Frame *frame = packet.toStruct<Frame>();

    if (frame->type & TYPE_ACK && !acknowledgePacket(*frame))
    {
        Log::error("Failed to acknowledge packet");

        return false;
    }

    const Bytes data(packet, sizeof(Frame));

    // Data is 32-bit aligned, check for minimum size
    if (
        frame->command == CMD_ANNOUNCE &&
        frame->length == sizeof(AnnounceData) &&
        data.size() >= sizeof(AnnounceData)
    ) {
        deviceAnnounced(
            frame->deviceId,
            data.toStruct<AnnounceData>()
        );
    }

    else if (
        frame->command == CMD_STATUS &&
        frame->length == sizeof(StatusData) &&
        data.size() >= sizeof(StatusData)
    ) {
        statusReceived(
            frame->deviceId,
            data.toStruct<StatusData>()
        );
    }

    else if (
        frame->command == CMD_GUIDE_BTN &&
        frame->length == sizeof(GuideButtonData) &&
        data.size() >= sizeof(GuideButtonData)
    ) {
        guideButtonPressed(data.toStruct<GuideButtonData>());
    }

    else if (
        frame->command == CMD_SERIAL_NUM &&
        frame->length == sizeof(SerialData) &&
        data.size() >= sizeof(SerialData)
    ) {
        serialNumberReceived(data.toStruct<SerialData>());
    }

    // Elite controllers send a larger input packet
    // The button remapping is done in hardware
    // The "non-remapped" input is appended to the packet
    else if (
        frame->command == CMD_INPUT &&
        frame->length >= sizeof(InputData) &&
        data.size() >= sizeof(InputData)
    ) {
        inputReceived(data.toStruct<InputData>());
    }

    // Ignore any unknown packets
    return true;
}

bool GipDevice::setPowerMode(uint8_t id, PowerMode mode)
{
    Frame frame = {};

    frame.command = CMD_POWER_MODE;
    frame.deviceId = id;
    frame.type = TYPE_REQUEST;
    frame.sequence = getSequence();
    frame.length = sizeof(uint8_t);

    Bytes out;

    out.append(frame);
    out.append(static_cast<uint8_t>(mode));

    return sendPacket(out);
}

void GipDevice::queueRumble(const RumbleData rumble) {
    /* update our back buffer */
    *tripleBuffer.back = rumble;

    /* make our back buffer the new front buffer */
    auto frontBuffer = std::atomic_exchange(&tripleBuffer.front, tripleBuffer.back);
    std::atomic_store(&tripleBuffer.back, frontBuffer);

    tripleBuffer.queued = true;
    tripleBuffer.worker.notify_all();

    Log::debug("thread 0x%x queueRumble: tripleBuffer .front 0x%x .back 0x%x .send 0x%x",
               std::this_thread::get_id(), tripleBuffer.front.get(), tripleBuffer.back.get(),
               tripleBuffer.send.get());
}

bool GipDevice::performRumble() {
    /* get the current front buffer for sending */
    auto frontBuffer = std::atomic_exchange(&tripleBuffer.front, tripleBuffer.send);
    std::atomic_store(&tripleBuffer.send, frontBuffer);

    Frame frame = {};

    frame.command = CMD_RUMBLE;
    frame.type = TYPE_COMMAND;
    frame.sequence = getSequence();
    frame.length = sizeof(*tripleBuffer.send);

    Bytes out;

    out.append(frame);
    out.append(*tripleBuffer.send);

    Log::debug("thread 0x%x performRumble: tripleBuffer .front 0x%x .back 0x%x .send 0x%x",
               std::this_thread::get_id(), tripleBuffer.front.get(), tripleBuffer.back.get(),
               tripleBuffer.send.get());

    /* send the packet and throttle for 10ms after success */
    auto ret = sendPacket(out);
    if (ret)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return ret;
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

    frame.command = CMD_SERIAL_NUM;
    frame.type = TYPE_REQUEST | TYPE_ACK;
    frame.sequence = getSequence();
    frame.length = sizeof(uint8_t);

    Bytes out;

    // The purpose of other values is still to be discovered
    out.append(frame);
    out.append(static_cast<uint8_t>(0x04));

    return sendPacket(out);
}

bool GipDevice::acknowledgePacket(Frame frame)
{
    Frame header = {};

    header.command = CMD_ACKNOWLEDGE;
    header.deviceId = frame.deviceId;
    header.type = TYPE_REQUEST;
    header.sequence = frame.sequence;
    header.length = sizeof(header) + 5;

    frame.type = TYPE_REQUEST;
    frame.sequence = frame.length;
    frame.length = 0;

    Bytes out;

    out.append(header);
    out.pad(1);
    out.append(frame);
    out.pad(4);

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
