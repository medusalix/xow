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

#pragma once

#include <atomic>
#include <memory>
#include <mutex>

/*
 * Lock-free triple buffer implementation
 */
template<typename T>
class Buffer
{
public:
    Buffer() : back(new T), middle(new T), front(new T), queued(false) {}

    void put(const T &data)
    {
        std::lock_guard<std::mutex> lock(put_mutex);

        *back = data;

        // Swap middle buffer with back buffer
        std::shared_ptr<T> previous = std::atomic_exchange(&middle, back);
        std::atomic_store(&back, previous);

        queued = true;
    }

    bool get(T &data)
    {
        std::lock_guard<std::mutex> lock(get_mutex);

        if (!std::atomic_exchange(&queued, false))
        {
            return false;
        }

        // Swap middle buffer with front buffer
        std::shared_ptr<T> previous = std::atomic_exchange(&middle, front);
        std::atomic_store(&front, previous);

        data = *front;

        return true;
    }

private:
  std::mutex put_mutex, get_mutex;
  std::shared_ptr<T> back, middle, front;
  std::atomic<bool> queued;
};
