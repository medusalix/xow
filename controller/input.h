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

#include <cstdint>
#include <string>
#include <stdexcept>

#include <linux/uinput.h>

/*
 * Base class for user mode input devices
 * Supports force feedback
 */
class InputDevice
{
public:
    static bool isAvailable();

protected:
    InputDevice();
    virtual ~InputDevice();

    virtual void feedbackReceived(
        ff_effect effect,
        uint8_t gain
    ) = 0;

    void addKey(uint16_t code);
    void addAxis(uint16_t code, int32_t min, int32_t max);
    void addFeedback(uint16_t code);

    void create(
        uint16_t vendorId,
        uint16_t productId,
        std::string name
    );
    void readEvents();

    inline void setKey(uint16_t key, bool pressed)
    {
        emitCode(EV_KEY, key, pressed);
    }

    inline void setAxis(uint16_t abs, uint32_t value)
    {
        emitCode(EV_ABS, abs, value);
    }

    inline void report()
    {
        emitCode(EV_SYN, SYN_REPORT, 0);
    }

private:
    void emitCode(
        uint16_t type,
        uint16_t code,
        int32_t value
    );

    void handleFeedbackUpload(uint32_t id);
    void handleFeedbackErase(uint32_t id);
    void handleEvent(input_event event);

    int file, stopPipe;
    ff_effect effect = {};
    uint8_t effectGain = 100;
};

class InputException : public std::runtime_error
{
public:
    InputException(std::string message);
};
