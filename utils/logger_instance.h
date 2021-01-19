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
#ifndef XOW_LOGGER_INSTANCE_H_
#define XOW_LOGGER_INSTANCE_H_

#include <memory>

#include "logger_console.h"
#include "logger_syslog.h"

namespace Log
{

class LoggerInstance {
public:
    LoggerInstance(const LoggerInstance&) = delete;
    void operator=(const LoggerInstance&) = delete;
    ~LoggerInstance() = default;

    static LoggerInstance& instance() {
        static LoggerInstance instance;
        return instance;
    }

    static ILogger& logger() {
        return instance().getLogger();
    }

    ILogger& getLogger() {
        return *_logger;
    }

    void installSysloger() {
        _logger = std::make_unique<LoggerSyslog>();
    }

private:
    std::unique_ptr<ILogger> _logger;

    LoggerInstance():
        _logger(std::make_unique<LoggerConsole>()) {
    };
};

}
#endif
