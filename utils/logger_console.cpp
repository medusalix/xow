/*
 * Copyright (C) 2021 Medusalix
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
#include <sstream>
#include <iomanip>

#include "logger_console.h"

namespace Log
{

void LoggerConsole::init() {
    setlinebuf(stdout);
    setlinebuf(stderr);
}

void LoggerConsole::sinkLog(Level level, const std::string& message) {
    FILE* outstream = stdout;

    if(level == Level::LOGLEVEL_ERROR) {
        outstream = stderr;
    }
    std::fputs(formatLog(level, message).c_str(), outstream);
}

std::string LoggerConsole::formatLog(Level level, const std::string& message) {
    std::ostringstream stream;
    std::time_t time = std::time(nullptr);
    std::tm localTime = {};

    // Add local time to output if available
    if (localtime_r(&time, &localTime))
    {
        stream << std::put_time(&localTime, "%F %T") << " ";
    }

    stream << std::left << std::setw(5);
    stream << level << " - ";
    stream << message << std::endl;

    return stream.str();
}

}
