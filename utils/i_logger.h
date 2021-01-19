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
#ifndef XOW_I_LOGGER_H_
#define XOW_I_LOGGER_H_

#include <ostream>

namespace Log
{

enum class Level {
  LOGLEVEL_DEBUG,
  LOGLEVEL_INFO,
  LOGLEVEL_ERROR
};

class ILogger {
public:
    ILogger() = default;
    ~ILogger() = default;

    virtual void init() = 0;
    virtual void sinkLog(Level level, const std::string& message) = 0;
};

inline std::ostream& operator<<(std::ostream& os, const Level& level) {
    switch(level) {
        case Level::LOGLEVEL_DEBUG:
          os << "DEBUG";
          break;
        case Level::LOGLEVEL_INFO:
          os << "INFO";
          break;
        case Level::LOGLEVEL_ERROR:
          os << "ERROR";
          break;
    }

    return os;
}

}
#endif
