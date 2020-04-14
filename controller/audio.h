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

#include <cstdint>
#include <functional>
#include <atomic>
#include <string>
#include <stdexcept>

class Bytes;
struct pa_simple;

/*
 * Bidirectional audio stream
 * Provides async input and sync output functionality
 */
class AudioStream
{
public:
    using SamplesRead = std::function<void(const Bytes &samples)>;

    AudioStream(SamplesRead samplesRead);
    virtual ~AudioStream();

    void start(
        uint32_t sampleRate,
        size_t sampleCount,
        std::string name
    );
    void write(const Bytes &samples);
    void stop();

private:
    enum State
    {
        STATE_STOPPED,
        STATE_RUNNING,
        STATE_STOPPING,
    };

    void read(size_t sampleCount);

    pa_simple *source, *sink;
    std::atomic<State> state;

    SamplesRead samplesRead;
};

class AudioException : public std::runtime_error
{
public:
    AudioException(std::string message);
    AudioException(std::string message, int error);
};
