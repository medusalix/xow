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
#include <syslog.h>

#include "logger_syslog.h"

namespace Log
{

LoggerSyslog::~LoggerSyslog() {
    closelog();
}

int LoggerSyslog::logLevelToSyslog(Level level) {
    switch(level) {
        case Level::LOGLEVEL_DEBUG:
            return LOG_DEBUG;
        case Level::LOGLEVEL_INFO:
            return LOG_INFO;
        default:
            return LOG_ERR;
    }
}

void LoggerSyslog::init() {
    openlog(nullptr, LOG_CONS, LOG_DAEMON);
}

void LoggerSyslog::sinkLog(Level level, const std::string& message) {
    syslog(logLevelToSyslog(level), "%s", message.c_str());
}

}
