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

extern uint8_t _binary_firmware_bin_start[];
extern uint8_t _binary_firmware_bin_end[];

Mt76::Mt76(
    std::unique_ptr<UsbDevice> usbDevice
) : usbDevice(std::move(usbDevice)), connectedWcids(0)
{
    if (!loadFirmware())
    {
        throw Mt76Exception("Failed to load firmware");
    }

    // Select RX ring buffer 1
    // Turn radio on
    // Load BBP command register
    if (
        !selectFunction(Q_SELECT, 1) ||
        !powerMode(RADIO_ON) ||
        !loadCr(MT_RF_BBP_CR)
    ) {
        throw Mt76Exception("Failed to init radio");
    }

    if (!initRegisters())
    {
        throw Mt76Exception("Failed to init registers");
    }

    if (!initGain(0, macAddress))
    {
        throw Mt76Exception("Failed to init gain");
    }

    // Reset necessary for reliable WLAN associations
    controlWrite(MT_MAC_SYS_CTRL, 0);
    controlWrite(MT_RF_BYPASS_0, 0);
    controlWrite(MT_RF_SETTING_0, 0);

    if (
        !calibrate(MCU_CAL_TEMP_SENSOR, 0) ||
        !calibrate(MCU_CAL_RXDCOC, 1) ||
        !calibrate(MCU_CAL_RC, 0)
    ) {
        throw Mt76Exception("Failed to calibrate chip");
    }

    controlWrite(
        MT_MAC_SYS_CTRL,
        MT_MAC_SYS_CTRL_ENABLE_TX | MT_MAC_SYS_CTRL_ENABLE_RX
    );

    if (!initChannels())
    {
        throw Mt76Exception("Failed to init channels");
    }

    if (!writeBeacon())
    {
        throw Mt76Exception("Failed to write beacon");
    }
}

Mt76::~Mt76()
{
    if (!setLedMode(MT_LED_OFF))
    {
        Log::error("Failed to turn off LED");
    }

    if (!powerMode(RADIO_OFF))
    {
        Log::error("Failed to turn off radio");
    }
}

uint8_t Mt76::associateClient(Bytes address)
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

    AssociationResponseFrame associationFrame = {};

    // Original status code
    // Original association ID
    associationFrame.statusCode = 0x0110;
    associationFrame.associationId = 0x0f00;

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

bool Mt76::removeClient(uint8_t wcid)
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

bool Mt76::pairClient(Bytes address)
{
    const Bytes data = {
        0x70, 0x02, 0x00, 0x45,
        0x55, 0x01, 0x0f, 0x8f,
        0xff, 0x87, 0x1f
    };

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
    wlanFrame.frameControl.subtype = MT_WLAN_RESERVED;

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

    // Stop sending the 'pairing' beacon
    if (!writeBeacon())
    {
        Log::error("Failed to write beacon");

        return false;
    }

    if (!setLedMode(MT_LED_ON))
    {
        Log::error("Failed to set LED mode");

        return false;
    }

    return true;
}

bool Mt76::sendClientPacket(
    uint8_t wcid,
    Bytes address,
    const Bytes &packet
) {
    // Skip unconnected WCIDs
    if (!(connectedWcids & BIT(wcid - 1)))
    {
        return true;
    }

    TxWi txWi = {};

    // OFDM transmission method
    // Wait for acknowledgement
    txWi.phyType = MT_PHY_TYPE_OFDM;
    txWi.ack = 1;
    txWi.mpduByteCount = sizeof(WlanFrame) + sizeof(QosFrame) + packet.size();

    WlanFrame wlanFrame = {};

    // Frame is sent from AP (DS)
    // Duration is the time required to transmit (μs)
    wlanFrame.frameControl.type = MT_WLAN_DATA;
    wlanFrame.frameControl.subtype = MT_WLAN_QOS_DATA;
    wlanFrame.frameControl.fromDs = 1;
    wlanFrame.duration = 144;

    address.copy(wlanFrame.destination);
    macAddress.copy(wlanFrame.source);
    macAddress.copy(wlanFrame.bssId);

    QosFrame qosFrame = {};

    // Frames and data must be 32-bit aligned
    uint32_t length = sizeof(txWi) + sizeof(wlanFrame) + sizeof(qosFrame);
    uint32_t wcidData = __builtin_bswap32(wcid - 1);
    uint8_t framePadding = Bytes::padding<uint32_t>(length);
    uint8_t dataPadding = Bytes::padding<uint32_t>(packet.size());

    Bytes out;

    out.append(wcidData);
    out.pad(sizeof(uint32_t));
    out.append(txWi);
    out.append(wlanFrame);
    out.append(qosFrame);
    out.pad(framePadding);
    out.append(packet);
    out.pad(dataPadding);

    if (!sendCommand(CMD_PACKET_TX, out))
    {
        Log::error("Failed to send controller packet");

        return false;
    }

    return true;
}

