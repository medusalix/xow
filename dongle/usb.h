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
#include <functional>
#include <string>
#include <stdexcept>

#include <libusb-1.0/libusb.h>

#define USB_BUFFER_SIZE 512

/*
 * Base class for interfacing with USB devices
 * Provides control/bulk transfers
 */
class UsbDevice
{
private:
    using Terminate = std::function<void()>;

public:
    void open(libusb_device *device);
    void close();

    Terminate terminate;

protected:
    struct ControlPacket
    {
        uint8_t request;
        uint16_t value;
        uint16_t index;
        uint8_t *data;
        uint16_t length;
    };

    virtual bool afterOpen() = 0;
    virtual bool beforeClose() = 0;

    void controlTransfer(ControlPacket packet, bool write);
    int bulkRead(
        uint8_t endpoint,
        FixedBytes<USB_BUFFER_SIZE> &buffer
    );
    bool bulkWrite(uint8_t endpoint, Bytes &data);

private:
    libusb_device_handle *handle = nullptr;
};

/*
 * Registers hotplugs, handles libusb events and signals
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
        UsbDevice &device,
        std::initializer_list<HardwareId> ids
    );
    void handleEvents(UsbDevice &device);

private:
    static int hotplugCallback(
        libusb_context *context,
        libusb_device *device,
        libusb_hotplug_event event,
        void *userData
    );

    int signalFile;

    libusb_hotplug_callback_handle hotplugHandle;
};

class UsbException : public std::runtime_error
{
public:
    UsbException(std::string message, int error);
    UsbException(std::string message, std::string error = "");
};
