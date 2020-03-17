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

#include "usb.h"
#include "../utils/log.h"
#include "../utils/bytes.h"

#include <thread>
#include <cstring>
#include <atomic>
#include <csignal>
#include <sys/signalfd.h>
#include <unistd.h>

// Timeouts in milliseconds
// No timeout for reading
#define USB_TIMEOUT_READ 0
#define USB_TIMEOUT_WRITE 1000

void UsbDevice::open(libusb_device *device)
{
    // Device is already open
    if (handle)
    {
        return;
    }

    Log::debug("Opening device...");

    int error = libusb_open(device, &handle);

    if (error)
    {
        throw UsbException("Error opening device: ", error);
    }

    error = libusb_reset_device(handle);

    if (error)
    {
        throw UsbException("Error resetting device: ", error);
    }

    error = libusb_set_configuration(handle, 1);

    if (error)
    {
        throw UsbException("Error setting configuration: ", error);
    }

    error = libusb_claim_interface(handle, 0);

    if (error)
    {
        throw UsbException("Error claiming interface: ", error);
    }

    if (!afterOpen())
    {
        throw UsbException("Error opening device");
    }
}

void UsbDevice::close()
{
    // Device is closed
    if (!handle)
    {
        return;
    }

    Log::debug("Closing device...");

    if (!beforeClose())
    {
        throw UsbException("Error closing device");
    }

    libusb_close(handle);
}

void UsbDevice::controlTransfer(ControlPacket packet, bool write)
{
    uint8_t type = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE;

    type |= write ? LIBUSB_ENDPOINT_OUT : LIBUSB_ENDPOINT_IN;

    // Number of bytes or error code
    int transferred = libusb_control_transfer(
        handle,
        type,
        packet.request,
        packet.value,
        packet.index,
        packet.data,
        packet.length,
        USB_TIMEOUT_WRITE
    );

    if (transferred != packet.length)
    {
        Log::error(
            "Error in control transfer: %s",
            libusb_error_name(transferred)
        );

        terminate();
    }
}

int UsbDevice::bulkRead(
    uint8_t endpoint,
    FixedBytes<USB_BUFFER_SIZE> &buffer
) {
    int transferred = 0;
    int error = libusb_bulk_transfer(
        handle,
        endpoint | LIBUSB_ENDPOINT_IN,
        buffer.raw(),
        buffer.size(),
        &transferred,
        USB_TIMEOUT_READ
    );

    if (error)
    {
        Log::error("Error in bulk read: %s", libusb_error_name(error));

        terminate();

        return 0;
    }

    return transferred;
}

bool UsbDevice::bulkWrite(uint8_t endpoint, Bytes &data)
{
    // Device was disconnected
    if (!handle)
    {
        return false;
    }

    int error = libusb_bulk_transfer(
        handle,
        endpoint | LIBUSB_ENDPOINT_OUT,
        data.raw(),
        data.size(),
        nullptr,
        USB_TIMEOUT_WRITE
    );

    if (error)
    {
        Log::error("Error in bulk write: %s", libusb_error_name(error));

        terminate();

        return false;
    }

    return true;
}

UsbDeviceManager::UsbDeviceManager()
{
    sigset_t signalMask;

    sigemptyset(&signalMask);
    sigaddset(&signalMask, SIGINT);
    sigaddset(&signalMask, SIGTERM);

    // Block signals in all threads
    if (pthread_sigmask(SIG_BLOCK, &signalMask, nullptr) < 0)
    {
        throw UsbException("Error setting signal mask: ", strerror(errno));
    }

    int error = libusb_init(nullptr);

    if (error)
    {
        throw UsbException("Error initializing libusb: ", error);
    }

    // Signals can be read from the file descriptor
    signalFile = signalfd(-1, &signalMask, 0);

    if (signalFile < 0)
    {
        throw UsbException("Error creating signal file: ", strerror(errno));
    }
}

void UsbDeviceManager::registerDevice(
    UsbDevice &device,
    std::initializer_list<HardwareId> ids
) {
    for (HardwareId id : ids)
    {
        int error = libusb_hotplug_register_callback(
            nullptr,
            static_cast<libusb_hotplug_event>(
                LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED
            ),
            LIBUSB_HOTPLUG_ENUMERATE,
            id.vendorId,
            id.productId,
            LIBUSB_HOTPLUG_MATCH_ANY,
            hotplugCallback,
            &device,
            &hotplugHandle
        );

        if (error)
        {
            throw UsbException("Error registering hotplug: ", error);
        }
    }
}

void UsbDeviceManager::handleEvents(UsbDevice &device)
{
    std::atomic<bool> run(true);

    // Device termination callback (in case of errors)
    device.terminate = [this, &run]
    {
        Log::debug("Device error, terminating...");

        run = false;
    };

    // Dedicated thread for signal handling
    std::thread([this, &device, &run]
    {
        signalfd_siginfo info = {};

        if (read(signalFile, &info, sizeof(info)) != sizeof(info))
        {
            throw UsbException("Error reading signal: ", strerror(errno));
        }

        Log::debug("Stop signal received");

        device.close();
        run = false;

        // Interrupt the event handling
        libusb_hotplug_deregister_callback(nullptr, hotplugHandle);
    }).detach();

    while (run)
    {
        int error = libusb_handle_events_completed(nullptr, nullptr);

        if (error)
        {
            throw UsbException("Error handling events: ", error);
        }
    }

    libusb_exit(nullptr);
}

int UsbDeviceManager::hotplugCallback(
    libusb_context *context,
    libusb_device *device,
    libusb_hotplug_event event,
    void *userData
) {
    UsbDevice *usbDevice = static_cast<UsbDevice*>(userData);

    // Transfers inside the callback are not allowed
    std::thread(&UsbDevice::open, usbDevice, device).detach();

    return 0;
}

UsbException::UsbException(std::string message, int error)
    : std::runtime_error(message + libusb_error_name(error)) {}

UsbException::UsbException(std::string message, std::string error)
    : std::runtime_error(message + error) {}
