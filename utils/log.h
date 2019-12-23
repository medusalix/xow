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

#include <string>
#include <mutex>
#include <cstdio>

class Bytes;

/*
 * Provides logging functions for different log levels
 * Debug logging can be enabled by defining DEBUG
 */
namespace Log
{
    extern std::mutex mutex;

    std::string formatBytes(const Bytes &bytes);

    void printHeader(
        FILE *output,
        std::string level,
        std::string message
    );

    inline void init()
    {
        // Disable buffering
        setbuf(stdout, nullptr);
    }

    inline void debug(std::string message)
    {
        #ifdef DEBUG
        std::lock_guard<std::mutex> lock(mutex);

        printHeader(stdout, "DEBUG", message);

        std::fputs(message.c_str(), stdout);
        std::fputs("\n", stdout);
        #endif
    }

    template<typename... Args>
    inline void debug(std::string message, Args... args)
    {
        #ifdef DEBUG
        std::lock_guard<std::mutex> lock(mutex);

        printHeader(stdout, "DEBUG", message);

        std::fprintf(stdout, message.c_str(), args...);
        std::fprintf(stdout, "\n");
        #endif
    }

    inline void info(std::string message)
    {
        std::lock_guard<std::mutex> lock(mutex);

        printHeader(stdout, "INFO", message);

        std::fputs(message.c_str(), stdout);
        std::fputs("\n", stdout);
    }

    template<typename... Args>
    inline void info(std::string message, Args... args)
    {
        std::lock_guard<std::mutex> lock(mutex);

        printHeader(stdout, "INFO", message);

        std::fprintf(stdout, message.c_str(), args...);
        std::fprintf(stdout, "\n");
    }

    inline void error(std::string message)
    {
        std::lock_guard<std::mutex> lock(mutex);

        printHeader(stderr, "ERROR", message);

        std::fputs(message.c_str(), stderr);
        std::fputs("\n", stderr);
    }

    template<typename... Args>
    inline void error(std::string message, Args... args)
    {
        std::lock_guard<std::mutex> lock(mutex);

        printHeader(stderr, "ERROR", message);

        std::fprintf(stderr, message.c_str(), args...);
        std::fprintf(stderr, "\n");
    }
}
