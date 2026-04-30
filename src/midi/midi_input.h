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
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>

namespace tpanar_ns
{

struct MidiMessage
{
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

class MidiInput
{
public:
    using Callback =
    ::std::function<void(const MidiMessage&)>;

    MidiInput();
    ~MidiInput();

    void start();
    void stop();

    void set_callback(Callback cb);

private:
    void run();

    ::std::thread m_thread;
    ::std::atomic<bool> m_running{false};
    Callback m_callback;
};

} // namespace tpanar_ns
