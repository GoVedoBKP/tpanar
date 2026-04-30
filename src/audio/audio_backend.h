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

#include <cstdint>

namespace tpanar_ns
{

enum class AudioBackendType
{
    Jack,
    Oss,
    Null
};

inline const char *audio_backend_type_id(AudioBackendType type)
{
    switch (type)
    {
        case AudioBackendType::Jack: return "jack";
        case AudioBackendType::Oss:  return "oss";
        case AudioBackendType::Null: return "null";
    }
    return "null";
}

inline const char *audio_backend_type_label(AudioBackendType type)
{
    switch (type)
    {
        case AudioBackendType::Jack: return "JACK";
        case AudioBackendType::Oss:  return "OSS";
        case AudioBackendType::Null: return "Null";
    }
    return "Null";
}

class AudioBackend
{
public:
    virtual ~AudioBackend() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool is_active() const = 0;

    virtual uint32_t sample_rate() const = 0;
    virtual uint32_t buffer_size() const = 0;
    virtual AudioBackendType type() const = 0;
};

} // namespace tpanar_ns
