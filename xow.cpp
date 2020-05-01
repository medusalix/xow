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
#include "dongle/usb.h"
#include "dongle/dongle.h"

#include <cstdlib>
#include <cstring>
#include <sys/file.h>

int main()
{
    Log::init();
    Log::info("xow %s ©Severin v. W.", VERSION);

    // Open lock file, read and writable by all users
    int file = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);

    if (flock(file, LOCK_EX | LOCK_NB))
    {
        if (errno == EWOULDBLOCK)
        {
            Log::error("Another instance of xow is already running");
        }

        else
        {
            Log::error("Error creating lock file: %s", strerror(errno));
        }

        return EXIT_FAILURE;
    }

    UsbDeviceManager manager;
    Dongle dongle(manager.getDevice({
        { DONGLE_VID, DONGLE_PID_OLD },
        { DONGLE_VID, DONGLE_PID_NEW }
    }));

    manager.waitForShutdown();

    return EXIT_SUCCESS;
}
