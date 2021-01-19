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

#include "logger_instance.h"

class Bytes;

inline const int LOG_BUFFER_SIZE = 128;
#define FORMAT_BUFFER(buffer_name) \
    char buffer_name[LOG_BUFFER_SIZE]; \
    std::snprintf(buffer_name, LOG_BUFFER_SIZE, message.c_str(), args...)

/*
 * Provides logging functions for different log levels
 * Debug logging can be enabled by defining DEBUG
 */
namespace Log
{
    std::string formatBytes(const Bytes &bytes);

    inline void init()
    {
        LoggerInstance::logger().init();
    }

    inline void debug(std::string message)
    {
        #ifdef DEBUG
        LoggerInstance::logger().sinkLog(Level::LOG_DEBUG, message);
        #endif
    }

    template<typename... Args>
    inline void debug(std::string message, Args... args)
    {
        #ifdef DEBUG
        FORMAT_BUFFER(formated_message);
        LoggerInstance::logger().sinkLog(Level::LOG_DEBUG, formated_message);
        #endif
    }

    inline void info(std::string message)
    {
        LoggerInstance::logger().sinkLog(Level::LOG_INFO, message);
    }

    template<typename... Args>
    inline void info(std::string message, Args... args)
    {
        FORMAT_BUFFER(formated_message);
        LoggerInstance::logger().sinkLog(Level::LOG_INFO, formated_message);
    }

    inline void error(std::string message)
    {
        LoggerInstance::logger().sinkLog(Level::LOG_ERROR, message);
    }

    template<typename... Args>
    inline void error(std::string message, Args... args)
    {
        FORMAT_BUFFER(formated_message);
        LoggerInstance::logger().sinkLog(Level::LOG_ERROR, formated_message);
    }
}
