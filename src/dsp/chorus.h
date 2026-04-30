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

class ChorusDSP : public tpanar_ns::DSP
{
public:
    ChorusDSP() {
        m_current_preset = "Subtle Width";
        m_buf_l.resize(4096, 0.0f);
        m_buf_r.resize(4096, 0.0f);
    }

    float rate = 0.2f;
    float depth = 0.5f;
    float feedback = 0.2f;
    float mix = 0.5f;

    std::string name() const override { return "Chorus"; }
    std::string type_name() const override { return "Chorus"; }

    void process(float* l, float* r, size_t nframes) override
    {
        if (m_bypassed) return;
        for (size_t i = 0; i < nframes; ++i)
        {
            // Modulation - separate LFO phases for stereo width
            float lfo_l = (std::sin(m_phase) + 1.0f) * 0.5f;
            float lfo_r = (std::sin(m_phase + M_PI/2.0f) + 1.0f) * 0.5f;
            
            m_phase += rate * 0.001f;
            if (m_phase > 2.0f * M_PI) m_phase -= 2.0f * M_PI;

            // Base delay ~30ms (1323 samples @ 44.1k)
            float delay_l = 1000.0f + lfo_l * depth * 500.0f;
            float delay_r = 1000.0f + lfo_r * depth * 500.0f;
            
            float out_l = read_buf(m_buf_l, m_pos, delay_l);
            float out_r = read_buf(m_buf_r, m_pos, delay_r);

            m_buf_l[m_pos] = l[i] + out_l * feedback;
            m_buf_r[m_pos] = r[i] + out_r * feedback;
            m_pos = (m_pos + 1) % m_buf_l.size();

            l[i] = l[i] * (1.0f - mix) + out_l * mix;
            r[i] = r[i] * (1.0f - mix) + out_r * mix;
        }
    }

    std::string get_state() const override {
        nlohmann::json j;
        j["rate"] = rate;
        j["depth"] = depth;
        j["feedback"] = feedback;
        j["mix"] = mix;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        try {
            auto j = nlohmann::json::parse(state);
            if (j.contains("rate")) rate = j["rate"];
            if (j.contains("depth")) depth = j["depth"];
            if (j.contains("feedback")) feedback = j["feedback"];
            if (j.contains("mix")) mix = j["mix"];
            if (j.contains("bypassed")) m_bypassed = j["bypassed"];
        } catch(...) {}
    }

    std::vector<std::string> get_presets() override {
        return {"Subtle Width", "Deep Lush", "Vibrato Chorus"};
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "Subtle Width") { rate = 0.1f; depth = 0.3f; feedback = 0.1f; mix = 0.3f; }
        else if (name == "Deep Lush") { rate = 0.2f; depth = 0.7f; feedback = 0.3f; mix = 0.5f; }
        else if (name == "Vibrato Chorus") { rate = 0.8f; depth = 0.5f; feedback = 0.0f; mix = 0.7f; }
    }

private:
    float read_buf(const std::vector<float>& buf, size_t pos, float delay) {
        float p = (float)pos - delay;
        while (p < 0) p += (float)buf.size();
        size_t i1 = (size_t)p;
        size_t i2 = (i1 + 1) % buf.size();
        float frac = p - (float)i1;
        return buf[i1] * (1.0f - frac) + buf[i2] * frac;
    }

    float m_phase = 0;
    size_t m_pos = 0;
    std::vector<float> m_buf_l, m_buf_r;
};

} // namespace tpanar_ns
