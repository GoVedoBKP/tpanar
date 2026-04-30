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
#include <atomic>
#include <cstddef>
#include <string>
#include <memory>
#include "../dsp/mastering_filter.h"
#include "../dsp/mastering_styles.h"
#include "../dsp/reference_matcher.h"

namespace tpanar_ns
{

class MasterBus
{
public:
    void reset(float sample_rate = 44100.0f);

    void set_gain(float g);
    float gain() const;

    void set_pan(float p);
    float pan() const;

    void set_mute(bool m);
    bool muted() const;

    void process(float* l,
                 float* r,
                 size_t nframes);

    void set_sample_rate(float sr) {
        m_filter.set_sample_rate(sr);
    }

    float meter_l() const;
    float meter_r() const;

    // Mastering Controls
    tpanar_ns::MasteringFilterDSP& mastering_filter() { return m_filter; }
    const tpanar_ns::MasteringFilterDSP& mastering_filter() const { return m_filter; }
    tpanar_ns::MasteringStylesDSP& mastering_styles() { return m_styles; }
    const tpanar_ns::MasteringStylesDSP& mastering_styles() const { return m_styles; }
    tpanar_ns::ReferenceMatcherDSP& reference_matcher() { return m_reference_matcher; }
    const tpanar_ns::ReferenceMatcherDSP& reference_matcher() const { return m_reference_matcher; }

    ::std::atomic<bool> m_is_recording{false};
    ::std::atomic<bool> m_export_mute{false};
    ::std::atomic<size_t> m_recorded_write_pos{0};
    ::std::vector<float> m_recorded_l;
    ::std::vector<float> m_recorded_r;

    ::std::atomic<float> m_gain{1.f};
    ::std::atomic<float> m_pan{0.f};
    ::std::atomic<float> m_meter_l{0.f};
    ::std::atomic<float> m_meter_r{0.f};
    ::std::atomic<bool> m_muted{false};

    tpanar_ns::MasteringFilterDSP m_filter;
    tpanar_ns::MasteringStylesDSP m_styles;
    tpanar_ns::ReferenceMatcherDSP m_reference_matcher;
};

} // namespace tpanar_ns
