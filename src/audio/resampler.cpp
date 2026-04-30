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

#include "resampler.h"
#include <samplerate.h>
#include <vector>

namespace tpanar_ns
{

    bool Resampler::process(const ::std::vector<float>& input,
                            ::std::vector<float>& output,
                            double ratio)
    {
        if (input.empty() || ratio <= 0.0)
            return false;

        SRC_DATA data{};
        data.data_in = input.data();
        data.input_frames = input.size();
        data.src_ratio = ratio;

        size_t out_frames = static_cast<size_t>(input.size() * ratio) + 1;
        output.resize(out_frames);

        data.data_out = output.data();
        data.output_frames = out_frames;

        int err = src_simple(&data,
                             SRC_SINC_FASTEST,
                             1);

        if (err != 0)
            return false;

        output.resize(data.output_frames_gen);
        return true;
    }

} // namespace tpanar_ns
