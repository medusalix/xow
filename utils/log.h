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

#define LOG_DEBUG 7
#define LOG_INFO 6
#define LOG_ERROR 3

class Bytes;

/*
 * Provides logging functions for different log levels
 * Debug logging can be enabled by defining DEBUG
 */
namespace Log
{
    std::string formatBytes(const Bytes &bytes);
    std::string formatLog(const int syslog_level, std::string message);

    inline void init()
    {
        // Switch to line buffering
        setlinebuf(stdout);
    }

    inline void debug(std::string message)
    {
        #ifdef DEBUG
        std::string output = formatLog(LOG_DEBUG, message);
        std::fputs(output.c_str(), stdout);
        #endif
    }

    template<typename... Args>
    inline void debug(std::string message, Args... args)
    {
        #ifdef DEBUG
        std::string output = formatLog(LOG_DEBUG, message);
        std::fprintf(stdout, output.c_str(), args...);
        #endif
    }

    inline void info(std::string message)
    {
        std::string output = formatLog(LOG_INFO, message);
        std::fputs(output.c_str(), stdout);
    }

    template<typename... Args>
    inline void info(std::string message, Args... args)
    {
        std::string output = formatLog(LOG_INFO, message);
        std::fprintf(stdout, output.c_str(), args...);
    }

    inline void error(std::string message)
    {
        std::string output = formatLog(LOG_ERROR, message);
        std::fputs(output.c_str(), stderr);
    }

    template<typename... Args>
    inline void error(std::string message, Args... args)
    {
        std::string output = formatLog(LOG_ERROR, message);
        std::fprintf(stderr, output.c_str(), args...);
    }
}
