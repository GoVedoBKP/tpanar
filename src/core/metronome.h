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
#include <cstddef>
#include <cstdint>

namespace tpanar_ns
{

class Metronome
{
public:
    void set_sample_rate(double sr);
    void set_volume(float v);
    void reset();

    void process(float* out_l,
                 float* out_r,
                 size_t nframes,
                 size_t& samples_until_next_beat,
                 size_t samples_per_beat);

private:
    double m_sample_rate{44100.0};
    float m_volume{0.4f};

    size_t m_beat_counter{0};
    size_t m_click_remaining{0};

    float m_phase{0.f};
};

} // namespace tpanar_ns
