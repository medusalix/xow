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

#include "mt76.h"
#include "../utils/log.h"
#include "../utils/bytes.h"

#include <thread>
#include <algorithm>

extern uint8_t _binary_firmware_bin_start[];
extern uint8_t _binary_firmware_bin_end[];

void MT76::added()
{
    loadFirmware();
    initChip();

    bulkReadAsync(MT_EP_READ, buffer);
    bulkReadAsync(MT_EP_READ_PACKET, packetBuffer);

    std::thread([this]()
    {
        Bytes packet;
        bool read = false;

        // Read while device is connected
        do
        {
            read = nextBulkPacket(packet);

            handleBulkPacket(packet);
        } while (read);
    }).detach();
}

void MT76::removed()
{
    macAddress.clear();
    connectedWcids = 0;
}

void MT76::handleWlanPacket(const Bytes &packet)
{
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

    if (type != MT_WLAN_MGMT)
    {
        return;
    }

    if (subtype == MT_WLAN_ASSOC_REQ)
    {
        Log::debug(
            "Client associating: %s",
            Log::formatBytes(source).c_str()
        );

        uint8_t wcid = associateClient(source);

        if (!wcid)
        {
            Log::error("Failed to associate client");

            return;
        }

        clientConnected(wcid, source);
    }

    else if (subtype == MT_WLAN_PAIR)
    {
        const PairingFrame *pairingFrame = packet.toStruct<PairingFrame>(
            sizeof(RxWi) + sizeof(WlanFrame)
        );

        // Pairing frame 'unknown' is always 0x70, 'type' is 0x01 for pairing
        if (pairingFrame->type != 0x01)
        {
            return;
        }

        Log::debug(
            "Client pairing: %s",
            Log::formatBytes(source).c_str()
        );

        if (!pairClient(source))
        {
            Log::error("Failed to pair client");
        }
    }
}

void MT76::handleClientPacket(const Bytes &packet)
{
    const RxWi *rxWi = packet.toStruct<RxWi>();
    const WlanFrame *wlanFrame = packet.toStruct<WlanFrame>(sizeof(RxWi));

    uint8_t type = wlanFrame->frameControl.type;
    uint8_t subtype = wlanFrame->frameControl.subtype;

    if (type != MT_WLAN_DATA || subtype != MT_WLAN_QOS_DATA)
    {
        return;
    }

    // Skip 2 byte padding
    const Bytes data(
        packet,
        sizeof(RxWi) + sizeof(WlanFrame) + sizeof(QosFrame) + sizeof(uint16_t)
    );

    packetReceived(rxWi->wcid, data);
}

void MT76::handleClientLost(const Bytes &packet)
{
    // Invalid packet
    if (packet.size() < 1)
    {
        return;
    }

    uint8_t wcid = packet[0];

    // Invalid WCID or not connected
    if (!wcid || !(connectedWcids & BIT(wcid - 1)))
    {
        return;
    }

    Log::debug("Client lost: %d", wcid);

    if (!removeClient(wcid))
    {
        Log::error("Failed to remove client");

        return;
    }

    clientDisconnected(wcid);
}

void MT76::handleButtonPress()
{
    // Start sending the 'pairing' beacon
    if (!writeBeacon(true))
    {
        Log::error("Failed to write pairing beacon");

        return;
    }

    if (!setLedMode(MT_LED_BLINK))
    {
        Log::error("Failed to set LED mode");

        return;
    }

    Log::info("Pairing initiated");
}

void MT76::handleBulkPacket(const Bytes &packet)
{
    if (packet.size() < sizeof(RxInfoGeneric))
    {
        Log::error("Invalid packet received");

        return;
    }

    const RxInfoGeneric *rxInfo = packet.toStruct<RxInfoGeneric>();

    if (rxInfo->port == CPU_RX_PORT)
    {
        const RxInfoCommand *info = packet.toStruct<RxInfoCommand>();
        const Bytes data(packet, sizeof(RxInfoCommand));

        if (info->eventType == EVT_PACKET_RX)
        {
            handleClientPacket(data);
        }

        else if (info->eventType == EVT_CLIENT_LOST)
        {
            handleClientLost(data);
        }

        else if (info->eventType == EVT_BUTTON_PRESS)
        {
            handleButtonPress();
        }
    }

    else if (rxInfo->port == WLAN_PORT)
    {
        const RxInfoPacket *info = packet.toStruct<RxInfoPacket>();

        if (info->is80211)
        {
            const Bytes data(packet, sizeof(RxInfoPacket));

            handleWlanPacket(data);
        }
    }
}

