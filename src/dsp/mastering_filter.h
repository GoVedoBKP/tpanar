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
#include "dsp.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <vector>

namespace tpanar_ns
{

class MasteringFilterDSP : public tpanar_ns::DSP
{
public:
    struct Biquad {
        float b0, b1, b2, a1, a2;
        float x1_l, x2_l, y1_l, y2_l;
        float x1_r, x2_r, y1_r, y2_r;

        void reset() {
            x1_l = x2_l = y1_l = y2_l = 0;
            x1_r = x2_r = y1_r = y2_r = 0;
        }

        void set_hpf(float freq, float q, float sample_rate) {
            float w0 = 2.0f * M_PI * freq / sample_rate;
            float cos_w0 = std::cos(w0);
            float alpha = std::sin(w0) / (2.0f * q);

            float b0_raw = (1.0f + cos_w0) / 2.0f;
            float b1_raw = -(1.0f + cos_w0);
            float b2_raw = (1.0f + cos_w0) / 2.0f;
            float a0_raw = 1.0f + alpha;
            float a1_raw = -2.0f * cos_w0;
            float a2_raw = 1.0f - alpha;

            b0 = b0_raw / a0_raw;
            b1 = b1_raw / a0_raw;
            b2 = b2_raw / a0_raw;
            a1 = a1_raw / a0_raw;
            a2 = a2_raw / a0_raw;
        }

        void set_lpf(float freq, float q, float sample_rate) {
            float w0 = 2.0f * M_PI * freq / sample_rate;
            float cos_w0 = std::cos(w0);
            float alpha = std::sin(w0) / (2.0f * q);

            float b0_raw = (1.0f - cos_w0) / 2.0f;
            float b1_raw = 1.0f - cos_w0;
            float b2_raw = (1.0f - cos_w0) / 2.0f;
            float a0_raw = 1.0f + alpha;
            float a1_raw = -2.0f * cos_w0;
            float a2_raw = 1.0f - alpha;

            b0 = b0_raw / a0_raw;
            b1 = b1_raw / a0_raw;
            b2 = b2_raw / a0_raw;
            a1 = a1_raw / a0_raw;
            a2 = a2_raw / a0_raw;
        }

        inline float process_l(float in) {
            float out = b0 * in + b1 * x1_l + b2 * x2_l - a1 * y1_l - a2 * y2_l;
            x2_l = x1_l; x1_l = in; y2_l = y1_l; y1_l = out;
            return out;
        }

        inline float process_r(float in) {
            float out = b0 * in + b1 * x1_r + b2 * x2_r - a1 * y1_r - a2 * y2_r;
            x2_r = x1_r; x1_r = in; y2_r = y1_r; y1_r = out;
            return out;
        }
    };

    MasteringFilterDSP() {
        m_current_preset = "Default";
        hpf_freq = 20.0f;
        lpf_freq = 20000.0f;
        hpf_enabled = false;
        lpf_enabled = false;
        update_filters();
    }

    std::string name() const override { return "Mastering Filter"; }
    std::string type_name() const override { return "MasteringFilter"; }

    void set_sample_rate(float sr) override {
        m_sample_rate = sr;
        update_filters();
    }

    void process(float* l, float* r, size_t nframes) override {
        if (m_bypassed) return;
        for (size_t i = 0; i < nframes; ++i) {
            float sl = l[i];
            float sr = r[i];
            if (hpf_enabled) {
                sl = m_hpf.process_l(sl);
                sr = m_hpf.process_r(sr);
            }
            if (lpf_enabled) {
                sl = m_lpf.process_l(sl);
                sr = m_lpf.process_r(sr);
            }
            l[i] = sl;
            r[i] = sr;
        }
    }

    void update_filters() {
        m_hpf.set_hpf(hpf_freq, 0.707f, m_sample_rate);
        m_lpf.set_lpf(lpf_freq, 0.707f, m_sample_rate);
    }

    float hpf_freq = 20.0f;
    float lpf_freq = 20000.0f;
    bool hpf_enabled = true;
    bool lpf_enabled = false;

    std::string get_state() const override {
        nlohmann::json j;
        j["hpf_freq"] = hpf_freq;
        j["lpf_freq"] = lpf_freq;
        j["hpf_enabled"] = hpf_enabled;
        j["lpf_enabled"] = lpf_enabled;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        try {
            auto j = nlohmann::json::parse(state);
            if (j.contains("hpf_freq")) hpf_freq = j["hpf_freq"];
            if (j.contains("lpf_freq")) lpf_freq = j["lpf_freq"];
            if (j.contains("hpf_enabled")) hpf_enabled = j["hpf_enabled"];
            if (j.contains("lpf_enabled")) lpf_enabled = j["lpf_enabled"];
            if (j.contains("bypassed")) m_bypassed = j["bypassed"];
            update_filters();
        } catch(...) {}
    }

private:
    Biquad m_hpf;
    Biquad m_lpf;
    float m_sample_rate = 44100.0f;
};

} // namespace tpanar_ns
