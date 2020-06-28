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

#include "gip.h"
#include "input.h"
#include "../utils/buffer.h"

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

/*
 * Forwards gamepad events to virtual input device
 * Passes force feedback effects to gamepad
 */
class Controller : public GipDevice
{
public:
    Controller(SendPacket sendPacket);
    ~Controller();

private:
    /* GIP events */
    void deviceAnnounced(uint8_t id, const AnnounceData *announce) override;
    void statusReceived(uint8_t id, const StatusData *status) override;
    void guideButtonPressed(const GuideButtonData *button) override;
    void serialNumberReceived(const SerialData *serial) override;
    void inputReceived(const InputData *input) override;

    /* Device initialization */
    void initInput(const AnnounceData *announce);

    /* Rumble buffer consumer */
    void processRumble();

    /* OS interface */
    void inputFeedbackReceived(
        uint16_t gain,
        ff_effect effect,
        uint8_t replayCount
    );

    InputDevice inputDevice;
    std::atomic<bool> stopRumbleThread;
    std::thread rumbleThread;
    std::mutex rumbleMutex;
    std::condition_variable rumbleCondition;
    Buffer<RumbleData> rumbleBuffer;

    uint8_t batteryLevel = 0xff;
};
