<p align="center">
    <img src="logo.png" alt="Logo">
</p>

<p align="center">
    <a href="https://github.com/medusalix/xow/actions">
        <img src="https://img.shields.io/github/workflow/status/medusalix/xow/Continuous%20Integration" alt="Build Badge">
    </a>
    <a href="https://github.com/medusalix/xow/releases/latest">
        <img src="https://img.shields.io/github/v/release/medusalix/xow" alt="Release Badge">
    </a>
    <a href="https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=PLN6F3UGS37DE&lc=US">
        <img src="https://www.paypalobjects.com/en_US/i/btn/btn_donate_SM.gif" alt="Donate Button">
    </a>
</p>

<p align="center">
  <img src="screenshot.png" alt="Screenshot">
</p>

## About

xow is a Linux user mode driver for the Xbox One wireless dongle.
It communicates with the dongle via `libusb` and provides joystick input through the `uinput` kernel module.
The input mapping is based on existing kernel drivers like [xpad](https://github.com/paroj/xpad).

**NOTE:** xow is still at a **VERY EARLY** stage of development. Do not be surprised if it does not work *at all*.
In case of problems, please open an issue with all the relevant details (dongle version, controller version, logs, captures, etc.) and I will see what I can do.

## Supported devices

xow supports both versions of the wireless dongle (slim and bulky one).
The following Xbox One controllers are currently compatible with the driver:

| Model number | Year | Additional information    | Status       |
|--------------|------|---------------------------|--------------|
| 1537         | 2013 | Original controller       | **Working**  |
| 1697         | 2015 | Audio jack                | **Working**  |
| 1698         | 2015 | Elite controller          | **Working**  |
| 1708         | 2016 | Bluetooth connectivity    | **Working**  |
| 1797         | 2019 | Elite controller series 2 | **Working**  |
| 1914         | 2020 | Share button and USB-C    | **Untested** |

## Releases

### Linux distributions

- Arch Linux ([master](https://aur.archlinux.org/packages/xow-git), [stable](https://aur.archlinux.org/packages/xow))
- Debian ([sid](https://packages.debian.org/sid/xow))

### Third-party hardware

- EmuELEC (starting with [version 3.3](https://github.com/EmuELEC/EmuELEC/releases/tag/v3.3))
- GamerOS (starting with [version 13](https://github.com/gamer-os/gamer-os/releases/tag/13))
- Steam Link (starting with [build 747](https://steamcommunity.com/app/353380/discussions/0/1735510154204276395))

Feel free to create prebuilt releases of xow for any Linux distribution or hardware you like.
Any issues regarding the packaging should be reported to the respective maintainers.

## Installation

### Prerequisites

- Linux (kernel 4.5 or newer)
- libusb (libusb-1.0-0-dev for Debian)
- systemd (version 232 or newer)

Clone the repository (necessary for version tagging to work):

```
git clone https://github.com/medusalix/xow
```

Build xow using the following command:

```
make BUILD=RELEASE
```

**NOTE:** Please use `BUILD=DEBUG` when asked for your debug logs.

Install xow as a `systemd` service (starts xow at boot time):

```
sudo make install
sudo systemctl enable xow
sudo systemctl start xow
```

**NOTE:** A reboot might be required for xow to work correctly.

### Updating

Make sure to completely uninstall xow before updating:

```
sudo systemctl stop xow
sudo systemctl disable xow
sudo make uninstall
```

## Troubleshooting

- Buttons/triggers/sticks are mapped incorrectly
    - Try the options listed on [this page](https://wiki.archlinux.org/index.php/Gamepad#Setting_up_deadzones_and_calibration) to remap your inputs.
- Input from the sticks is jumping around
    - Try the options listed on [this page](https://wiki.archlinux.org/index.php/Gamepad#Setting_up_deadzones_and_calibration) to set your deadzones.
- Controller does not connect to the dongle
    - See [supported devices](#supported-devices). Do a packet capture and open an issue.

**NOTE:** Please refrain from opening issues concerning input remapping, deadzones or game compatibility, as these topics are outside the scope of this project.

## How it works

The dongle's wireless chip (MT76xx) handles the WLAN connection with individual controllers.
The packet format follows Microsoft's undisclosed GIP (Game Input Protocol) specification.
Most of the reverse engineering was done by capturing the communication between the dongle and a Windows PC using [`Wireshark`](https://www.wireshark.org).
As no datasheets for this chip are publicly available, I have used datasheets of similar wireless radios for assistance.
Special thanks to the authors of OpenWrt's [`mt76`](https://github.com/openwrt/mt76) kernel driver.
It would have been impossible for me to create this driver without `mt76`'s source code.
If anyone has a greater understanding of the GIP or the weird quirks I had to add to make the driver work (like `initGain`), please contact me.

## License

xow is released under the [GNU General Public License, Version 2](LICENSE).

```
Copyright (C) 2019 Medusalix

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
```

The included `firmware.bin` file, extracted from the Windows drivers, is provided under a [special proprietary license](LICENSE-FIRMWARE).
