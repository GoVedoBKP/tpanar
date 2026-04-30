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
#include <algorithm>

namespace tpanar_ns
{

class LimiterDSP : public tpanar_ns::DSP
{
public:
    LimiterDSP() { m_current_preset = "Safe Ceiling"; }
    float ceiling = 0.95f;
    float threshold = 0.95f;
    float release = 0.999f;

    std::string name() const override { return "Limiter"; }
    std::string type_name() const override { return "Limiter"; }

    void process(float* l, float* r, size_t nframes) override
    {
        if (m_bypassed) return;
        for (size_t i = 0; i < nframes; ++i)
        {
            float peak = std::max(std::abs(l[i]), std::abs(r[i]));
            
            if (peak > m_envelope) m_envelope = peak;
            else m_envelope = peak + (m_envelope - peak) * release;

            float gain = 1.0f;
            if (m_envelope > threshold) {
                gain = threshold / m_envelope;
            }

            l[i] *= gain;
            r[i] *= gain;

            // Hard clamp at ceiling
            l[i] = std::max(-ceiling, std::min(ceiling, l[i]));
            r[i] = std::max(-ceiling, std::min(ceiling, r[i]));
        }
    }

    std::string get_state() const override {
        nlohmann::json j;
        j["ceiling"] = ceiling;
        j["threshold"] = threshold;
        j["release"] = release;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        try {
            auto j = nlohmann::json::parse(state);
            if (j.contains("ceiling")) ceiling = j["ceiling"];
            if (j.contains("threshold")) threshold = j["threshold"];
            if (j.contains("release")) release = j["release"];
            if (j.contains("bypassed")) m_bypassed = j["bypassed"];
        } catch(...) {}
    }

    std::vector<std::string> get_presets() override {
        return {"Safe Ceiling", "Hard Wall", "Soft Limiter"};
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "Safe Ceiling") { ceiling = 0.95f; threshold = 0.90f; release = 0.999f; }
        else if (name == "Hard Wall") { ceiling = 1.0f; threshold = 1.0f; release = 0.99f; }
        else if (name == "Soft Limiter") { ceiling = 0.90f; threshold = 0.70f; release = 0.9995f; }
    }

private:
    float m_envelope = 0.0f;
};

} // namespace tpanar_ns
