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

#include "masterbus.h"
#include <cmath>

namespace tpanar_ns
{

void MasterBus::reset(float sample_rate)
{
    m_gain.store(1.0f);
    m_pan.store(0.0f);
    m_meter_l.store(0.0f);
    m_meter_r.store(0.0f);
    m_muted.store(false);
    m_is_recording.store(false, std::memory_order_relaxed);
    m_export_mute.store(false, std::memory_order_relaxed);
    m_recorded_write_pos.store(0, std::memory_order_relaxed);
    m_recorded_l.clear();
    m_recorded_r.clear();

    m_filter = MasteringFilterDSP();
    m_styles = MasteringStylesDSP();
    m_reference_matcher = ReferenceMatcherDSP();
    m_filter.set_sample_rate(sample_rate);
}

void MasterBus::set_gain(float g)
{
    m_gain.store(g);
}

float MasterBus::gain() const
{
    return m_gain.load();
}

void MasterBus::set_pan(float p)
{
    m_pan.store(p);
}

float MasterBus::pan() const
{
    return m_pan.load();
}

void MasterBus::set_mute(bool m)
{
    m_muted.store(m);
}

bool MasterBus::muted() const
{
    return m_muted.load();
}

void MasterBus::process(float* l,
                        float* r,
                        size_t nframes)
{
    m_filter.process(l, r, nframes);
    m_styles.process(l, r, nframes);
    m_reference_matcher.process(l, r, nframes);

    float vol = m_muted.load() ? 0.f : m_gain.load();
    float pan = m_pan.load();
    float left_gain  = vol * (pan <= 0 ? 1.0f : 1.0f - pan);
    float right_gain = vol * (pan >= 0 ? 1.0f : 1.0f + pan);

    float peak_l = 0.f;
    float peak_r = 0.f;

    for (size_t i = 0; i < nframes; ++i)
    {
        float sl = l[i] * left_gain;
        float sr = r[i] * right_gain;

        l[i] = sl;
        r[i] = sr;

        if (m_is_recording.load(std::memory_order_relaxed)) {
            size_t wp = m_recorded_write_pos.load(std::memory_order_relaxed);
            if (wp < m_recorded_l.size()) {
                m_recorded_l[wp] = sl;
                m_recorded_r[wp] = sr;
                m_recorded_write_pos.fetch_add(1, std::memory_order_release);
            }
        }

        float pl = ::std::fabs(sl);
        if (pl > peak_l) peak_l = pl;

        float pr = ::std::fabs(sr);
        if (pr > peak_r) peak_r = pr;
    }

    if (m_export_mute.load()) {
        for (size_t i = 0; i < nframes; ++i) {
            l[i] = 0.f; r[i] = 0.f;
        }
    }

    float prev_l = m_meter_l.load();
    float smoothed_l = 0.9f * prev_l + 0.1f * peak_l;
    m_meter_l.store(smoothed_l);

    float prev_r = m_meter_r.load();
    float smoothed_r = 0.9f * prev_r + 0.1f * peak_r;
    m_meter_r.store(smoothed_r);
}

float MasterBus::meter_l() const
{
    return m_meter_l.load();
}

float MasterBus::meter_r() const
{
    return m_meter_r.load();
}

} // namespace tpanar_ns
