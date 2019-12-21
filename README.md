# xow [![Build Status](https://img.shields.io/travis/com/medusalix/xow)](https://travis-ci.com/medusalix/xow) [![Release](https://img.shields.io/github/v/release/medusalix/xow)](https://github.com/medusalix/xow/releases/latest)

<p align="center">
  <img src="screenshot.png" alt="Screenshot">
</p>

xow is a Linux user mode driver for the Xbox One wireless dongle.
It communicates with the dongle via `libusb` and provides joystick input through the `uinput` kernel module.
The input mapping is based on existing kernel drivers like [xpad](https://github.com/paroj/xpad).

**NOTE:** xow is still at a **VERY EARLY** stage of development. Do not be surprised if it does not work *at all*.
In case of problems, please open an issue with all the relevant details (dongle version, controller version, logs, captures, etc.) and I will see what I can do.

## Supported devices

xow currently only supports the newer model of the wireless dongle (the slim one).
**Additional packet captures are needed to add support for the old dongle (the bulky one).**

The driver supports the following Xbox One controllers:

| Model number | Year | Additional information  | Status       |
|--------------|------|-------------------------|--------------|
| 1537         | 2013 | Original controller     | **Untested** |
| 1697         | 2015 | Added audio jack        | **Untested** |
| 1708         | 2016 | Bluetooth functionality | **Working**  |

Elite controllers may be added in the future.

## Planned features

#### Make controller's LEDs and power settings accessible

Ideally, other applications should be able to query/set these things.

#### Automatic channel selection

The Windows driver goes through all channels and reads `MT_CH_IDLE` and `MT_CH_BUSY` to select the best one.
I have not figured out what these values are and how they are used to determine the least noisy channel.

#### Improved controller rumble

Rumble support is not finished yet and I would really like to see `ff_memless` being implemented for `uinput` devices (see [here](https://patchwork.kernel.org/patch/9039051)).
This would greatly simplify things.

Any **help/suggestions** regarding the planned features is much appreciated.

## Building

Make sure that `libusb` is installed on your machine. You can build xow using the following command:

```
make BUILD=RELEASE
```

**Option 1:** Install xow as a `systemd` service (starts xow at boot):

```
sudo make install
```

**Option 2:** Run xow manually:

```
sudo ./xow
```

xow needs root privileges to create an input device from user space.

## Troubleshooting

- Connection dropouts
    - Try adjust the radio's [`CHANNEL`](Makefile) (2.4 GHz channels might work better)
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
Special thanks to the authors of Linux' [`mt76`](https://github.com/torvalds/linux/tree/master/drivers/net/wireless/mediatek/mt76) kernel driver.
It would have been impossible for me to create this driver without `mt76`'s source code.
If anyone has a greater understanding of the GIP or the weird quirks I had to add to make the driver work (like `initGain`), please contact me.
