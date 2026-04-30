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

class PhaserDSP : public tpanar_ns::DSP
{
public:
    PhaserDSP() {
        m_current_preset = "Slow Sweep";
        m_ap_l.resize(4, 0.0f);
        m_ap_r.resize(4, 0.0f);
    }

    float rate = 0.5f;
    float depth = 0.5f;
    float feedback = 0.5f;
    float mix = 0.5f;

    std::string name() const override { return "Phaser"; }
    std::string type_name() const override { return "Phaser"; }

    void process(float* l, float* r, size_t nframes) override
    {
        if (m_bypassed) return;
        for (size_t i = 0; i < nframes; ++i)
        {
            float in_l = l[i] + m_feedback_l * feedback;
            float in_r = r[i] + m_feedback_r * feedback;

            float lfo = (std::sin(m_phase) + 1.0f) * 0.5f;
            m_phase += rate * 0.001f;
            if (m_phase > 2.0f * M_PI) m_phase -= 2.0f * M_PI;

            float freq = 0.01f + lfo * depth * 0.5f;
            float a = (1.0f - freq) / (1.0f + freq);

            // 4-stage all-pass
            float out_l = in_l;
            float out_r = in_r;
            for(int j=0; j<4; ++j) {
                float tmp_l = a * (out_l + m_ap_l[j]) - m_prev_ap_l[j];
                m_prev_ap_l[j] = out_l;
                m_ap_l[j] = tmp_l;
                out_l = tmp_l;

                float tmp_r = a * (out_r + m_ap_r[j]) - m_prev_ap_r[j];
                m_prev_ap_r[j] = out_r;
                m_ap_r[j] = tmp_r;
                out_r = tmp_r;
            }

            m_feedback_l = out_l;
            m_feedback_r = out_r;

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
        return {"Slow Sweep", "Fast Wobble", "Deep Space", "Jet Stream"};
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "Slow Sweep") { rate = 0.1f; depth = 0.7f; feedback = 0.5f; mix = 0.5f; }
        else if (name == "Fast Wobble") { rate = 0.8f; depth = 0.3f; feedback = 0.2f; mix = 0.5f; }
        else if (name == "Deep Space") { rate = 0.05f; depth = 0.9f; feedback = 0.8f; mix = 0.6f; }
        else if (name == "Jet Stream") { rate = 0.3f; depth = 0.6f; feedback = 0.9f; mix = 0.7f; }
    }

private:
    float m_phase = 0;
    std::vector<float> m_ap_l, m_ap_r;
    float m_prev_ap_l[4] = {0}, m_prev_ap_r[4] = {0};
    float m_feedback_l = 0, m_feedback_r = 0;
};

} // namespace tpanar_ns
