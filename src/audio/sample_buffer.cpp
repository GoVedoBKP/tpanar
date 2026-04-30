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

#include "sample_buffer.h"
#include <algorithm>
#include <cmath>

namespace tpanar_ns
{

    void SampleBuffer::normalize()
    {
        float max_amp = 0.0f;

        for (float v : left)
            max_amp = ::std::max(max_amp, ::std::fabs(v));

        for (float v : right)
            max_amp = ::std::max(max_amp, ::std::fabs(v));

        if (max_amp <= 0.000001f)
            return;

        float gain = 1.0f / max_amp;
        apply_gain(gain);
    }

    void SampleBuffer::crop(size_t start, size_t end)
    {
        if (start >= end)
            return;

        if (end > left.size())
            end = left.size();

        left = ::std::vector<float>(left.begin() + start,
                                  left.begin() + end);

        if (!right.empty())
        {
            right = ::std::vector<float>(right.begin() + start,
                                       right.begin() + end);
        }
    }

    void SampleBuffer::apply_gain(float g)
    {
        for (auto& v : left)
            v *= g;

        for (auto& v : right)
            v *= g;
    }

} // namespace tpanar_ns
