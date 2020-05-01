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
#define USB_TIMEOUT_READ 1000
#define USB_TIMEOUT_WRITE 1000

UsbDevice::UsbDevice(
    libusb_device *device,
    Terminate terminate
) : terminate(terminate)
{
    Log::debug("Opening device...");

    int error = libusb_open(device, &handle);

    if (error)
    {
        throw UsbException(
            "Error opening device",
            libusb_error_name(error)
        );
    }

    error = libusb_reset_device(handle);

    if (error)
    {
        throw UsbException(
            "Error resetting device",
            libusb_error_name(error)
        );
    }

    error = libusb_set_configuration(handle, 1);

    if (error)
    {
        throw UsbException(
            "Error setting configuration",
            libusb_error_name(error)
        );
    }

    error = libusb_claim_interface(handle, 0);

    if (error)
    {
        throw UsbException(
            "Error claiming interface",
            libusb_error_name(error)
        );
    }
}

UsbDevice::~UsbDevice()
{
    Log::debug("Closing device...");

    libusb_close(handle);
}

void UsbDevice::controlTransfer(ControlPacket packet, bool write)
{
    uint8_t direction = write ? LIBUSB_ENDPOINT_OUT : LIBUSB_ENDPOINT_IN;

    // Number of bytes or error code
    int transferred = libusb_control_transfer(
        handle,
        LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | direction,
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
    FixedBytes<USB_MAX_BULK_TRANSFER_SIZE> &buffer
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

    if (error && error != LIBUSB_ERROR_TIMEOUT)
    {
        Log::error("Error in bulk read: %s", libusb_error_name(error));

        terminate();

        return -1;
    }

    return transferred;
}

bool UsbDevice::bulkWrite(uint8_t endpoint, Bytes &data)
{
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
    sigemptyset(&signalMask);
    sigaddset(&signalMask, SIGINT);
    sigaddset(&signalMask, SIGTERM);

    // Block signals for all threads started by libusb
    if (pthread_sigmask(SIG_BLOCK, &signalMask, nullptr) < 0)
    {
        throw UsbException(
            "Error blocking signals",
            strerror(errno)
        );
    }

    int error = libusb_init(nullptr);

    if (error)
    {
        throw UsbException(
            "Error initializing libusb",
            libusb_error_name(error)
        );
    }

    // Unblock signals for current thread to allow interruption
    if (pthread_sigmask(SIG_UNBLOCK, &signalMask, nullptr) < 0)
    {
        throw UsbException(
            "Error unblocking signals",
            strerror(errno)
        );
    }
}

UsbDeviceManager::~UsbDeviceManager()
{
    libusb_exit(nullptr);
}

std::unique_ptr<UsbDevice> UsbDeviceManager::getDevice(
    std::initializer_list<HardwareId> ids
) {
    libusb_device *device = waitForDevice(ids);

    // Block signals and pass them to the signalfd
    if (pthread_sigmask(SIG_BLOCK, &signalMask, nullptr) < 0)
    {
        throw UsbException(
            "Error blocking signals",
            strerror(errno)
        );
    }

    int file = signalfd(-1, &signalMask, 0);

    if (file < 0)
    {
        throw UsbException(
            "Error creating signal file",
            strerror(errno)
        );
    }

    signalReader.prepare(file);

    // Pass ownership of USB device to caller
    return std::unique_ptr<UsbDevice>(new UsbDevice(
        device,
        std::bind(&InterruptibleReader::interrupt, &signalReader)
    ));
}

void UsbDeviceManager::waitForShutdown()
{
    signalfd_siginfo info = {};

    if (signalReader.read(&info, sizeof(info)))
    {
        Log::info("Shutting down...");
    }

    else
    {
        Log::error("Shutting down due to error...");
    }
}

libusb_device* UsbDeviceManager::waitForDevice(
    std::initializer_list<HardwareId> ids
) {
    std::vector<libusb_hotplug_callback_handle> handles(ids.size());
    size_t counter = 0;
    libusb_device *device = nullptr;

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
            &handles[counter]
        );

        if (error)
        {
            throw UsbException(
                "Error registering hotplug",
                libusb_error_name(error)
            );
        }

        counter++;
    }

    Log::info("Waiting for device...");

    // Handle events until device is plugged in
    while (!device)
    {
        int error = libusb_handle_events_completed(nullptr, nullptr);

        if (error)
        {
            throw UsbException(
                "Error handling events",
                libusb_error_name(error)
            );
        }
    }

    // Remove all hotplug callbacks
    for (libusb_hotplug_callback_handle handle : handles)
    {
        libusb_hotplug_deregister_callback(nullptr, handle);
    }

    return device;
}

int UsbDeviceManager::hotplugCallback(
    libusb_context *context,
    libusb_device *device,
    libusb_hotplug_event event,
    void *userData
) {
    libusb_device **newDevice = static_cast<libusb_device**>(userData);

    *newDevice = device;

    // Deregister hotplug callback
    return 1;
}

UsbException::UsbException(
    std::string message,
    std::string error
) : std::runtime_error(message + ": " + error) {}
