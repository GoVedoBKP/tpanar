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
#include <cstddef> 

namespace tpanar_ns
{
    class Engine;

    enum class TransportState
    {
        Stopped,
        Playing
    };

class Transport
{
public:
    Transport(Engine& engine);

    bool is_playing() const;

    void set_tempo(double bpm);
    double tempo() const;
    void play();
    void stop();
    void toggle_play();
    void set_loop(bool enable);

    TransportState state() const; 

    void set_play_position(size_t pattern,
                           size_t row);

    ::std::atomic<bool> m_loop_pattern{false};

private:
    Engine& m_engine;
    ::std::atomic<double> m_tempo;
    ::std::atomic<TransportState> m_transport{TransportState::Stopped};
};

} // namespace tpanar_ns
