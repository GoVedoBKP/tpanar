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

#include "audio_backend.h"
#include <atomic>

namespace tpanar_ns
{

class NullBackend : public AudioBackend
{
public:
    NullBackend() : m_active(false) {}
    virtual ~NullBackend() = default;

    bool start() override { m_active = true; return true; }
    void stop() override { m_active = false; }
    bool is_active() const override { return m_active; }

    uint32_t sample_rate() const override { return 44100; }
    uint32_t buffer_size() const override { return 512; }
    AudioBackendType type() const override { return AudioBackendType::Null; }

private:
    std::atomic<bool> m_active;
};

} // namespace tpanar_ns
