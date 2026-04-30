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
#include <algorithm>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tpanar_ns {

class VocoderDSP : public tpanar_ns::DSP {
public:
    static constexpr int NUM_BANDS = 16;

    struct Biquad {
        float b0, b1, b2, a1, a2;
        float x1, x2, y1, y2;
        void reset() { x1 = x2 = y1 = y2 = 0; }
        void set_bandpass(float freq, float q, float sr) {
            float w0 = 2.0f * (float)M_PI * freq / sr;
            float alpha = std::sin(w0) / (2.0f * q);
            float cos_w0 = std::cos(w0);
            float a0 = 1.0f + alpha;
            b0 = alpha / a0;
            b1 = 0;
            b2 = -alpha / a0;
            a1 = -2.0f * cos_w0 / a0;
            a2 = (1.0f - alpha) / a0;
        }
        inline float process(float in) {
            float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = in; y2 = y1; y1 = out;
            return out;
        }
    };

    struct Band {
        Biquad modulator_filter;
        Biquad carrier_filter;
        float envelope = 0;
    };

    VocoderDSP() {
        m_current_preset = "Classic Robot";
        for (int i = 0; i < NUM_BANDS; ++i) {
            m_bands[i].modulator_filter.reset();
            m_bands[i].carrier_filter.reset();
        }
        update_bands();
    }

    float attack = 0.01f;
    float release = 0.05f;
    float bandwidth = 0.2f;
    float shift = 0.0f; // -1 to 1
    float carrier_type = 0.0f; // 0: Saw, 1: Noise, 2: External
    float mix = 1.0f;

    std::string name() const override { return "Vocoder"; }
    std::string type_name() const override { return "Vocoder"; }

    void set_sample_rate(float sr) override {
        m_sample_rate = sr;
        update_bands();
    }

    void process(float* l, float* r, size_t nframes) override {
        if (m_bypassed) return;

        float sr = m_sample_rate;
        float att_coeff = std::exp(-1.0f / (attack * sr + 1.0f));
        float rel_coeff = std::exp(-1.0f / (release * sr + 1.0f));

        for (size_t i = 0; i < nframes; ++i) {
            float mod_in = (l[i] + r[i]) * 0.5f;
            float car_in = 0;

            if (carrier_type < 0.5f) { // Saw
                car_in = m_saw_phase * 2.0f - 1.0f;
                m_saw_phase += 110.0f / sr; // Fixed 110Hz for robot
                if (m_saw_phase > 1.0f) m_saw_phase -= 1.0f;
            } else if (carrier_type < 1.5f) { // Noise
                car_in = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
            } else { // External (L=Mod, R=Car)
                mod_in = l[i];
                car_in = r[i];
            }

            float vocoded = 0;
            for (int b = 0; b < NUM_BANDS; ++b) {
                float mod_band = m_bands[b].modulator_filter.process(mod_in);
                float env = std::abs(mod_band);
                
                if (env > m_bands[b].envelope)
                    m_bands[b].envelope = env + att_coeff * (m_bands[b].envelope - env);
                else
                    m_bands[b].envelope = env + rel_coeff * (m_bands[b].envelope - env);

                float car_band = m_bands[b].carrier_filter.process(car_in);
                vocoded += car_band * m_bands[b].envelope;
            }

            vocoded *= 10.0f; // Scale up

            float out_l = l[i] * (1.0f - mix) + vocoded * mix;
            float out_r = r[i] * (1.0f - mix) + vocoded * mix;
            
            l[i] = out_l;
            r[i] = out_r;
        }
    }

    void update_bands() {
        float sr = m_sample_rate;
        float min_f = 80.0f;
        float max_f = 10000.0f;
        float q = 1.0f / (bandwidth + 0.05f);

        for (int i = 0; i < NUM_BANDS; ++i) {
            float freq = min_f * std::pow(max_f / min_f, (float)i / (float)(NUM_BANDS - 1));
            m_bands[i].modulator_filter.set_bandpass(freq, q, sr);
            
            float car_freq = freq * std::pow(2.0f, shift);
            if (car_freq > 18000.0f) car_freq = 18000.0f;
            if (car_freq < 20.0f) car_freq = 20.0f;
            m_bands[i].carrier_filter.set_bandpass(car_freq, q, sr);
        }
    }

    std::string get_state() const override {
        nlohmann::json j;
        j["attack"] = attack;
        j["release"] = release;
        j["bandwidth"] = bandwidth;
        j["shift"] = shift;
        j["carrier_type"] = carrier_type;
        j["mix"] = mix;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        try {
            auto j = nlohmann::json::parse(state);
            if (j.contains("attack")) attack = j["attack"];
            if (j.contains("release")) release = j["release"];
            if (j.contains("bandwidth")) bandwidth = j["bandwidth"];
            if (j.contains("shift")) shift = j["shift"];
            if (j.contains("carrier_type")) carrier_type = j["carrier_type"];
            if (j.contains("mix")) mix = j["mix"];
            if (j.contains("bypassed")) m_bypassed = j["bypassed"];
            update_bands();
        } catch(...) {}
    }

    std::vector<std::string> get_presets() override {
        return {"Classic Robot", "Whisper", "Fast Talker", "Smooth Choir", "External Mod"};
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "Classic Robot") { attack = 0.01f; release = 0.05f; bandwidth = 0.2f; shift = 0.0f; carrier_type = 0.0f; mix = 1.0f; }
        else if (name == "Whisper") { attack = 0.02f; release = 0.1f; bandwidth = 0.3f; shift = 0.0f; carrier_type = 1.0f; mix = 1.0f; }
        else if (name == "Fast Talker") { attack = 0.005f; release = 0.02f; bandwidth = 0.15f; shift = 0.0f; carrier_type = 0.0f; mix = 1.0f; }
        else if (name == "Smooth Choir") { attack = 0.05f; release = 0.3f; bandwidth = 0.4f; shift = 0.0f; carrier_type = 0.0f; mix = 1.0f; }
        else if (name == "External Mod") { attack = 0.01f; release = 0.05f; bandwidth = 0.2f; shift = 0.0f; carrier_type = 2.0f; mix = 1.0f; }
        update_bands();
    }

private:
    Band m_bands[NUM_BANDS];
    float m_saw_phase = 0;
    float m_sample_rate = 44100.0f;
};

} // namespace tpanar_ns
