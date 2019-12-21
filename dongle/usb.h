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

#include "../utils/bytes.h"

#include <cstdint>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <string>
#include <stdexcept>

#include <libusb-1.0/libusb.h>

#define USB_BUFFER_SIZE 512

class Bytes;

/*
 * Base class for interfacing with USB devices
 * Provides sync/async control/bulk transfers
 */
class UsbDevice
{
public:
    void open(libusb_device *device);
    void close();

protected:
    struct ControlPacket
    {
        bool out;
        uint8_t request;
        uint16_t value;
        uint16_t index;
        uint8_t *data;
        uint16_t length;
    };

    virtual void added() = 0;
    virtual void removed() = 0;

    void controlTransfer(ControlPacket packet);
    void bulkReadAsync(
        uint8_t endpoint,
        FixedBytes<USB_BUFFER_SIZE> &buffer
    );
    bool nextBulkPacket(Bytes &packet);
    bool bulkWrite(uint8_t endpoint, Bytes &data);

private:
    static void readCallback(libusb_transfer *transfer);

    libusb_device_handle *handle = nullptr;

    std::mutex readMutex, writeMutex, controlMutex;
    std::queue<Bytes> readQueue;
    std::condition_variable readCondition;
};

/*
 * Registers hotplugs and handles libusb events
 */
class UsbDeviceManager
{
public:
    struct HardwareId
    {
        uint16_t vendorId, productId;
    };

    UsbDeviceManager();

    void registerDevice(
        UsbDevice *device,
        std::initializer_list<HardwareId> ids
    );
    void handleEvents();

private:
    static int hotplugCallback(
        libusb_context *context,
        libusb_device *device,
        libusb_hotplug_event event,
        void *userData
    );
};

class UsbException : public std::runtime_error
{
public:
    UsbException(std::string message, int error);
};