bool Mt76::sendWlanPacket(const Bytes &data)
{
    // Values must be 32-bit aligned
    // 32 zero-bits mark the end
    uint32_t length = data.size();
    uint8_t padding = Bytes::padding<uint32_t>(length);

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

    if (!usbDevice->bulkWrite(MT_EP_WRITE, out))
    {
        Log::error("Failed to write WLAN packet");

        return false;
    }

    return true;
}

bool Mt76::initRegisters()
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
    controlWrite(MT_TXOP_CTRL_CFG, 0x10583f);
    controlWrite(MT_TX_TIMEOUT_CFG, 0x0a0f90);
    controlWrite(MT_TX_RETRY_CFG, 0x47d01f0f);
    controlWrite(MT_CCK_PROT_CFG, 0x03f40003);
    controlWrite(MT_OFDM_PROT_CFG, 0x03f40003);
    controlWrite(MT_MM20_PROT_CFG, 0x01742004);
    controlWrite(MT_GF20_PROT_CFG, 0x01742004);
    controlWrite(MT_GF40_PROT_CFG, 0x03f42084);
    controlWrite(MT_EXP_ACK_TIME, 0x2c00dc);
    controlWrite(MT_TX_ALC_CFG_2, 0x22160a00);
    controlWrite(MT_TX_ALC_CFG_3, 0x22160a76);
    controlWrite(MT_TX_ALC_CFG_0, 0x3f3f1818);
    controlWrite(MT_TX_ALC_CFG_4, 0x0606);
    controlWrite(MT_PIFS_TX_CFG, 0x060fff);
    controlWrite(MT_RX_FILTR_CFG, 0x017f17);
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
    controlWrite(MT_RF_BYPASS_0, 0x7f000000);
    controlWrite(MT_RF_SETTING_0, 0x1a800000);
    controlWrite(MT_XIFS_TIME_CFG, 0x33a40e0a);
    controlWrite(MT_FCE_L2_STUFF, 0x03ff0223);
    controlWrite(MT_TX_RTS_CFG, 0);
    controlWrite(MT_BEACON_TIME_CFG, 0x0640);
    controlWrite(MT_EXT_CCA_CFG, 0xf0e4);
    controlWrite(MT_CH_TIME_CFG, 0x015f);

    // Calibrate internal crystal oscillator
    calibrateCrystal();

    // Configure automatic gain control (AGC)
    controlWrite(MT_BBP(AGC, 8), 0x18365efa);
    controlWrite(MT_BBP(AGC, 9), 0x18365efa);

    macAddress = efuseRead(MT_EE_MAC_ADDR, 6);

    // Some dongles' addresses start with 6c:5d:3a
    // Controllers only connect to 62:45:bx:xx:xx:xx
    if (macAddress[0] != 0x62)
    {
        Log::debug("Invalid MAC address, correcting...");

        macAddress[0] = 0x62;
        macAddress[1] = 0x45;
        macAddress[2] = 0xbd;
    }

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

    uint16_t asicVersion = controlRead(MT_ASIC_VERSION) >> 16;
    uint16_t macVersion = controlRead(MT_MAC_CSR0) >> 16;
    Bytes chipId = efuseRead(MT_EE_CHIP_ID, sizeof(uint32_t));
    uint16_t id = (chipId[1] << 8) | chipId[2];

    Log::debug("ASIC version: %x", asicVersion);
    Log::debug("MAC version: %x", macVersion);
    Log::debug("Chip id: %x", id);
    Log::info("Wireless address: %s", Log::formatBytes(macAddress).c_str());

    return true;
}

void Mt76::calibrateCrystal()
{
    Bytes trim = efuseRead(MT_EE_XTAL_TRIM_2, sizeof(uint32_t));
    uint16_t value = (trim[3] << 8) | trim[2];
	int8_t offset = value & 0x7f;

	if ((value & 0xff) == 0xff)
    {
		offset = 0;
    }

	else if (value & 0x80)
    {
		offset = -offset;
    }

	value >>= 8;

	if (value == 0x00 || value == 0xff)
    {
		trim = efuseRead(MT_EE_XTAL_TRIM_1, sizeof(uint32_t));
        value = (trim[3] << 8) | trim[2];
		value &= 0xff;

		if (value == 0x00 || value == 0xff)
        {
			value = 0x14;
        }
	}

	value = (value & 0x7f) + offset;

    uint32_t ctrl = controlRead(MT_XO_CTRL5) & ~MT_XO_CTRL5_C2_VAL;

	controlWrite(MT_XO_CTRL5, ctrl | (value << 8), MT_VEND_WRITE_CFG);
	controlWrite(MT_XO_CTRL6, MT_XO_CTRL6_C2_CTRL, MT_VEND_WRITE_CFG);
    controlWrite(MT_CMB_CTRL, 0x0091a7ff);
}