uint8_t MT76::associateClient(Bytes address)
{
    // Find first available WCID
    uint16_t freeWcids = static_cast<uint16_t>(~connectedWcids);
    uint8_t wcid = __builtin_ffs(freeWcids);

    if (!wcid)
    {
        Log::error("All WCIDs are taken");

        return 0;
    }

    connectedWcids |= BIT(wcid - 1);

    TxWi txWi = {};

    // OFDM transmission method
    // Wait for acknowledgement
    // Ignore wireless client identifier (WCID)
    txWi.phyType = MT_PHY_TYPE_OFDM;
    txWi.ack = 1;
    txWi.wcid = 0xff;
    txWi.mpduByteCount = sizeof(WlanFrame) + sizeof(AssociationResponseFrame);

    WlanFrame wlanFrame = {};

    wlanFrame.frameControl.type = MT_WLAN_MGMT;
    wlanFrame.frameControl.subtype = MT_WLAN_ASSOC_RESP;

    address.copy(wlanFrame.destination);
    macAddress.copy(wlanFrame.source);
    macAddress.copy(wlanFrame.bssId);

    // Status code zero (success)
    // Association ID can remain zero
    // Wildcard SSID
    AssociationResponseFrame associationFrame = {};

    Bytes out;

    out.append(txWi);
    out.append(wlanFrame);
    out.append(associationFrame);

    const Bytes gain = {
        static_cast<uint8_t>(wcid - 1), 0x00, 0x00, 0x00,
        0x40, 0x1f, 0x00, 0x00
    };

    // WCID 0 is reserved for beacon frames
    if (!burstWrite(MT_WCID_ADDR(wcid), address))
    {
        Log::error("Failed to write WCID");

        return 0;
    }

    if (!initGain(1, gain))
    {
        Log::error("Failed to init gain");

        return 0;
    }

    if (!sendWlanPacket(out))
    {
        Log::error("Failed to send association packet");

        return 0;
    }

    if (!setLedMode(MT_LED_ON))
    {
        Log::error("Failed to set LED mode");

        return 0;
    }

    return wcid;
}

bool MT76::removeClient(uint8_t wcid)
{
    const Bytes emptyAddress = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    const Bytes gain = {
        static_cast<uint8_t>(wcid - 1), 0x00, 0x00, 0x00
    };

    // Remove WCID from connected clients
    connectedWcids &= ~BIT(wcid - 1);

    if (!initGain(2, gain))
    {
        Log::error("Failed to reset gain");

        return false;
    }

    if (!burstWrite(MT_WCID_ADDR(wcid), emptyAddress))
    {
        Log::error("Failed to write WCID");

        return false;
    }

    if (!connectedWcids && !setLedMode(MT_LED_OFF))
    {
        Log::error("Failed to set LED mode");

        return false;
    }

    return true;
}

bool MT76::pairClient(Bytes address)
{
    const Bytes data = { 0x70, 0x02, 0x00 };

    TxWi txWi = {};

    // OFDM transmission method
    // Wait for acknowledgement
    // Ignore wireless client index (WCID)
    txWi.phyType = MT_PHY_TYPE_OFDM;
    txWi.ack = 1;
    txWi.wcid = 0xff;
    txWi.mpduByteCount = sizeof(WlanFrame) + data.size();

    WlanFrame wlanFrame = {};

    wlanFrame.frameControl.type = MT_WLAN_MGMT;
    wlanFrame.frameControl.subtype = MT_WLAN_PAIR;

    address.copy(wlanFrame.destination);
    macAddress.copy(wlanFrame.source);
    macAddress.copy(wlanFrame.bssId);

    Bytes out;

    out.append(txWi);
    out.append(wlanFrame);
    out.append(data);

    if (!sendWlanPacket(out))
    {
        Log::error("Failed to send pairing packet");

        return false;
    }

    return true;
}

