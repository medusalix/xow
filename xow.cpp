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

#include "utils/log.h"
#include "utils/reader.h"
#include "dongle/usb.h"
#include "dongle/dongle.h"

#include <getopt.h>

#include <cstring>
#include <csignal>
#include <iostream>
#include <sys/signalfd.h>

void usage(const char* bin_path) {
    std::cout << "xow (c) 2019 Medusalix" << std::endl;
    std::cout << bin_path << ": " << std::endl;
    std::cout << "-h | --help       Show this help message" << std::endl;
    std::cout << "-v | --version    Print version" << std::endl;
}

int parseArgs(int argc, char* argv[])
{
    static struct option long_options[] = {
        {"help",      no_argument,        0,  'h'},
        {"version",   no_argument,        0,  'v'},
        {0,           0,                  0,  0  }};

    while(1) {
        int option;

        option = getopt_long(argc, argv, "hv", long_options, 0);
        if(option == -1)
        {
            break;
        }

        switch(option) {
            case 'h':
                usage(argv[0]);
                return 0;
            case 'v':
                std::cout << "xow " << VERSION << std::endl;
                return 0;
            case '?':
            default:
                usage(argv[0]);
                return -1;
        }
    }

    return 1;
}

int main(int argc, char* argv[])
{
    switch(parseArgs(argc, argv)) {
        case 1: break;
        case 0: return 0;
        default: return -1;
    }

    Log::init();
    Log::info("xow %s ©Severin v. W.", VERSION);

    sigset_t signalMask;

    sigemptyset(&signalMask);
    sigaddset(&signalMask, SIGINT);
    sigaddset(&signalMask, SIGTERM);
    sigaddset(&signalMask, SIGUSR1);

    // Block signals for all USB threads
    if (pthread_sigmask(SIG_BLOCK, &signalMask, nullptr) < 0)
    {
        Log::error("Error blocking signals: %s", strerror(errno));

        return EXIT_FAILURE;
    }

    UsbDeviceManager manager;

    // Unblock signals for current thread to allow interruption
    if (pthread_sigmask(SIG_UNBLOCK, &signalMask, nullptr) < 0)
    {
        Log::error("Error unblocking signals: %s", strerror(errno));

        return EXIT_FAILURE;
    }

    // Bind USB device termination to signal reader interruption
    InterruptibleReader signalReader;
    UsbDevice::Terminate terminate = std::bind(
        &InterruptibleReader::interrupt,
        &signalReader
    );
    std::unique_ptr<UsbDevice> device = manager.getDevice({
        { DONGLE_VID, DONGLE_PID_OLD },
        { DONGLE_VID, DONGLE_PID_NEW },
        { DONGLE_VID, DONGLE_PID_SURFACE }
    }, terminate);

    // Block signals and pass them to the signalfd
    if (pthread_sigmask(SIG_BLOCK, &signalMask, nullptr) < 0)
    {
        Log::error("Error blocking signals: %s", strerror(errno));

        return EXIT_FAILURE;
    }

    int file = signalfd(-1, &signalMask, 0);

    if (file < 0)
    {
        Log::error("Error creating signal file: %s", strerror(errno));

        return EXIT_FAILURE;
    }

    signalReader.prepare(file);

    Dongle dongle(std::move(device));
    signalfd_siginfo info = {};

    while (signalReader.read(&info, sizeof(info)))
    {
        uint32_t type = info.ssi_signo;

        if (type == SIGINT || type == SIGTERM)
        {
            break;
        }

        if (type == SIGUSR1)
        {
            Log::debug("User signal received");

            dongle.setPairingStatus(true);
        }
    }

    Log::info("Shutting down...");

    return EXIT_SUCCESS;
}
