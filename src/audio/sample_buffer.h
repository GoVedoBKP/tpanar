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
#include <vector>
#include <cstdint>

namespace tpanar_ns
{

    class SampleBuffer
    {
    public:
        ::std::vector<float> left;
        ::std::vector<float> right;
        uint32_t sample_rate = 44100;

        void normalize();
        void crop(size_t start, size_t end);
        void apply_gain(float g);
    };

} // namespace tpanar_ns
