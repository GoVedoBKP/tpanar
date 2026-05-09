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

class CompressorDSP : public tpanar_ns::DSP
{
public:
    CompressorDSP() { m_current_preset = "Gentle Master"; }
    float threshold = 0.5f; // 0 to 1
    float ratio = 4.0f;     // 1 to 20
    float attack = 0.01f;   // seconds
    float release = 0.1f;   // seconds
    float makeup = 1.0f;    // 0 to 2

    std::string name() const override { return "Compressor"; }
    std::string type_name() const override { return "Compressor"; }

    void set_sample_rate(float sr) override { m_sample_rate = sr; }

    void process(float* l, float* r, size_t nframes) override
    {
        if (m_bypassed) return;

        float attack_coeff = std::exp(-1.0f / (attack * m_sample_rate));
        float release_coeff = std::exp(-1.0f / (release * m_sample_rate));

        for (size_t i = 0; i < nframes; ++i)
        {
            float input_l = l[i];
            float input_r = r[i];
            float env = std::max(std::abs(input_l), std::abs(input_r));

            if (env > m_envelope)
                m_envelope = env + attack_coeff * (m_envelope - env);
            else
                m_envelope = env + release_coeff * (m_envelope - env);

            float gain = 1.0f;
            if (m_envelope > threshold && m_envelope > 0.00001f) {
                float target_db = 20.0f * std::log10(threshold);
                float env_db = 20.0f * std::log10(m_envelope);
                float compressed_db = target_db + (env_db - target_db) / ratio;
                gain = std::pow(10.0f, (compressed_db - env_db) / 20.0f);
            }

            l[i] = input_l * gain * makeup;
            r[i] = input_r * gain * makeup;
        }
    }

    std::string get_state() const override {
        nlohmann::json j;
        j["threshold"] = threshold;
        j["ratio"] = ratio;
        j["attack"] = attack;
        j["release"] = release;
        j["makeup"] = makeup;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        try {
            auto j = nlohmann::json::parse(state);
            if (j.contains("threshold")) threshold = j["threshold"];
            if (j.contains("ratio")) ratio = j["ratio"];
            if (j.contains("attack")) attack = j["attack"];
            if (j.contains("release")) release = j["release"];
            if (j.contains("makeup")) makeup = j["makeup"];
            if (j.contains("bypassed")) m_bypassed = j["bypassed"];
        } catch(...) {}
    }

    std::vector<std::string> get_presets() override {
        return {"Gentle Master", "Punchy Drums", "Vocal Leveler", "Hard Squash",
                "Kick Smack", "Snare Pop", "Drum Bus", "Parallel Crush", "Overhead Glue"};
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "Gentle Master")    { threshold = 0.8f;  ratio =  2.0f; attack = 0.050f; release = 0.200f; makeup = 1.10f; }
        else if (name == "Punchy Drums"){ threshold = 0.5f;  ratio =  4.0f; attack = 0.010f; release = 0.100f; makeup = 1.20f; }
        else if (name == "Vocal Leveler"){ threshold = 0.6f; ratio =  3.0f; attack = 0.020f; release = 0.300f; makeup = 1.10f; }
        else if (name == "Hard Squash") { threshold = 0.3f;  ratio = 10.0f; attack = 0.005f; release = 0.050f; makeup = 1.50f; }
        // Kick: slow enough to let transient through, fast release for each hit
        else if (name == "Kick Smack")  { threshold = 0.45f; ratio =  6.0f; attack = 0.004f; release = 0.080f; makeup = 1.30f; }
        // Snare: very fast to clamp body, quick release to restore crack
        else if (name == "Snare Pop")   { threshold = 0.50f; ratio =  5.0f; attack = 0.001f; release = 0.060f; makeup = 1.25f; }
        // Drum bus glue: preserves transients, controls dynamics gently
        else if (name == "Drum Bus")    { threshold = 0.65f; ratio =  3.0f; attack = 0.025f; release = 0.150f; makeup = 1.15f; }
        // Parallel crush: extreme ratio for blending underneath the dry signal
        else if (name == "Parallel Crush"){ threshold = 0.15f; ratio = 20.0f; attack = 0.001f; release = 0.050f; makeup = 1.80f; }
        // Overhead: gentle leveling without killing cymbal transients
        else if (name == "Overhead Glue"){ threshold = 0.72f; ratio =  2.0f; attack = 0.035f; release = 0.250f; makeup = 1.05f; }
    }

private:
    float m_envelope = 0.0f;
    float m_sample_rate = 44100.0f;
};

} // namespace tpanar_ns