bool Mt76::initChannels()
{
    // Configure and enable/disable each individual channel
    configureChannel(0x01, 0x00, 0x1f, true);
    configureChannel(0x06, 0x00, 0x20, true);
    configureChannel(0x0b, 0x00, 0x21, true);
    configureChannel(0x24, 0x01, 0x2f, true);
    configureChannel(0x28, 0x01, 0x2f, false);
    configureChannel(0x2c, 0x01, 0x2f, true);
    configureChannel(0x30, 0x01, 0x2f, false);
    configureChannel(0x95, 0x02, 0x29, true);
    configureChannel(0x99, 0x02, 0x29, false);
    configureChannel(0x9d, 0x02, 0x28, true);
    configureChannel(0xa1, 0x02, 0x28, false);
    configureChannel(0xa5, 0x02, 0x28, false);

    // List of wireless channel candidates
    const Bytes candidates = {
        0x01, 0xa5,
        0x0b, 0x01,
        0x06, 0x0b,
        0x24, 0x28,
        0x2c, 0x30,
        0x95, 0x99,
        0x9d, 0xa1
    };
    Bytes values;

    // Map channels to 32-bit values
    for (uint32_t channel : candidates)
    {
        values.append(channel);
    }

    if (!initGain(7, values))
    {
        Log::error("Failed to send channel candidates");

        return false;
    }

    return true;
}

bool Mt76::loadFirmware()
{
    if (controlRead(MT_FCE_DMA_ADDR, MT_VEND_READ_CFG))
    {
        Log::debug("Firmware already loaded, resetting...");

        uint32_t patch = controlRead(MT_RF_PATCH, MT_VEND_READ_CFG);

        patch &= ~BIT(19);

        // Mandatory for already initialized radios
        controlWrite(MT_RF_PATCH, patch, MT_VEND_WRITE_CFG);
        controlWrite(MT_FW_RESET_IVB, 0, MT_VEND_DEV_MODE);

        // Wait for firmware to reset
        while (controlRead(MT_FCE_DMA_ADDR, MT_VEND_READ_CFG) != 0x80000000);
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

    // Upload instruction local memory (ILM)
    if (!loadFirmwarePart(MT_MCU_ILM_OFFSET, ilmStart, dlmStart))
    {
        Log::error("Failed to write ILM");

        return false;
    }

    // Upload data local memory (DLM)
    if (!loadFirmwarePart(MT_MCU_DLM_OFFSET, dlmStart, dlmEnd))
    {
        Log::error("Failed to write DLM");

        return false;
    }

    // Load initial vector block (IVB)
    controlWrite(MT_FCE_DMA_ADDR, 0, MT_VEND_WRITE_CFG);
    controlWrite(MT_FW_LOAD_IVB, 0, MT_VEND_DEV_MODE);

    // Wait for firmware to start
    while (controlRead(MT_FCE_DMA_ADDR, MT_VEND_READ_CFG) != 0x01);

    Log::debug("Firmware loaded");

    return true;
}

bool Mt76::loadFirmwarePart(
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

        if (!usbDevice->bulkWrite(MT_EP_WRITE, out))
        {
            Log::error("Failed to write firmware chunk");

            return false;
        }

        uint32_t complete = (length << 16) | MT_DMA_COMPLETE;

        while (controlRead(MT_FCE_DMA_LEN, MT_VEND_READ_CFG) != complete);
    }

    return true;
}

