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
#include <array>

namespace tpanar_ns
{

class GraphicalEQDSP : public tpanar_ns::DSP
{
public:
    static constexpr size_t NUM_BANDS = 12;
    
    struct Biquad {
        float b0, b1, b2, a1, a2;
        float x1_l, x2_l, y1_l, y2_l;
        float x1_r, x2_r, y1_r, y2_r;

        void reset() {
            x1_l = x2_l = y1_l = y2_l = 0;
            x1_r = x2_r = y1_r = y2_r = 0;
        }

        void set_peaking(float freq, float q, float gain_db, float sample_rate) {
            float a = std::pow(10.0f, gain_db / 40.0f);
            float w0 = 2.0f * M_PI * freq / sample_rate;
            float alpha = std::sin(w0) / (2.0f * q);
            float cos_w0 = std::cos(w0);

            float b0_raw = 1.0f + alpha * a;
            float b1_raw = -2.0f * cos_w0;
            float b2_raw = 1.0f - alpha * a;
            float a0_raw = 1.0f + alpha / a;
            float a1_raw = -2.0f * cos_w0;
            float a2_raw = 1.0f - alpha / a;

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

    GraphicalEQDSP() {
        m_current_preset = "Flat";
        m_frequencies = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000, 20000};
        m_gains.fill(0.0f);
        m_filters.resize(NUM_BANDS);
        update_filters();
    }

    std::string name() const override { return "Graphical EQ"; }
    std::string type_name() const override { return "GraphicalEQ"; }

    void set_sample_rate(float sr) override {
        m_sample_rate = sr;
        update_filters();
    }

    void process(float* l, float* r, size_t nframes) override
    {
        if (m_bypassed) return;
        for (size_t i = 0; i < nframes; ++i)
        {
            float out_l = l[i];
            float out_r = r[i];
            for (size_t j = 0; j < NUM_BANDS; ++j) {
                out_l = m_filters[j].process_l(out_l);
                out_r = m_filters[j].process_r(out_r);
            }
            l[i] = out_l;
            r[i] = out_r;
        }
    }

    void set_band_gain(size_t band, float gain_db) {
        if (band < NUM_BANDS) {
            m_gains[band] = gain_db;
            m_filters[band].set_peaking(m_frequencies[band], 1.414f, m_gains[band], m_sample_rate);
        }
    }

    float get_band_gain(size_t band) const {
        return band < NUM_BANDS ? m_gains[band] : 0.0f;
    }

    float get_band_freq(size_t band) const {
        return band < NUM_BANDS ? m_frequencies[band] : 0.0f;
    }

    std::string get_state() const override {
        nlohmann::json j;
        nlohmann::json g = nlohmann::json::array();
        for (float v : m_gains) g.push_back(v);
        j["gains"] = g;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        try {
            auto j = nlohmann::json::parse(state);
            if (j.contains("gains") && j["gains"].is_array()) {
                for (size_t i = 0; i < NUM_BANDS && i < j["gains"].size(); ++i) {
                    m_gains[i] = j["gains"][i];
                }
                update_filters();
            }
            if (j.contains("bypassed")) m_bypassed = j["bypassed"];
        } catch(...) {}
    }

    std::vector<std::string> get_presets() override {
        return {"Flat", "Bass Boost", "Treble Boost", "Loudness", "Mid Scoop",
                "Kick Punch", "Snare Crack", "Overhead Brightness", "Room Warmth", "Hi-Hat Air"};
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "Flat") { m_gains.fill(0.0f); }
        else if (name == "Bass Boost")   { m_gains.fill(0.0f); m_gains[0]=6; m_gains[1]=5; m_gains[2]=3; }
        else if (name == "Treble Boost") { m_gains.fill(0.0f); m_gains[8]=3; m_gains[9]=5; m_gains[10]=6; m_gains[11]=6; }
        else if (name == "Loudness")     { m_gains.fill(0.0f); m_gains[0]=6; m_gains[1]=4; m_gains[10]=4; m_gains[11]=6; }
        else if (name == "Mid Scoop")    { m_gains.fill(0.0f); m_gains[4]=-4; m_gains[5]=-6; m_gains[6]=-4; }
        // Kick: weight at 64 Hz, punch at 125 Hz, mud cut 250-500 Hz, click at 4-8 kHz
        else if (name == "Kick Punch") {
            m_gains.fill(0.0f);
            m_gains[1]=6; m_gains[2]=4; m_gains[3]=-3; m_gains[4]=-4; m_gains[7]=3; m_gains[8]=2;
        }
        // Snare: body at 250 Hz, crack at 2 kHz, snap at 8 kHz, cut mud
        else if (name == "Snare Crack") {
            m_gains.fill(0.0f);
            m_gains[2]=-2; m_gains[3]=4; m_gains[4]=-2; m_gains[6]=5; m_gains[8]=3; m_gains[9]=2;
        }
        // Overheads: cut unnecessary lows, lift air above 12 kHz
        else if (name == "Overhead Brightness") {
            m_gains.fill(0.0f);
            m_gains[0]=-6; m_gains[1]=-4; m_gains[2]=-2; m_gains[9]=3; m_gains[10]=4; m_gains[11]=3;
        }
        // Room mics: add warmth in low-mids, tame harsh high end
        else if (name == "Room Warmth") {
            m_gains.fill(0.0f);
            m_gains[1]=2; m_gains[2]=3; m_gains[9]=-2; m_gains[10]=-4; m_gains[11]=-5;
        }
        // Hi-hat / cymbal: low-cut, bring out sizzle at 8-16 kHz
        else if (name == "Hi-Hat Air") {
            m_gains.fill(0.0f);
            m_gains[0]=-6; m_gains[1]=-6; m_gains[2]=-4; m_gains[3]=-2;
            m_gains[8]=3; m_gains[9]=4; m_gains[10]=3;
        }
        update_filters();
    }

private:
    void update_filters() {
        for (size_t i = 0; i < NUM_BANDS; ++i) {
            m_filters[i].set_peaking(m_frequencies[i], 1.414f, m_gains[i], m_sample_rate);
        }
    }

    float m_sample_rate = 44100.0f;
    std::vector<float> m_frequencies;
    std::array<float, NUM_BANDS> m_gains;
    std::vector<Biquad> m_filters;
};

} // namespace tpanar_ns
