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

#include <iomanip>
#include <map>
#include <sstream>
#include <thread>

#include "bytes.h"
#include "log.h"

static std::map<int, std::string> map_level = {
    {LOG_DEBUG, "DEBUG"},
    {LOG_INFO, "INFO"},
    {LOG_ERROR, "ERROR"},
};

namespace Log
{
    std::string formatBytes(const Bytes &bytes)
    {
        std::ostringstream stream;

        stream << std::hex << std::setfill('0');

        for (uint8_t byte : bytes)
        {
            stream << std::setw(2);
            stream << static_cast<uint32_t>(byte) << ':';
        }

        std::string output = stream.str();

        // Remove trailing colon
        output.pop_back();

        return output;
    }

    std::string formatLog(const int syslog_level, std::string message) {
        std::ostringstream stream;

        // Add local time to output if available and not logging to journald
        if (!getenv("JOURNAL_STREAM")) {
            std::time_t time = std::time(nullptr);
            std::tm localTime = {};
            if (localtime_r(&time, &localTime)) {
                stream << std::put_time(&localTime, "%F %T") << " ";
            }
            stream << "[" << std::left << std::setw(5);
            stream << map_level[syslog_level] << "] [";
        } else {
            stream << "<" << syslog_level << ">[";
        }

        stream << std::hex << std::setw(16) << std::right;
        stream << std::this_thread::get_id() << "] - ";
        stream << message << std::endl;

        return stream.str();
    }
}
