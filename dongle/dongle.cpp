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

#include <functional>

Dongle::Dongle(
    std::unique_ptr<UsbDevice> usbDevice
) : Mt76(std::move(usbDevice)), stopThreads(false)
{
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
    uint8_t wcid = associateClient(address);

    if (wcid < 1)
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
    // Invalid WCID
    if (wcid < 1 || wcid > MT_WCID_COUNT)
    {
        return;
    }

    if (!removeClient(wcid))
    {
        Log::error("Failed to remove controller");

        return;
    }

    if (!controllers[wcid - 1])
    {
        Log::error("Controller '%d' is not connected", wcid);

        return;
    }

    controllers[wcid - 1].reset();

    Log::info("Controller '%d' disconnected", wcid);
}

void Dongle::handleControllerPair(Bytes address, const Bytes &packet)
{
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
    if (wcid < 1 || wcid > MT_WCID_COUNT)
    {
        return;
    }

    if (packet.size() < sizeof(QosFrame) + sizeof(uint16_t) + sizeof(uint32_t))
    {
        return;
    }

    // Skip 2 byte padding and 4 bytes at the end
    const uint8_t *begin = packet.raw() + sizeof(QosFrame) + sizeof(uint16_t);
    const uint8_t *end = packet.raw() + packet.size() - sizeof(uint32_t);
    const Bytes data(begin, end);

    if (!controllers[wcid - 1])
    {
        Log::error("Packet for unconnected controller '%d'", wcid);

        return;
    }

    if (!controllers[wcid - 1]->handlePacket(data))
    {
        Log::error("Error handling packet for controller '%d'", wcid);
    }
}

void Dongle::handleWlanPacket(const Bytes &packet)
{
    // Ignore invalid packets
    if (packet.size() < sizeof(RxWi) + sizeof(WlanFrame))
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

    if (type == MT_WLAN_DATA && subtype == MT_WLAN_QOS_DATA)
    {
        const Bytes innerPacket(
            packet,
            sizeof(RxWi) + sizeof(WlanFrame)
        );

        handleControllerPacket(rxWi->wcid, innerPacket);

        return;
    }

    if (type != MT_WLAN_MGMT)
    {
        return;
    }

    if (subtype == MT_WLAN_ASSOC_REQ)
    {
        handleControllerConnect(source);
    }

    // Only kept for compatibility with 1537 controllers
    // They associate, disassociate and associate again during pairing
    // Disassociations happen without triggering EVT_CLIENT_LOST
    else if (subtype == MT_WLAN_DISASSOC)
    {
        handleControllerDisconnect(rxWi->wcid);
    }

    // Reserved frames are used for different purposes
    // Most of them are yet to be discovered
    else if (subtype == MT_WLAN_RESERVED)
    {
        const Bytes innerPacket(
            packet,
            sizeof(RxWi) + sizeof(WlanFrame)
        );

        handleControllerPair(source, innerPacket);
    }
}

void Dongle::handleBulkData(const Bytes &data)
{
    // Ignore invalid data
    if (data.size() < sizeof(RxInfoGeneric))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(handleDataMutex);

    const RxInfoGeneric *rxInfo = data.toStruct<RxInfoGeneric>();

    if (rxInfo->port == CPU_RX_PORT)
    {
        const RxInfoCommand *info = data.toStruct<RxInfoCommand>();
        const Bytes packet(data, sizeof(RxInfoCommand));

        if (info->eventType == EVT_PACKET_RX)
        {
            handleWlanPacket(packet);
        }

        else if (info->eventType == EVT_CLIENT_LOST && packet.size() > 0)
        {
            handleControllerDisconnect(packet[0]);
        }

        else if (info->eventType == EVT_BUTTON_PRESS)
        {
            setPairingStatus(true);
        }
    }

    else if (rxInfo->port == WLAN_PORT)
    {
        const RxInfoPacket *info = data.toStruct<RxInfoPacket>();

        if (info->is80211)
        {
            const Bytes packet(data, sizeof(RxInfoPacket));

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
