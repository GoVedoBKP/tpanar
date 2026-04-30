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

#include "transport.h"
#include "engine.h"

namespace tpanar_ns
{

Transport::Transport(Engine& engine)
    : m_engine(engine),
      m_tempo(120.0), // Default tempo
      m_transport(TransportState::Stopped),
      m_loop_pattern(false)
{
}

bool Transport::is_playing() const
{
    return m_transport.load(::std::memory_order_relaxed) == TransportState::Playing;
}

void Transport::set_tempo(double bpm)
{
    m_tempo.store(bpm, ::std::memory_order_relaxed);
}

double Transport::tempo() const
{
    return m_tempo.load(::std::memory_order_relaxed);
}

void Transport::play()
{
    m_transport.store(TransportState::Playing, ::std::memory_order_relaxed);
}

void Transport::stop()
{
    m_transport.store(TransportState::Stopped, ::std::memory_order_relaxed);
}

void Transport::toggle_play()
{
    if (m_transport.load(::std::memory_order_relaxed) == TransportState::Playing)
    {
        stop();
    }
    else
    {
        play();
    }
}

void Transport::set_loop(bool enable)
{
    m_loop_pattern.store(enable, ::std::memory_order_relaxed);
}

TransportState Transport::state() const
{
    return m_transport.load(::std::memory_order_relaxed);
}

void Transport::set_play_position(size_t pattern,
                           size_t row)
{
}

} // namespace tpanar_ns