bool MT76::sendWlanPacket(const Bytes &data)
{
    // Values must be 32-bit aligned
    // 32 zero-bits mark the end
    uint32_t length = data.size();
    uint8_t padding = length % sizeof(uint32_t);

    TxInfoPacket info = {};

    // 802.11 WLAN packet
    // Wireless information valid (WIV)
    // Enhanced distributed channel access (EDCA)
    info.port = WLAN_PORT;
    info.infoType = NORMAL_PACKET;
    info.is80211 = 1;
    info.wiv = 1;
    info.qsel = MT_QSEL_EDCA;
    info.length = length + padding;

    Bytes out;

    out.append(info);
    out.append(data);
    out.pad(padding);
    out.pad(sizeof(uint32_t));

    if (!bulkWrite(MT_EP_WRITE, out))
    {
        Log::error("Failed to write WLAN packet");

        return false;
    }

    return true;
}

bool MT76::initRegisters()
{
    controlWrite(
        MT_MAC_SYS_CTRL,
        MT_MAC_SYS_CTRL_RESET_CSR | MT_MAC_SYS_CTRL_RESET_BBP
    );
    controlWrite(MT_USB_DMA_CFG, 0);
    controlWrite(MT_MAC_SYS_CTRL, 0);
    controlWrite(MT_PWR_PIN_CFG, 0);
    controlWrite(MT_LDO_CTRL_1, 0x6b006464);
    controlWrite(MT_WPDMA_GLO_CFG, 0x70);
    controlWrite(MT_WMM_AIFSN, 0x2273);
    controlWrite(MT_WMM_CWMIN, 0x2344);
    controlWrite(MT_WMM_CWMAX, 0x34aa);
    controlWrite(MT_FCE_DMA_ADDR, 0x041200);
    controlWrite(MT_TSO_CTRL, 0);
    controlWrite(MT_PBF_SYS_CTRL, 0x080c00);
    controlWrite(MT_PBF_TX_MAX_PCNT, 0x1fbf1f1f);
    controlWrite(MT_FCE_PSE_CTRL, 0x01);
    controlWrite(
        MT_MAC_SYS_CTRL,
        MT_MAC_SYS_CTRL_ENABLE_TX | MT_MAC_SYS_CTRL_ENABLE_RX
    );
    controlWrite(MT_AUTO_RSP_CFG, 0x13);
    controlWrite(MT_MAX_LEN_CFG, 0x3e3fff);
    controlWrite(MT_AMPDU_MAX_LEN_20M1S, 0xfffc9855);
    controlWrite(MT_AMPDU_MAX_LEN_20M2S, 0xff);
    controlWrite(MT_BKOFF_SLOT_CFG, 0x0109);
    controlWrite(MT_PWR_PIN_CFG, 0);
    controlWrite(MT_EDCA_CFG_AC(0), 0x064320);
    controlWrite(MT_EDCA_CFG_AC(1), 0x0a4700);
    controlWrite(MT_EDCA_CFG_AC(2), 0x043238);
    controlWrite(MT_EDCA_CFG_AC(3), 0x03212f);
    controlWrite(MT_TX_PIN_CFG, 0x150f0f);
    controlWrite(MT_TX_SW_CFG0, 0x101001);
    controlWrite(MT_TX_SW_CFG1, 0x010000);
    controlWrite(MT_TXOP_CTRL_CFG, 0x583f);
    controlWrite(MT_TX_RTS_CFG, 0x092b20);
    controlWrite(MT_TX_TIMEOUT_CFG, 0x0a0f90);
    controlWrite(MT_TX_RETRY_CFG, 0x47d01f0f);
    controlWrite(MT_CCK_PROT_CFG, 0x03f40003);
    controlWrite(MT_OFDM_PROT_CFG, 0x03f40003);
    controlWrite(MT_MM20_PROT_CFG, 0x01742004);
    controlWrite(MT_GF20_PROT_CFG, 0x01742004);
    controlWrite(MT_GF40_PROT_CFG, 0x03f42084);
    controlWrite(MT_EXP_ACK_TIME, 0x2c00dc);
    controlWrite(MT_TX0_RF_GAIN_ATTEN, 0x22160a00);
    controlWrite(MT_TX_ALC_CFG_3, 0x22160a76);
    controlWrite(MT_TX_ALC_CFG_0, 0x3f3f1818);
    controlWrite(MT_TX_ALC_CFG_4, 0x80000606);
    controlWrite(MT_TX_PROT_CFG6, 0xe3f52004);
    controlWrite(MT_TX_PROT_CFG7, 0xe3f52084);
    controlWrite(MT_TX_PROT_CFG8, 0xe3f52104);
    controlWrite(MT_PIFS_TX_CFG, 0x060fff);
    controlWrite(MT_RX_FILTR_CFG, 0x015f9f);
    controlWrite(MT_LEGACY_BASIC_RATE, 0x017f);
    controlWrite(MT_HT_BASIC_RATE, 0x8003);
    controlWrite(MT_PN_PAD_MODE, 0x02);
    controlWrite(MT_TXOP_HLDR_ET, 0x02);
    controlWrite(MT_TX_PROT_CFG6, 0xe3f42004);
    controlWrite(MT_TX_PROT_CFG7, 0xe3f42084);
    controlWrite(MT_TX_PROT_CFG8, 0xe3f42104);
    controlWrite(MT_DACCLK_EN_DLY_CFG, 0);
    controlWrite(MT_RF_PA_MODE_ADJ0, 0xee000000);
    controlWrite(MT_RF_PA_MODE_ADJ1, 0xee000000);
    controlWrite(MT_TX0_RF_GAIN_CORR, 0x0f3c3c3c);
    controlWrite(MT_TX1_RF_GAIN_CORR, 0x0f3c3c3c);
    controlWrite(MT_PBF_CFG, 0x1efebcf5);
    controlWrite(MT_PAUSE_ENABLE_CONTROL1, 0x0a);
    controlWrite(MT_XIFS_TIME_CFG, 0x33a40e0a);
    controlWrite(MT_FCE_L2_STUFF, 0x03ff0223);
    controlWrite(MT_TX_RTS_CFG, 0);
    controlWrite(MT_BEACON_TIME_CFG, 0x0640);
    controlWrite(MT_CMB_CTRL, 0x0091a7ff);
    controlWrite(MT_BBP(TXBE, 5), 0);

    // Necessary for reliable WLAN associations
    controlWrite(MT_RF_BYPASS_0, 0x7f000000);
    controlWrite(MT_RF_SETTING_0, 0x1a800000);
    controlWrite(MT_RF_BYPASS_0, 0);
    controlWrite(MT_RF_SETTING_0, 0);

    // Read crystal calibration from EFUSE
    uint32_t calibration = efuseRead(MT_EF_XTAL_CALIB, 3) >> 16;

    controlWrite(MT_XO_CTRL5, calibration, MT_VEND_WRITE_CFG);
    controlWrite(MT_XO_CTRL6, MT_XO_CTRL6_C2_CTRL, MT_VEND_WRITE_CFG);

    // Read MAC address from EFUSE
    uint32_t macAddress1 = efuseRead(MT_EF_MAC_ADDR, 1);
    uint32_t macAddress2 = efuseRead(MT_EF_MAC_ADDR, 2);

    macAddress.append(macAddress1, 4);
    macAddress.append(macAddress2, 2);

    if (!burstWrite(MT_MAC_ADDR_DW0, macAddress))
    {
        Log::error("Failed to write MAC address");

        return false;
    }

    if (!burstWrite(MT_MAC_BSSID_DW0, macAddress))
    {
        Log::error("Failed to write BSSID");

        return false;
    }

    Log::info("Chip address: %s", Log::formatBytes(macAddress).c_str());

    return true;
}

