/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "timing.h"
#include <cstddef>

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
        double tick_sec = 2.5 / double(m_bpm);
        return static_cast<size_t>(m_sample_rate * tick_sec);
    }

    size_t Timing::samples_per_row() const
    {
        return samples_per_tick() * m_speed;
    }
    size_t Timing::samples_per_beat() const
    {
        return samples_per_row() * m_lpb;
    }

    size_t Timing::samples_per_bar() const
    {
        return samples_per_beat() * 4;
    }

} // namespace tpanar_ns
