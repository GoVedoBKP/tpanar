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

#include "midi_input.h"
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <utility>

namespace tpanar_ns
{

MidiInput::MidiInput() {}
MidiInput::~MidiInput() { stop(); }

void MidiInput::start()
{
    m_running = true;
    m_thread = ::std::thread(&MidiInput::run, this);
}

void MidiInput::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

void MidiInput::set_callback(Callback cb)
{
    m_callback = ::std::move(cb);
}

void MidiInput::run()
{
    while (m_running)
    {
        ::std::this_thread::sleep_for(
            ::std::chrono::milliseconds(1));

        // platform MIDI read here later
    }
}

} // namespace tpanar_ns
