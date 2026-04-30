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

#include "metronome.h"
#include <cmath>

namespace tpanar_ns
{

constexpr float PI_F = 3.14159265358979323846f; // Renamed from M_PI

void Metronome::set_sample_rate(double sr)
{
    m_sample_rate = sr;
}

void Metronome::set_volume(float v)
{
    m_volume = v;
}

void Metronome::reset()
{
    m_beat_counter = 0;
    m_click_remaining = 0;
    m_phase = 0.f;
}

void Metronome::process(float* out_l,
                        float* out_r,
                        size_t nframes,
                        size_t& samples_until_next_beat,
                        size_t samples_per_beat)
{
    const float freq_accent = 1200.f;
    const float freq_normal = 800.f;

    for (size_t i = 0; i < nframes; ++i)
    {
        if (samples_until_next_beat == 0)
        {
            m_click_remaining =
            static_cast<size_t>(0.02 * m_sample_rate); // 20ms

            m_beat_counter++;

            samples_until_next_beat =
            samples_per_beat;
        }

        float sample = 0.f;

        if (m_click_remaining > 0)
        {
            float freq =
            (m_beat_counter % 4 == 1)
            ? freq_accent
            : freq_normal;

            float phase_inc = 2.f * PI_F * freq / (float)m_sample_rate;

            sample = ::std::sin(m_phase) * m_volume;
            m_phase += phase_inc;
            if (m_phase > 2.f * PI_F) m_phase -= 2.f * PI_F;

            m_click_remaining--;
        }

        out_l[i] += sample;
        out_r[i] += sample;

        samples_until_next_beat--;
    }
}

} // namespace tpanar_ns
