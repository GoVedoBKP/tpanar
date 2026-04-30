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

#include <string>
#include <vector>
#include <cstdint> // Added for uint32_t

namespace tpanar_ns
{

    class AudioFile
    {
    public:
        static bool load_audio(const ::std::string& path,
                               ::std::vector<float>& left,
                               ::std::vector<float>& right,
                               uint32_t& sample_rate);

        static bool save_wav(const ::std::string& path,
                             const ::std::vector<float>& left,
                             const ::std::vector<float>& right,
                             uint32_t sample_rate);
    };

} // namespace tpanar_ns
