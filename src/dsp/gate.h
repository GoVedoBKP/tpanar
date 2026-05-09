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
#include <algorithm>

namespace tpanar_ns
{

class GateDSP : public tpanar_ns::DSP
{
public:
    GateDSP() { m_current_preset = "Noise Floor"; }
    float threshold = 0.1f; // 0 to 1
    float range = 0.0f;     // 0 to 1 (0 is total silence when closed)
    float attack = 0.01f;   // seconds
    float release = 0.1f;   // seconds

    std::string name() const override { return "Gate"; }
    std::string type_name() const override { return "Gate"; }

    void set_sample_rate(float sr) override { m_sample_rate = sr; }

    void process(float* l, float* r, size_t nframes) override
    {
        if (m_bypassed) return;

        float attack_coeff = std::exp(-1.0f / (attack * m_sample_rate));
        float release_coeff = std::exp(-1.0f / (release * m_sample_rate));

        for (size_t i = 0; i < nframes; ++i)
        {
            float env = std::max(std::abs(l[i]), std::abs(r[i]));
            
            if (env > m_envelope)
                m_envelope = env + attack_coeff * (m_envelope - env);
            else
                m_envelope = env + release_coeff * (m_envelope - env);

            float target_gain = (m_envelope > threshold) ? 1.0f : range;
            
            // Smooth gain change
            m_gain = target_gain + 0.95f * (m_gain - target_gain);

            l[i] *= m_gain;
            r[i] *= m_gain;
        }
    }

    std::string get_state() const override {
        nlohmann::json j;
        j["threshold"] = threshold;
        j["range"] = range;
        j["attack"] = attack;
        j["release"] = release;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        try {
            auto j = nlohmann::json::parse(state);
            if (j.contains("threshold")) threshold = j["threshold"];
            if (j.contains("range")) range = j["range"];
            if (j.contains("attack")) attack = j["attack"];
            if (j.contains("release")) release = j["release"];
            if (j.contains("bypassed")) m_bypassed = j["bypassed"];
        } catch(...) {}
    }

    std::vector<std::string> get_presets() override {
        return {"Noise Floor", "Tight Drums", "Hard Cut", "Kick Gate", "Snare Gate", "Room Gate"};
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "Noise Floor")     { threshold = 0.05f; range = 0.0f; attack = 0.010f; release = 0.100f; }
        else if (name == "Tight Drums"){ threshold = 0.20f; range = 0.0f; attack = 0.001f; release = 0.050f; }
        else if (name == "Hard Cut")   { threshold = 0.50f; range = 0.0f; attack = 0.001f; release = 0.010f; }
        // Kick: high threshold clamps bleed, fast open/close for each kick hit
        else if (name == "Kick Gate")  { threshold = 0.30f; range = 0.0f; attack = 0.001f; release = 0.080f; }
        // Snare: medium threshold, very quick release preserving snare tail
        else if (name == "Snare Gate") { threshold = 0.25f; range = 0.0f; attack = 0.001f; release = 0.050f; }
        // Room mic gate: lower threshold for subtle ambient bleed removal
        else if (name == "Room Gate")  { threshold = 0.12f; range = 0.0f; attack = 0.005f; release = 0.200f; }
    }

private:
    float m_envelope = 0.0f;
    float m_gain = 1.0f;
    float m_sample_rate = 44100.0f;
};

} // namespace tpanar_ns
