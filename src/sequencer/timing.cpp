/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "timing.h"
#include <cstddef>
#include <cmath>

namespace tpanar_ns
{

    void Timing::set_bpm(int bpm)
    {
        if (bpm > 10 && bpm < 400)
            m_bpm = bpm;
    }

    void Timing::set_lpb(uint32_t lpb)
    {
        if (lpb > 0 && lpb < 128)
            m_lpb = lpb;
    }

    void Timing::set_speed(int speed)
    {
        if (speed > 0 && speed < 32)
            m_speed = speed;
    }

    size_t Timing::samples_per_tick() const
    {
        // 1 beat = 60 / BPM seconds.
        // Tpanar assumes 24 ticks per beat (Speed 6 * LPB 4).
        // So 1 tick = 60 / (BPM * 24) = 2.5 / BPM seconds.
        double tick_sec = 2.5 / double(m_bpm);
        return static_cast<size_t>(std::lround(m_sample_rate * tick_sec));
    }

    size_t Timing::samples_per_row() const
    {
        // Calculate directly to avoid accumulating tick-level rounding errors.
        double row_sec = (2.5 * double(m_speed)) / double(m_bpm);
        return static_cast<size_t>(std::lround(m_sample_rate * row_sec));
    }

    size_t Timing::samples_for_tick(size_t tick_index_in_row) const
    {
        // Distribute samples_per_row() across speed ticks so the row boundary is
        // sample-accurate.  The first (spr % speed) ticks get one extra sample.
        size_t spr = samples_per_row();
        size_t q   = spr / static_cast<size_t>(m_speed);
        size_t r   = spr % static_cast<size_t>(m_speed);
        return q + (tick_index_in_row < r ? 1u : 0u);
    }

    size_t Timing::samples_per_beat() const
    {
        // Calculate directly. 
        // Note: In Tpanar, LPB is 'rows per beat marker', and row duration is fixed by 'speed'.
        double beat_sec = (2.5 * double(m_speed) * double(m_lpb)) / double(m_bpm);
        return static_cast<size_t>(std::lround(m_sample_rate * beat_sec));
    }

    size_t Timing::samples_per_bar() const
    {
        // Assume 4 beats per bar.
        return samples_per_beat() * 4;
    }

} // namespace tpanar_ns
