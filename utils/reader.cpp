/*
 * Copyright (C) 2020 Medusalix
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

#include "reader.h"
#include "log.h"

#include <cstring>
#include <unistd.h>

void InterruptibleReader::prepare(int file)
{
    std::lock_guard<std::mutex> lock(prepareMutex);

    if (pipe(pipes))
    {
        Log::error("Error creating pipes: %s", strerror(errno));

        return;
    }

    polls[0].fd = pipes[0];
    polls[1].fd = file;
    polls[0].events = POLLIN;
    polls[1].events = POLLIN;

    prepared = true;
}

void InterruptibleReader::interrupt()
{
    std::lock_guard<std::mutex> lock(prepareMutex);

    if (!prepared)
    {
        return;
    }

    bool stop = true;
    ssize_t size = sizeof(stop);

    if (write(pipes[1], &stop, size) != size)
    {
        Log::error("Error writing stop signal: %s", strerror(errno));
    }

    prepared = false;
}

bool InterruptibleReader::read(void *data, ssize_t size)
{
    // Wait for an event or a stop signal
    int error = poll(polls, 2, -1);

    if (error < 0)
    {
        Log::error("Poll failed: %s", strerror(errno));

        return false;
    }

    // Event loop was interrupted
    if (polls[0].revents & POLLIN)
    {
        if (close(pipes[0]) < 0 || close(pipes[1]) < 0)
        {
            Log::error("Error closing pipes: %s", strerror(errno));
        }

        return false;
    }

    // Data is available
    if (polls[1].revents & POLLIN)
    {
        return ::read(polls[1].fd, data, size) == size;
    }

    return false;
}
