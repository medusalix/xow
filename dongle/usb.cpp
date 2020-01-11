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

// Timeout in milliseconds
#define USB_WRITE_TIMEOUT 1000

void UsbDevice::open(libusb_device *device)
{
    if (handle)
    {
        Log::error("Device is already open");

        return;
    }

    int error = libusb_open(device, &handle);

    if (error)
    {
        libusb_exit(nullptr);

        throw UsbException("Error opening device: ", error);
    }

    error = libusb_reset_device(handle);

    if (error)
    {
        libusb_close(handle);
        libusb_exit(nullptr);

        throw UsbException("Error resetting device: ", error);
    }

    error = libusb_set_configuration(handle, 1);

    if (error)
    {
        libusb_close(handle);
        libusb_exit(nullptr);

        throw UsbException("Error setting configuration: ", error);
    }

    error = libusb_claim_interface(handle, 0);

    if (error)
    {
        libusb_close(handle);
        libusb_exit(nullptr);

        throw UsbException("Error claiming interface: ", error);
    }

    std::thread(&UsbDevice::added, this).detach();
}

void UsbDevice::close()
{
    // Prevent deadlocks from occuring
    std::lock(controlMutex, readMutex, writeMutex);

    // Avoid race conditions by acquiring all mutexes
    std::lock_guard<std::mutex> controlLock(controlMutex, std::adopt_lock);
    std::lock_guard<std::mutex> readLock(readMutex, std::adopt_lock);
    std::lock_guard<std::mutex> writeLock(writeMutex, std::adopt_lock);

    // Clear read queue
    readCondition.notify_one();
    readQueue = std::queue<Bytes>();

    libusb_close(handle);
    handle = nullptr;

    removed();
}

void UsbDevice::controlTransfer(ControlPacket packet)
{
    std::lock_guard<std::mutex> lock(controlMutex);

    // Device was disconnected
    if (!handle)
    {
        return;
    }

    uint8_t type = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE;

    type |= packet.out ? LIBUSB_ENDPOINT_OUT : LIBUSB_ENDPOINT_IN;

    // Number of bytes or error code
    int count = libusb_control_transfer(
        handle,
        type,
        packet.request,
        packet.value,
        packet.index,
        packet.data,
        packet.length,
        USB_WRITE_TIMEOUT
    );

    if (count != packet.length)
    {
        libusb_close(handle);
        libusb_exit(nullptr);

        throw UsbException("Error in control transfer: ", count);
    }
}

void UsbDevice::bulkReadAsync(
    uint8_t endpoint,
    FixedBytes<USB_BUFFER_SIZE> &buffer
) {
    libusb_transfer *transfer = libusb_alloc_transfer(0);

    if (!transfer)
    {
        libusb_close(handle);
        libusb_exit(nullptr);

        throw UsbException(
            "Error allocating bulk read: ",
            LIBUSB_ERROR_NO_MEM
        );
    }

    libusb_fill_bulk_transfer(
        transfer,
        handle,
        endpoint | LIBUSB_ENDPOINT_IN,
        buffer.raw(),
        buffer.size(),
        readCallback,
        this,
        0
    );

    int error = libusb_submit_transfer(transfer);

    if (error)
    {
        libusb_free_transfer(transfer);
        libusb_close(handle);
        libusb_exit(nullptr);

        throw UsbException("Error submitting bulk read", error);
    }
}

bool UsbDevice::nextBulkPacket(Bytes &packet)
{
    std::unique_lock<std::mutex> lock(readMutex);

    while (readQueue.empty())
    {
        // Device was disconnected
        if (!handle)
        {
            return false;
        }

        readCondition.wait(lock);
    }

    packet = readQueue.front();
    readQueue.pop();

    return true;
}

bool UsbDevice::bulkWrite(uint8_t endpoint, Bytes &data)
{
    std::lock_guard<std::mutex> lock(writeMutex);

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
        USB_WRITE_TIMEOUT
    );

    if (error)
    {
        Log::error("Error in bulk write: %s", libusb_error_name(error));

        return false;
    }

    return true;
}

void UsbDevice::readCallback(libusb_transfer *transfer)
{
    UsbDevice *device = static_cast<UsbDevice*>(transfer->user_data);

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
    {
        Log::error("Error in bulk read: %d", transfer->status);

        libusb_free_transfer(transfer);

        return;
    }

    const Bytes data(
        transfer->buffer,
        transfer->buffer + transfer->actual_length
    );
    std::lock_guard<std::mutex> lock(device->readMutex);

    device->readQueue.push(data);
    device->readCondition.notify_one();

    // Resubmit transfer
    int error = libusb_submit_transfer(transfer);

    if (error)
    {
        libusb_free_transfer(transfer);

        Log::error(
            "Error resubmitting bulk read: %s",
            libusb_error_name(error)
        );
    }
}

UsbDeviceManager::UsbDeviceManager()
{
    int error = libusb_init(nullptr);

    if (error)
    {
        throw UsbException("Error initializing libusb: ", error);
    }
}

void UsbDeviceManager::registerDevice(
    UsbDevice *device,
    std::initializer_list<HardwareId> ids
) {
    for (HardwareId id : ids)
    {
        int error = libusb_hotplug_register_callback(
            nullptr,
            static_cast<libusb_hotplug_event>(
                LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT
            ),
            LIBUSB_HOTPLUG_ENUMERATE,
            id.vendorId,
            id.productId,
            LIBUSB_HOTPLUG_MATCH_ANY,
            hotplugCallback,
            device,
            nullptr
        );

        if (error)
        {
            libusb_exit(nullptr);

            throw UsbException("Error registering hotplug: ", error);
        }
    }
}

void UsbDeviceManager::handleEvents()
{
    while (true)
    {
        int error = libusb_handle_events_completed(nullptr, nullptr);

        if (error)
        {
            libusb_exit(nullptr);

            throw UsbException("Error handling events: ", error);
        }
    }
}

int UsbDeviceManager::hotplugCallback(
    libusb_context *context,
    libusb_device *device,
    libusb_hotplug_event event,
    void *userData
) {
    UsbDevice *usbDevice = static_cast<UsbDevice*>(userData);

    if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED)
    {
        usbDevice->open(device);
    }

    else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT)
    {
        usbDevice->close();
    }

    return 0;
}

UsbException::UsbException(std::string message, int error)
    : std::runtime_error(message + libusb_error_name(error)) {}
