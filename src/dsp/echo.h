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
#include <vector>

namespace tpanar_ns
{

class EchoDSP : public tpanar_ns::DSP
{
public:
    EchoDSP() {
        m_current_preset = "Analog Echo";
        m_buf_l.resize(96000, 0.0f);
        m_buf_r.resize(96000, 0.0f);
    }

    float time = 0.3f;
    float feedback = 0.4f;
    float damp = 0.3f;
    float mix = 0.3f;

    std::string name() const override { return "Echo"; }
    std::string type_name() const override { return "Echo"; }

    void set_sample_rate(float sr) override {
        m_sample_rate = sr;
        size_t buf_size = (size_t)(sr * 2.0f); // 2 seconds max delay
        m_buf_l.assign(buf_size, 0.0f);
        m_buf_r.assign(buf_size, 0.0f);
        m_pos = 0;
    }

    void process(float* l, float* r, size_t nframes) override
    {
        if (m_bypassed) return;
        size_t delay_samples = (size_t)(time * m_sample_rate);
        if (delay_samples >= m_buf_l.size()) delay_samples = m_buf_l.size() - 1;

        for (size_t i = 0; i < nframes; ++i)
        {
            size_t read_pos = (m_pos + m_buf_l.size() - delay_samples) % m_buf_l.size();
            float out_l = m_buf_l[read_pos];
            float out_r = m_buf_r[read_pos];

            // Feedback with damping (LPF)
            m_filter_l = out_l * (1.0f - damp) + m_filter_l * damp;
            m_filter_r = out_r * (1.0f - damp) + m_filter_r * damp;

            m_buf_l[m_pos] = l[i] + m_filter_l * feedback;
            m_buf_r[m_pos] = r[i] + m_filter_r * feedback;
            m_pos = (m_pos + 1) % m_buf_l.size();

            l[i] = l[i] * (1.0f - mix) + out_l * mix;
            r[i] = r[i] * (1.0f - mix) + out_r * mix;
        }
    }

    std::string get_state() const override {
        nlohmann::json j;
        j["time"] = time;
        j["feedback"] = feedback;
        j["damp"] = damp;
        j["mix"] = mix;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        try {
            auto j = nlohmann::json::parse(state);
            if (j.contains("time")) time = j["time"];
            if (j.contains("feedback")) feedback = j["feedback"];
            if (j.contains("damp")) damp = j["damp"];
            if (j.contains("mix")) mix = j["mix"];
            if (j.contains("bypassed")) m_bypassed = j["bypassed"];
        } catch(...) {}
    }

    std::vector<std::string> get_presets() override {
        return {"Warm Slap", "Analog Echo", "Damped Repeats"};
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "Warm Slap") { time = 0.1f; feedback = 0.2f; damp = 0.5f; mix = 0.4f; }
        else if (name == "Analog Echo") { time = 0.4f; feedback = 0.6f; damp = 0.4f; mix = 0.3f; }
        else if (name == "Damped Repeats") { time = 0.3f; feedback = 0.8f; damp = 0.8f; mix = 0.5f; }
    }

private:
    size_t m_pos = 0;
    std::vector<float> m_buf_l, m_buf_r;
    float m_filter_l = 0, m_filter_r = 0;
    float m_sample_rate = 44100.0f;
};

} // namespace tpanar_ns
