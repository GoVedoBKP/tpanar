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

#include "sample_voice.h"
#include <cmath>

namespace tpanar_ns
{

    SampleVoice::SampleVoice(std::shared_ptr<SampleData> data,
                             double engine_rate)
    : m_sample(data),
    m_engine_rate(engine_rate)
    {
        m_env.set(0.005f, 0.05f, 0.9f, 0.2f);
    }

    void SampleVoice::start(uint8_t note,
                            uint8_t velocity,
                            float freq,
                            size_t offset_samples)
    {
        m_position = (double)offset_samples;

        double base_freq =
        440.0; // assume sample tuned to A4

        m_increment =
        (freq / base_freq) *
        (double(m_sample->sample_rate) /
        m_engine_rate);

        m_volume = velocity / 127.f;
         m_env.note_on();
        m_active = true;
    }

    void SampleVoice::stop()
    {
         m_env.note_off();
    }

    void SampleVoice::panic()
    {
        m_env.reset();
        m_active = false;
    }

    void SampleVoice::set_pitch(float freq)
    {
        double base_freq = 440.0;

        m_increment =
        (freq / base_freq) *
        (double(m_sample->sample_rate) /
        m_engine_rate);
    }

    void SampleVoice::set_volume(float vol)
    {
        m_volume = vol;
    }

    void SampleVoice::set_engine_rate(double sr)
    {
        m_engine_rate = sr;
        if (m_active && m_sample) {
            double base_freq = 440.0;
            double freq = base_freq * m_increment * (m_engine_rate / double(m_sample->sample_rate));
            m_increment = (freq / base_freq) * (double(m_sample->sample_rate) / m_engine_rate);
        }
    }

    bool SampleVoice::active() const
    {
        return m_active;
    }


    void SampleVoice::process(float* out_l,
                              float* out_r,
                              size_t frames)
    {
        if (!m_env.active() || !m_sample)
        {
            m_active = false;
            return;
        }

        const size_t sample_size =
        m_sample->left.size();

        const size_t effective_end =
            (m_end_pos > 0 && m_end_pos < sample_size) ? m_end_pos : (sample_size - 1);

        for (size_t i = 0; i < frames; ++i)
        {
            if (m_position >= (double)effective_end)
            {
                if (m_loop_enabled) {
                    m_position = (double)m_loop_start;
                } else {
                    m_active = false;
                    return;
                }
            }

            size_t idx = size_t(m_position);
            float frac = float(m_position - idx);

            // Linear interpolation
            float l0 = m_sample->left[idx];
            float l1 = m_sample->left[idx + 1];
            float l  = l0 + (l1 - l0) * frac;

            float r = l;
            if (!m_sample->right.empty())
            {
                float r0 = m_sample->right[idx];
                float r1 = m_sample->right[idx + 1];
                r = r0 + (r1 - r0) * frac;
            }

            float env = m_env.process();

            out_l[i] += l * m_volume * env;
            out_r[i] += r * m_volume * env;

            m_position += m_increment;
        }

        if (!m_env.active())
            m_active = false;
    }



} // namespace tpanar_ns
