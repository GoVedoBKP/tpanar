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

#include "timestretch.h"
#include <soundtouch/SoundTouch.h>
#include <cstdint>

namespace tpanar_ns
{

    bool TimeStretch::stretch(const ::std::vector<float>& input,
                              ::std::vector<float>& output,
                              float tempo_ratio,
                              uint32_t sample_rate)
    {
        if (input.empty() || tempo_ratio <= 0.0f)
            return false;

        soundtouch::SoundTouch st;
        st.setChannels(1);
        st.setSampleRate(sample_rate > 0 ? sample_rate : 44100);
        st.setTempo(tempo_ratio);

        st.putSamples(input.data(), input.size());

        output.clear();
        ::std::vector<float> temp(4096);

        while (true)
        {
            uint32_t received =
            st.receiveSamples(temp.data(), temp.size());

            if (received == 0)
                break;

            output.insert(output.end(),
                          temp.begin(),
                          temp.begin() + received);
        }

        return true;
    }

} // namespace tpanar_ns
