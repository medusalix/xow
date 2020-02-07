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

#include "mt76.h"
#include "../controller/controller.h"

#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <stdexcept>

// Microsoft's vendor ID
#define DONGLE_VID 0x045e

// Product IDs for both versions of the dongle
#define DONGLE_PID_OLD 0x02e6
#define DONGLE_PID_NEW 0x02fe

class Bytes;

/*
 * Handles received 802.11 packets
 * Delegates GIP (Game Input Protocol) packets to controllers
 */
class Dongle : public MT76
{
private:
    void added() override;
    void removed() override;

    void clientConnected(uint8_t wcid, Bytes address) override;
    void clientDisconnected(uint8_t wcid) override;
    void packetReceived(uint8_t wcid, const Bytes &packet) override;

    bool sendControllerPacket(
        uint8_t wcid,
        Bytes address,
        const Bytes &packet
    );

    std::array<std::unique_ptr<Controller>, MT_WCID_COUNT> controllers;
};

class DongleException : public std::runtime_error
{
public:
    DongleException(std::string message);
};
