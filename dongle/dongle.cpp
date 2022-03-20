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

#include "dongle.h"
#include "../utils/log.h"
#include "../utils/macros.h"

Dongle::Dongle(
    std::unique_ptr<UsbDevice> usbDevice
) : Mt76(std::move(usbDevice)), stopThreads(false)
{
    Log::info("Dongle initialized");

    threads.emplace_back(
        &Dongle::readBulkPackets,
        this,
        MT_EP_READ
    );
    threads.emplace_back(
        &Dongle::readBulkPackets,
        this,
        MT_EP_READ_PACKET
    );
}

Dongle::~Dongle()
{
    stopThreads = true;

    // Wait for all threads to shut down
    for (std::thread &thread : threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
}

void Dongle::handleControllerConnect(Bytes address)
{
    std::lock_guard<std::mutex> lock(controllerMutex);

    uint8_t wcid = associateClient(address);

    if (wcid == 0)
    {
        Log::error("Failed to associate controller");

        return;
    }

    GipDevice::SendPacket sendPacket = std::bind(
        &Dongle::sendClientPacket,
        this,
        wcid,
        address,
        std::placeholders::_1
    );

    controllers[wcid - 1].reset(new Controller(sendPacket));

    Log::info("Controller '%d' connected", wcid);
}

void Dongle::handleControllerDisconnect(uint8_t wcid)
{
    // Ignore invalid WCIDs
    if (wcid == 0 || wcid > MT_WCID_COUNT)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(controllerMutex);

    // Ignore unconnected controllers
    if (!controllers[wcid - 1])
    {
        return;
    }

    controllers[wcid - 1].reset();

    if (!removeClient(wcid))
    {
        Log::error("Failed to remove controller");

        return;
    }

    Log::info("Controller '%d' disconnected", wcid);
}

void Dongle::handleControllerPair(Bytes address, const Bytes &packet)
{
    // Ignore invalid packets
    if (packet.size() < sizeof(ReservedFrame))
    {
        return;
    }

    const ReservedFrame *frame = packet.toStruct<ReservedFrame>();

    // Type 0x01 is for pairing requests
    if (frame->type != 0x01)
    {
        return;
    }

    if (!pairClient(address))
    {
        Log::error("Failed to pair controller");

        return;
    }

    if (!setPairingStatus(false))
    {
        Log::error("Failed to disable pairing");

        return;
    }

    Log::debug(
        "Controller paired: %s",
        Log::formatBytes(address).c_str()
    );
}

void Dongle::handleControllerPacket(uint8_t wcid, const Bytes &packet)
{
    // Invalid WCID
    if (wcid == 0 || wcid > MT_WCID_COUNT)
    {
        return;
    }

    // Ignore invalid or empty packets
    if (packet.size() <= sizeof(QosFrame) + sizeof(uint16_t))
    {
        return;
    }

    // Skip 2 bytes of padding
    const Bytes data(packet, sizeof(QosFrame) + sizeof(uint16_t));

    std::lock_guard<std::mutex> lock(controllerMutex);

    // Ignore unconnected controllers
    if (!controllers[wcid - 1])
    {
        return;
    }

    if (!controllers[wcid - 1]->handlePacket(data))
    {
        Log::error("Error handling packet for controller '%d'", wcid);
    }
}

void Dongle::handleWlanPacket(const Bytes &packet)
{
    // Ignore invalid or empty packets
    if (packet.size() <= sizeof(RxWi) + sizeof(WlanFrame))
    {
        return;
    }

    const RxWi *rxWi = packet.toStruct<RxWi>();
    const WlanFrame *wlanFrame = packet.toStruct<WlanFrame>(sizeof(RxWi));

    const Bytes source(
        wlanFrame->source,
        wlanFrame->source + macAddress.size()
    );
    const Bytes destination(
        wlanFrame->destination,
        wlanFrame->destination + macAddress.size()
    );

    // Packet has wrong destination address
    if (destination != macAddress)
    {
        return;
    }

    uint8_t type = wlanFrame->frameControl.type;
    uint8_t subtype = wlanFrame->frameControl.subtype;

    if (type == MT_WLAN_MANAGEMENT)
    {
        switch (subtype)
        {
            case MT_WLAN_ASSOCIATION_REQ:
                handleControllerConnect(source);
                break;

            // Only kept for compatibility with 1537 controllers
            // They associate, disassociate and associate again during pairing
            // Disassociations happen without triggering EVT_CLIENT_LOST
            case MT_WLAN_DISASSOCIATION:
                handleControllerDisconnect(rxWi->wcid);
                break;

            // Reserved frames are used for different purposes
            // Most of them are yet to be discovered
            case MT_WLAN_RESERVED:
                const Bytes innerPacket(
                    packet,
                    sizeof(RxWi) + sizeof(WlanFrame)
                );

                handleControllerPair(source, innerPacket);
                break;
        }
    }

    else if (type == MT_WLAN_DATA && subtype == MT_WLAN_QOS_DATA)
    {
        const Bytes innerPacket(
            packet,
            sizeof(RxWi) + sizeof(WlanFrame)
        );

        handleControllerPacket(rxWi->wcid, innerPacket);
    }
}

void Dongle::handleBulkData(const Bytes &data)
{
    // Ignore invalid or empty data
    if (data.size() <= sizeof(RxInfoGeneric) + sizeof(uint32_t))
    {
        return;
    }

    // Skip packet end marker (4 bytes, identical to header)
    const RxInfoGeneric *rxInfo = data.toStruct<RxInfoGeneric>();
    const Bytes packet(data, sizeof(RxInfoGeneric), sizeof(uint32_t));

    if (rxInfo->port == CPU_RX_PORT)
    {
        const RxInfoCommand *info = data.toStruct<RxInfoCommand>();

        switch (info->eventType)
        {
            case EVT_BUTTON_PRESS:
                // Setting the pairing status doesn't require locking the mutex
                setPairingStatus(true);
                break;

            case EVT_PACKET_RX:
                handleWlanPacket(packet);
                break;

            case EVT_CLIENT_LOST:
                // Packet is guaranteed not to be empty
                #if IS_PROP_ENABLED(EMPTY_PACKET_DISCONNECT)
                handleControllerDisconnect(packet[0]);
                #endif
                break;
        }
    }

    else if (rxInfo->port == WLAN_PORT)
    {
        const RxInfoPacket *info = data.toStruct<RxInfoPacket>();

        if (info->is80211)
        {
            handleWlanPacket(packet);
        }
    }
}

void Dongle::readBulkPackets(uint8_t endpoint)
{
    FixedBytes<USB_MAX_BULK_TRANSFER_SIZE> buffer;

    while (!stopThreads)
    {
        int transferred = usbDevice->bulkRead(endpoint, buffer);

        // Bulk read failed
        if (transferred < 0)
        {
            break;
        }

        if (transferred > 0)
        {
            Bytes data = buffer.toBytes(transferred);

            handleBulkData(data);
        }
    }
}
