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

namespace tpanar_ns
{

class StereoExpanderDSP : public tpanar_ns::DSP
{
public:
    float width = 1.0f; // 0.0 (Mono) to 2.0 (Extra wide), 1.0 is original

    std::string name() const override { return "Stereo Expander"; }
    std::string type_name() const override { return "StereoExpander"; }

    void process(float* l, float* r, size_t nframes) override
    {
        if (m_bypassed) return;
        for (size_t i = 0; i < nframes; ++i)
        {
            float mid = (l[i] + r[i]) * 0.5f;
            float side = (l[i] - r[i]) * 0.5f;

            l[i] = mid + side * width;
            r[i] = mid - side * width;
        }
    }

    std::string get_state() const override {
        nlohmann::json j;
        j["width"] = width;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        try {
            auto j = nlohmann::json::parse(state);
            if (j.contains("width")) width = j["width"];
            if (j.contains("bypassed")) m_bypassed = j["bypassed"];
        } catch(...) {}
    }

    std::vector<std::string> get_presets() override {
        return {"Mono", "Stereo (Normal)", "Wide", "Extra Wide"};
    }

    void load_preset(const std::string& name) override {
        if (name == "Mono") width = 0.0f;
        else if (name == "Stereo (Normal)") width = 1.0f;
        else if (name == "Wide") width = 1.5f;
        else if (name == "Extra Wide") width = 2.0f;
    }
};

} // namespace tpanar_ns
