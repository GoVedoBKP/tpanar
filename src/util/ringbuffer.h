/*
 * TPanar - Digital Audio Workstation
 * Copyright (C) 2025  Miroslav Shaltev
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <atomic>
#include <array>

namespace tpanar_ns
{

template<typename T, size_t Size>
class RingBuffer
{
public:
    bool push(const T &item)
    {
        auto head = m_head.load(::std::memory_order_relaxed);
        auto next = (head + 1) % Size;

        if (next == m_tail.load(::std::memory_order_acquire))
            return false; // full

        m_buffer[head] = item;
        m_head.store(next, ::std::memory_order_release);
        return true;
    }

    bool pop(T &item)
    {
        auto tail = m_tail.load(::std::memory_order_relaxed);

        if (tail == m_head.load(::std::memory_order_acquire))
            return false; // empty

        item = m_buffer[tail];
        m_tail.store((tail + 1) % Size, ::std::memory_order_release);
        return true;
    }

private:
    ::std::array<T, Size> m_buffer;
    ::std::atomic<size_t> m_head{0};
    ::std::atomic<size_t> m_tail{0};
};

} // namespace tpanar_ns