void MT76::loadFirmware()
{
    if (controlRead(MT_FCE_DMA_ADDR, MT_VEND_READ_CFG))
    {
        Log::debug("Firmware already loaded");

        return;
    }

    DmaConfig config = {};

    config.props.rxBulkEnabled = 1;
    config.props.txBulkEnabled = 1;

    // Configure direct memory access (DMA)
    // Enable FCE and packet DMA
    controlWrite(MT_USB_U3DMA_CFG, config.value, MT_VEND_WRITE_CFG);
    controlWrite(MT_FCE_PSE_CTRL, 0x01);
    controlWrite(MT_TX_CPU_FROM_FCE_BASE_PTR, 0x400230);
    controlWrite(MT_TX_CPU_FROM_FCE_MAX_COUNT, 0x01);
    controlWrite(MT_TX_CPU_FROM_FCE_CPU_DESC_IDX, 0x01);
    controlWrite(MT_FCE_PDMA_GLOBAL_CONF, 0x44);
    controlWrite(MT_FCE_SKIP_FS, 0x03);

    const Bytes firmware(
        _binary_firmware_bin_start,
        _binary_firmware_bin_end
    );

    const FwHeader *header = firmware.toStruct<FwHeader>();
    Bytes::Iterator ilmStart = firmware.begin() + sizeof(FwHeader);
    Bytes::Iterator dlmStart = ilmStart + header->ilmLength;
    Bytes::Iterator dlmEnd = dlmStart + header->dlmLength;

    // Upload firmware parts (ILM and DLM)
    if (!loadFirmwarePart(MT_MCU_ILM_OFFSET, ilmStart, dlmStart))
    {
        throw MT76Exception("Failed to write ILM");
    }

    if (!loadFirmwarePart(MT_MCU_DLM_OFFSET, dlmStart, dlmEnd))
    {
        throw MT76Exception("Failed to write DLM");
    }

    // Load initial vector block (IVB)
    controlWrite(MT_FCE_DMA_ADDR, 0, MT_VEND_WRITE_CFG);
    controlWrite(MT_FW_LOAD_IVB, 0, MT_VEND_DEV_MODE);

    // Wait for firmware to start
    while (controlRead(MT_FCE_DMA_ADDR, MT_VEND_READ_CFG) != 0x01);

    Log::debug("Firmware loaded");
}

