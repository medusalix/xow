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

#include "input.h"
#include "../utils/log.h"

#include <cstring>
#include <unistd.h>
#include <fcntl.h>

#define INPUT_MAX_FF_EFFECTS 1

InputDevice::InputDevice(
    FeedbackReceived feedbackReceived
) : feedbackReceived(feedbackReceived)
{
    file = open("/dev/uinput", O_RDWR | O_NONBLOCK);

    if (file < 0)
    {
        throw InputException("Error opening device");
    }
}

InputDevice::~InputDevice()
{
    // Wait for event thread to shut down
    if (eventThread.joinable())
    {
        eventReader.interrupt();
        eventThread.join();
    }

    if (
        ioctl(file, UI_DEV_DESTROY) < 0 ||
        close(file) < 0
    ) {
        Log::error("Error closing device: %s", strerror(errno));
    }
}

void InputDevice::addKey(uint16_t code)
{
    if (
        ioctl(file, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(file, UI_SET_KEYBIT, code) < 0
    ) {
        throw InputException("Error adding key code");
    }
}

void InputDevice::addAxis(uint16_t code, AxisConfig config)
{
    if (
        ioctl(file, UI_SET_EVBIT, EV_ABS) < 0 ||
        ioctl(file, UI_SET_ABSBIT, code) < 0
    ) {
        throw InputException("Error adding axis code");
    }

    uinput_abs_setup setup = {};

    setup.code = code;
    setup.absinfo.minimum = config.minimum;
    setup.absinfo.maximum = config.maximum;
    setup.absinfo.fuzz = config.fuzz;
    setup.absinfo.flat = config.flat;

    if (ioctl(file, UI_ABS_SETUP, &setup) < 0)
    {
        throw InputException("Error setting up axis");
    }
}

void InputDevice::addFeedback(uint16_t code)
{
    if (
        ioctl(file, UI_SET_EVBIT, EV_FF) < 0 ||
        ioctl(file, UI_SET_FFBIT, code) < 0
    ) {
        throw InputException("Error adding feedback code");
    }
}

void InputDevice::create(
    uint16_t vendorId,
    uint16_t productId,
    std::string name
) {
    uinput_setup setup = {};

    setup.id.bustype = BUS_USB;
    setup.id.vendor = vendorId;
    setup.id.product = productId;
    setup.ff_effects_max = INPUT_MAX_FF_EFFECTS;

    std::copy(name.begin(), name.end(), std::begin(setup.name));

    if (
        ioctl(file, UI_DEV_SETUP, &setup) < 0 ||
        ioctl(file, UI_DEV_CREATE) < 0
    ) {
        throw InputException("Error creating device");
    }

    eventReader.prepare(file);
    eventThread = std::thread(&InputDevice::readEvents, this);
}

void InputDevice::readEvents()
{
    input_event event = {};

    while (eventReader.read(&event, sizeof(event)))
    {
        handleEvent(event);
    }
}

void InputDevice::emitCode(
    uint16_t type,
    uint16_t code,
    int32_t value
) {
    input_event event = {};

    event.type = type;
    event.code = code;
    event.value = value;

    if (write(file, &event, sizeof(event)) != sizeof(event))
    {
        throw InputException("Error emitting key");
    }
}

void InputDevice::handleFeedbackUpload(uint32_t id)
{
    uinput_ff_upload upload = {};

    upload.request_id = id;

    if (ioctl(file, UI_BEGIN_FF_UPLOAD, &upload) < 0)
    {
        Log::error(
            "Error beginning feedback upload: %s",
            strerror(errno)
        );

        return;
    }

    effect = upload.effect;
    upload.retval = 0;

    if (ioctl(file, UI_END_FF_UPLOAD, &upload) < 0)
    {
        Log::error(
            "Error ending feedback upload: %s",
            strerror(errno)
        );

        return;
    }
}

void InputDevice::handleFeedbackErase(uint32_t id)
{
    uinput_ff_erase erase = {};

    erase.request_id = id;

    if (ioctl(file, UI_BEGIN_FF_ERASE, &erase) < 0)
    {
        Log::error(
            "Error beginning feedback erase: %s",
            strerror(errno)
        );

        return;
    }

    effect = {};
    erase.retval = 0;

    if (ioctl(file, UI_END_FF_ERASE, &erase) < 0)
    {
        Log::error(
            "Error ending feedback erase: %s",
            strerror(errno)
        );

        return;
    }
}

void InputDevice::handleEvent(input_event event)
{
    if (event.type == EV_UINPUT)
    {
        if (event.code == UI_FF_UPLOAD)
        {
            handleFeedbackUpload(event.value);
        }

        else if (event.code == UI_FF_ERASE)
        {
            handleFeedbackErase(event.value);

            // Stop feedback
            feedbackReceived(effect, 0);
        }
    }

    else if (event.type == EV_FF)
    {
        if (event.code == FF_GAIN)
        {
            // Gain varies between 0 and 0xffff
            effectGain = event.value;
        }

        // Start or stop feedback based on event value
        feedbackReceived(effect, event.value > 0 ? effectGain : 0);
    }
}

InputException::InputException(
    std::string message
) : std::runtime_error(message + ": " + strerror(errno)) {}
