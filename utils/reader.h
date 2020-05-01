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

#pragma once

#include <mutex>
#include <sys/types.h>
#include <poll.h>

/*
 * Interruptible reader for file descriptors
 */
class InterruptibleReader
{
public:
    void prepare(int file);
    bool read(void *data, ssize_t size);
    void interrupt();

private:
    std::mutex prepareMutex;
    bool prepared = false;

    int pipes[2];
    pollfd polls[2];
};