bool MT76::loadFirmwarePart(
    uint32_t offset,
    Bytes::Iterator start,
    Bytes::Iterator end
) {
    // Send firmware in chunks
    for (Bytes::Iterator chunk = start; chunk < end; chunk += MT_FW_CHUNK_SIZE)
    {
        uint32_t address = offset + chunk - start;
        uint32_t remaining = end - chunk;
        uint16_t length = remaining > MT_FW_CHUNK_SIZE
            ? MT_FW_CHUNK_SIZE
            : remaining;

        TxInfoCommand info = {};

        info.port = CPU_TX_PORT;
        info.infoType = NORMAL_PACKET;
        info.length = length;

        Bytes out;

        out.append(info);
        out.append(chunk, chunk + length);
        out.pad(sizeof(uint32_t));

        controlWrite(MT_FCE_DMA_ADDR, address, MT_VEND_WRITE_CFG);
        controlWrite(MT_FCE_DMA_LEN, length << 16, MT_VEND_WRITE_CFG);

        if (!bulkWrite(MT_EP_WRITE, out))
        {
            Log::error("Failed to write firmware chunk");

            return false;
        }

        uint32_t complete = (length << 16) | MT_DMA_COMPLETE;

        while (controlRead(MT_FCE_DMA_LEN, MT_VEND_READ_CFG) != complete);
    }

    return true;
}

