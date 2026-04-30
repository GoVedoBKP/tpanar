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
#include <cstddef>

namespace tpanar_ns
{

template<typename T, size_t Size>
class MidiQueue
{
public:
    bool push(const T& v)
    {
        auto next =
        (m_write + 1) % Size;

        if (next == m_read)
            return false;

        m_buffer[m_write] = v;
        m_write = next;
        return true;
    }

    bool pop(T& out)
    {
        if (m_read == m_write)
            return false;

        out = m_buffer[m_read];
        m_read =
        (m_read + 1) % Size;

        return true;
    }

private:
    ::std::array<T, Size> m_buffer{};
    ::std::atomic<size_t> m_write{0};
    ::std::atomic<size_t> m_read{0};
};

} // namespace tpanar_ns
