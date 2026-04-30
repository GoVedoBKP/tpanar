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
#include <string>

namespace tpanar_ns
{

class CabinetDSP : public tpanar_ns::DSP
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

        void set_lpf(float freq, float q, float sr) {
            float w0 = 2.0f * M_PI * freq / sr;
            float cos_w0 = std::cos(w0);
            float alpha = std::sin(w0) / (2.0f * q);
            float b0_raw = (1.0f - cos_w0) / 2.0f;
            float b1_raw = 1.0f - cos_w0;
            float b2_raw = (1.0f - cos_w0) / 2.0f;
            float a0_raw = 1.0f + alpha;
            float a1_raw = -2.0f * cos_w0;
            float a2_raw = 1.0f - alpha;
            b0 = b0_raw / a0_raw; b1 = b1_raw / a0_raw; b2 = b2_raw / a0_raw;
            a1 = a1_raw / a0_raw; a2 = a2_raw / a0_raw;
        }

        void set_hpf(float freq, float q, float sr) {
            float w0 = 2.0f * M_PI * freq / sr;
            float cos_w0 = std::cos(w0);
            float alpha = std::sin(w0) / (2.0f * q);
            float b0_raw = (1.0f + cos_w0) / 2.0f;
            float b1_raw = -(1.0f + cos_w0);
            float b2_raw = (1.0f + cos_w0) / 2.0f;
            float a0_raw = 1.0f + alpha;
            float a1_raw = -2.0f * cos_w0;
            float a2_raw = 1.0f - alpha;
            b0 = b0_raw / a0_raw; b1 = b1_raw / a0_raw; b2 = b2_raw / a0_raw;
            a1 = a1_raw / a0_raw; a2 = a2_raw / a0_raw;
        }

        void set_peaking(float freq, float q, float gain_db, float sr) {
            float a = std::pow(10.0f, gain_db / 40.0f);
            float w0 = 2.0f * M_PI * freq / sr;
            float alpha = std::sin(w0) / (2.0f * q);
            float cos_w0 = std::cos(w0);
            float b0_raw = 1.0f + alpha * a;
            float b1_raw = -2.0f * cos_w0;
            float b2_raw = 1.0f - alpha * a;
            float a0_raw = 1.0f + alpha / a;
            float a1_raw = -2.0f * cos_w0;
            float a2_raw = 1.0f - alpha / a;
            b0 = b0_raw / a0_raw; b1 = b1_raw / a0_raw; b2 = b2_raw / a0_raw;
            a1 = a1_raw / a0_raw; a2 = a2_raw / a0_raw;
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

    CabinetDSP() {
        m_current_preset = "4 x 12\"";
        m_filters.resize(3);
        update_filters();
    }

    float low_cut = 80.0f;
    float high_cut = 5000.0f;
    float peak_freq = 2000.0f;
    float peak_gain = 3.0f;

    std::string name() const override { return "Cabinet"; }
    std::string type_name() const override { return "Cabinet"; }

    void set_sample_rate(float sr) override {
        m_sample_rate = sr;
        update_filters();
    }

    void process(float* l, float* r, size_t nframes) override
    {
        if (m_bypassed) return;
        update_filters(); // Update in real-time or on parameter change
        for (size_t i = 0; i < nframes; ++i)
        {
            float out_l = l[i];
            float out_r = r[i];
            for (auto& f : m_filters) {
                out_l = f.process_l(out_l);
                out_r = f.process_r(out_r);
            }
            l[i] = out_l;
            r[i] = out_r;
        }
    }

    std::string get_state() const override {
        nlohmann::json j;
        j["low_cut"] = low_cut;
        j["high_cut"] = high_cut;
        j["peak_freq"] = peak_freq;
        j["peak_gain"] = peak_gain;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        try {
            auto j = nlohmann::json::parse(state);
            if (j.contains("low_cut")) low_cut = j["low_cut"];
            if (j.contains("high_cut")) high_cut = j["high_cut"];
            if (j.contains("peak_freq")) peak_freq = j["peak_freq"];
            if (j.contains("peak_gain")) peak_gain = j["peak_gain"];
            if (j.contains("bypassed")) m_bypassed = j["bypassed"];
            update_filters();
        } catch(...) {}
    }

    std::vector<std::string> get_presets() override {
        return { "1 x 12\"", "2 x 12\"", "4 x 12\"", "8 x 12\"", "4 x 10\"", "1 x 15\"", "2 x 15\"", "1 x 15\" + 4 x 10\"" };
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "1 x 12\"") { low_cut = 100; high_cut = 5000; peak_freq = 2500; peak_gain = 3; }
        else if (name == "2 x 12\"") { low_cut = 90; high_cut = 6000; peak_freq = 3000; peak_gain = 2; }
        else if (name == "4 x 12\"") { low_cut = 80; high_cut = 5000; peak_freq = 2000; peak_gain = 4; }
        else if (name == "8 x 12\"") { low_cut = 70; high_cut = 4500; peak_freq = 1500; peak_gain = 5; }
        else if (name == "4 x 10\"") { low_cut = 120; high_cut = 7000; peak_freq = 3500; peak_gain = 3; }
        else if (name == "1 x 15\"") { low_cut = 50; high_cut = 3500; peak_freq = 1000; peak_gain = 2; }
        else if (name == "2 x 15\"") { low_cut = 40; high_cut = 3000; peak_freq = 800; peak_gain = 3; }
        else if (name == "1 x 15\" + 4 x 10\"") { low_cut = 45; high_cut = 6500; peak_freq = 3000; peak_gain = 2; }
        update_filters();
    }

private:
    void update_filters() {
        m_filters[0].set_hpf(low_cut, 0.707f, m_sample_rate);
        m_filters[1].set_lpf(high_cut, 0.707f, m_sample_rate);
        m_filters[2].set_peaking(peak_freq, 0.707f, peak_gain, m_sample_rate);
    }
    std::vector<Biquad> m_filters;
    float m_sample_rate = 44100.0f;
};

} // namespace tpanar_ns