void MT76::initChip()
{
    uint16_t version = controlRead(MT_ASIC_VERSION) >> 16;

    Log::debug("Chip version: %x", version);

    // Select RX ring buffer 1
    // Turn radio on
    // Load BBP command register
    if (
        !selectFunction(Q_SELECT, 1) ||
        !powerMode(RADIO_ON) ||
        !loadCr(MT_RF_BBP_CR)
    ) {
        throw MT76Exception("Failed to init radio");
    }

    // Write initial register values
    if (!initRegisters())
    {
        throw MT76Exception("Failed to init registers");
    }

    // Turn off LED
    if (!setLedMode(MT_LED_OFF))
    {
        throw MT76Exception("Failed to turn off LED");
    }

    controlWrite(MT_MAC_SYS_CTRL, 0);

    // Calibrate chip
    if (
        !calibrate(MCU_CAL_TEMP_SENSOR, 0) ||
        !calibrate(MCU_CAL_RXDCOC, 1) ||
        !calibrate(MCU_CAL_RC, 0)
    ) {
        throw MT76Exception("Failed to calibrate chip");
    }

    controlWrite(
        MT_MAC_SYS_CTRL,
        MT_MAC_SYS_CTRL_ENABLE_TX | MT_MAC_SYS_CTRL_ENABLE_RX
    );

    // Set default channel
    if (!switchChannel(MT_CHANNEL))
    {
        throw MT76Exception("Failed to set channel");
    }

    // Write MAC address
    if (!initGain(0, macAddress))
    {
        throw MT76Exception("Failed to init gain");
    }

    // Start beacon transmission
    if (!writeBeacon())
    {
        throw MT76Exception("Failed to write beacon");
    }
}

bool MT76::writeBeacon(bool pairing)
{
    const Bytes broadcastAddress = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    // Required for clients to connect reliably
    // Probably contains the selected channel pair
    // 00 -> a5 and 30 -> 99
    const Bytes beaconData = {
        0xdd, 0x10, 0x00, 0x50,
        0xf2, 0x11, 0x01, 0x10,
        pairing, 0xa5, 0x30, 0x99,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00
    };

    TxWi txWi = {};

    // OFDM transmission method
    // Generate beacon timestamp
    // Use hardware sequence control
    txWi.phyType = MT_PHY_TYPE_OFDM;
    txWi.timestamp = 1;
    txWi.nseq = 1;
    txWi.mpduByteCount = sizeof(WlanFrame) + sizeof(BeaconFrame) + beaconData.size();

    WlanFrame wlanFrame = {};

    wlanFrame.frameControl.type = MT_WLAN_MGMT;
    wlanFrame.frameControl.subtype = MT_WLAN_BEACON;

    broadcastAddress.copy(wlanFrame.destination);
    macAddress.copy(wlanFrame.source);
    macAddress.copy(wlanFrame.bssId);

    BeaconFrame beaconFrame = {};

    // Default beacon interval
    // Original capability info
    // Wildcard SSID
    beaconFrame.interval = MT_BCN_DEF_INTVAL;
    beaconFrame.capabilityInfo = 0xc631;

    Bytes out;

    out.append(txWi);
    out.append(wlanFrame);
    out.append(beaconFrame);
    out.append(beaconData);

    BeaconTimeConfig config = {};

    // Enable timing synchronization function (TSF) timer
    // Enable target beacon transmission time (TBTT) timer
    // Set TSF timer to AP mode
    // Activate beacon transmission
    config.value = controlRead(MT_BEACON_TIME_CFG);
    config.props.tsfTimerEnabled = 1;
    config.props.tbttTimerEnabled = 1;
    config.props.tsfSyncMode = 3;
    config.props.transmitBeacon = 1;

    if (!burstWrite(MT_BEACON_BASE, out))
    {
        Log::error("Failed to write beacon");

        return false;
    }

    controlWrite(MT_BEACON_TIME_CFG, config.value);

    if (!calibrate(MCU_CAL_RXDCOC, 0))
    {
        Log::error("Failed to calibrate beacon");

        return false;
    }

    return true;
}

bool MT76::selectFunction(McuFunction function, uint32_t value)
{
    Bytes data;

    data.append(function);
    data.append(value);

    if (!sendCommand(CMD_FUN_SET_OP, data))
    {
        Log::error("Failed to select function");

        return false;
    }

    return true;
}

bool MT76::powerMode(McuPowerMode mode)
{
    Bytes data;

    data.append(mode);

    if (!sendCommand(CMD_POWER_SAVING_OP, data))
    {
        Log::error("Failed to set power mode");

        return false;
    }

    return true;
}

