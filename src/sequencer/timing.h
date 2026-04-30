/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once

#include <cstdint>
#include <cstddef> 

namespace tpanar_ns
{

class Timing
{
public:
    Timing(uint32_t sr = 44100) : m_sample_rate((double)sr) {}

    void set_sample_rate(uint32_t sr) { m_sample_rate = (double)sr; }
    void set_bpm(int bpm); 
    double tempo() const { return (double)m_bpm; } 
    void set_lpb(uint32_t lpb);
    uint32_t lpb() const { return m_lpb; } 
    void set_speed(int speed);

    int bpm() const { return m_bpm; }
    int speed() const { return m_speed; }

    size_t samples_per_tick() const;
    size_t samples_per_row() const;

    size_t samples_per_beat() const;
    size_t samples_per_bar() const;


private:
    double m_sample_rate = 44100.0;
    int m_bpm = 125;     
    int m_speed = 6;     
    uint32_t m_lpb = 4;
};

} // namespace tpanar_ns