bool Mt76::writeBeacon(bool pairing)
{
    const Bytes broadcastAddress = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    // Contains an information element (ID: 0xdd, Length: 0x10)
    // Probably includes the selected channel pair
    const Bytes data = {
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
    txWi.mpduByteCount = sizeof(WlanFrame) + sizeof(BeaconFrame) + data.size();

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
    out.append(data);

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

bool Mt76::selectFunction(McuFunction function, uint32_t value)
{
    Bytes out;

    out.append(function);
    out.append(value);

    if (!sendCommand(CMD_FUN_SET_OP, out))
    {
        Log::error("Failed to select function");

        return false;
    }

    return true;
}

bool Mt76::powerMode(McuPowerMode mode)
{
    Bytes out;

    out.append(mode);

    if (!sendCommand(CMD_POWER_SAVING_OP, out))
    {
        Log::error("Failed to set power mode");

        return false;
    }

    return true;
}

bool Mt76::loadCr(McuCrMode mode)
{
    Bytes out;

    out.append(mode);

    if (!sendCommand(CMD_LOAD_CR, out))
    {
        Log::error("Failed to load CR");

        return false;
    }

    return true;
}

bool Mt76::burstWrite(uint32_t index, const Bytes &values)
{
    index += MT_REG_OFFSET;

    Bytes out;

    out.append(index);
    out.append(values);

    if (!sendCommand(CMD_BURST_WRITE, out))
    {
        Log::error("Failed to burst write register");

        return false;
    }

    return true;
}

bool Mt76::calibrate(McuCalibration calibration, uint32_t value)
{
    Bytes out;

    out.append(calibration);
    out.append(value);

    if (!sendCommand(CMD_CALIBRATION_OP, out))
    {
        Log::error("Failed to calibrate");

        return false;
    }

    return true;
}

bool Mt76::configureChannel(
    uint8_t channel,
    uint8_t group,
    uint8_t txPower,
    bool enabled
) {
    ChannelConfigData config = {};

    // Select TX and RX stream 1
    // Set transmit power (from 0x00 to 0x2f)
    // Set channel group (unknown purpose)
    // Enable or disable channel
    config.channel = channel;
    config.txRxSetting = 0x0101;
    config.group = group;
    config.txPower = txPower;
    config.enabled = enabled;

    Bytes out;

    out.append(config);

    if (!sendCommand(CMD_SWITCH_CHANNEL_OP, out))
    {
        Log::error("Failed to switch channel");

        return false;
    }

    return true;
}

bool Mt76::initGain(uint32_t index, const Bytes &values)
{
    Bytes out;

    out.append(index);
    out.append(values);

    if (!sendCommand(CMD_INIT_GAIN_OP, out))
    {
        Log::error("Failed to init gain");

        return false;
    }

    return true;
}

bool Mt76::setLedMode(uint32_t index)
{
    Bytes out;

    out.append(index);

    if (!sendCommand(CMD_LED_MODE_OP, out))
    {
        Log::error("Failed to set LED mode");

        return false;
    }

    return true;
}

bool Mt76::sendCommand(McuCommand command, const Bytes &data)
{
    // Values must be 32-bit aligned
    // 32 zero-bits mark the end
    uint32_t length = data.size();
    uint8_t padding = Bytes::padding<uint32_t>(length);

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

    if (!usbDevice->bulkWrite(MT_EP_WRITE, out))
    {
        Log::error("Failed to write command");

        return false;
    }

    return true;
}

Bytes Mt76::efuseRead(uint8_t address, uint8_t length)
{
    EfuseControl control = {};

    // Set address to read from
    // Kick-off read
    control.value = controlRead(MT_EFUSE_CTRL);
    control.props.mode = 0;
    control.props.addressIn = address & 0xf0;
    control.props.kick = 1;

    controlWrite(MT_EFUSE_CTRL, control.value);

    while (controlRead(MT_EFUSE_CTRL) & MT_EFUSE_CTRL_KICK);

    Bytes data;

    for (uint8_t i = 0; i < length; i += sizeof(uint32_t))
    {
        uint8_t offset = i + (address & 0x0c);
        uint32_t value = controlRead(MT_EFUSE_DATA_BASE + offset);
        uint8_t remaining = length - i;
        uint8_t size = remaining < sizeof(uint32_t)
            ? remaining
            : sizeof(uint32_t);

        data.append(value, size);
    }

    return data;
}

uint32_t Mt76::controlRead(uint16_t address, VendorRequest request)
{
    uint32_t response = 0;
    UsbDevice::ControlPacket packet = {};

    packet.request = request;
    packet.index = address;
    packet.data = reinterpret_cast<uint8_t*>(&response);
    packet.length = sizeof(response);

    usbDevice->controlTransfer(packet, false);

    return response;
}

void Mt76::controlWrite(
    uint16_t address,
    uint32_t value,
    VendorRequest request
) {
    UsbDevice::ControlPacket packet = {};

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

    usbDevice->controlTransfer(packet, true);
}

Mt76Exception::Mt76Exception(
    std::string message
) : std::runtime_error(message) {}