bool MT76::loadCr(McuCrMode mode)
{
    Bytes data;

    data.append(mode);

    if (!sendCommand(CMD_LOAD_CR, data))
    {
        Log::error("Failed to load CR");

        return false;
    }

    return true;
}

bool MT76::burstWrite(uint32_t index, const Bytes &values)
{
    index += MT_BURST_WRITE;

    Bytes data;

    data.append(index);
    data.append(values);

    if (!sendCommand(CMD_BURST_WRITE, data))
    {
        Log::error("Failed to burst write register");

        return false;
    }

    return true;
}

bool MT76::calibrate(McuCalibration calibration, uint32_t value)
{
    Bytes data;

    data.append(calibration);
    data.append(value);

    if (!sendCommand(CMD_CALIBRATION_OP, data))
    {
        Log::error("Failed to calibrate");

        return false;
    }

    return true;
}

bool MT76::switchChannel(uint8_t channel)
{
    SwitchChannelMessage message = {};

    // Set channel to switch to
    // Select TX and RX stream 1
    message.channel = channel;
    message.txRxSetting = 0x0101;

    Bytes data;

    data.append(message);

    if (!sendCommand(CMD_SWITCH_CHANNEL_OP, data))
    {
        Log::error("Failed to switch channel");

        return false;
    }

    return true;
}

bool MT76::initGain(uint32_t index, const Bytes &values)
{
    Bytes data;

    data.append(index);
    data.append(values);

    if (!sendCommand(CMD_INIT_GAIN_OP, data))
    {
        Log::error("Failed to init gain");

        return false;
    }

    return true;
}

bool MT76::setLedMode(uint32_t index)
{
    Bytes data;

    data.append(index);

    if (!sendCommand(CMD_LED_MODE_OP, data))
    {
        Log::error("Failed to set LED mode");

        return false;
    }

    return true;
}

bool MT76::sendCommand(McuCommand command, const Bytes &data)
{
    // Values must be 32-bit aligned
    // 32 zero-bits mark the end
    uint32_t length = data.size();
    uint8_t padding = length % sizeof(uint32_t);

    // We ignore responses, sequence number is always zero
    TxInfoCommand info = {};

    info.port = CPU_TX_PORT;
    info.infoType = CMD_PACKET;
    info.command = command;
    info.length = length + padding;

    Bytes out;

    out.append(info);
    out.append(data);
    out.pad(padding);
    out.pad(sizeof(uint32_t));

    if (!bulkWrite(MT_EP_WRITE, out))
    {
        Log::error("Failed to write command");

        return false;
    }

    return true;
}

uint32_t MT76::efuseRead(uint8_t address, uint8_t index)
{
    EfuseControl control = {};

    // Set address to read from
    // Kick-off read
    control.value = controlRead(MT_EFUSE_CTRL);
    control.props.mode = 0;
    control.props.addressIn = address;
    control.props.kick = 1;

    controlWrite(MT_EFUSE_CTRL, control.value);

    while (controlRead(MT_EFUSE_CTRL) & MT_EFUSE_CTRL_KICK);

    return controlRead(MT_EFUSE_DATA(index));
}

uint32_t MT76::controlRead(uint16_t address, VendorRequest request)
{
    uint32_t response = 0;
    ControlPacket packet = {};

    packet.request = request;
    packet.index = address;
    packet.data = reinterpret_cast<uint8_t*>(&response);
    packet.length = sizeof(response);

    controlTransfer(packet);

    return response;
}

void MT76::controlWrite(
    uint16_t address,
    uint32_t value,
    VendorRequest request
) {
    ControlPacket packet = {};

    packet.out = true;
    packet.request = request;

    if (request == MT_VEND_DEV_MODE)
    {
        packet.value = address;
    }

    else
    {
        packet.index = address;
        packet.data = reinterpret_cast<uint8_t*>(&value);
        packet.length = sizeof(value);
    }

    controlTransfer(packet);
}

MT76Exception::MT76Exception(std::string message)
    : std::runtime_error(message) {}
