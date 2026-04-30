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
#include <vector>
#include <cmath>
#include <nlohmann/json.hpp>

namespace tpanar_ns
{

class ReverbDSP : public tpanar_ns::DSP
{
public:
    ReverbDSP() {
        m_current_preset = "Small Room";
        // Simple Schroeder Reverb initialization
        // Comb filters
        m_combs_l.resize(4);
        m_combs_r.resize(4);
        int comb_lengths[] = {1116, 1188, 1277, 1356};
        for(int i=0; i<4; ++i) {
            m_combs_l[i].buffer.resize(comb_lengths[i], 0.0f);
            m_combs_r[i].buffer.resize(comb_lengths[i] + 23, 0.0f); // Slightly offset for stereo
        }
        // All-pass filters
        m_allpass_l.resize(2);
        m_allpass_r.resize(2);
        int ap_lengths[] = {556, 441};
        for(int i=0; i<2; ++i) {
            m_allpass_l[i].buffer.resize(ap_lengths[i], 0.0f);
            m_allpass_r[i].buffer.resize(ap_lengths[i] + 17, 0.0f);
        }
    }

    float room_size = 0.5f;
    float damp = 0.2f;
    float mix = 0.3f;

    std::string name() const override { return "Reverb"; }
    std::string type_name() const override { return "Reverb"; }

    void process(float* l, float* r, size_t nframes) override
    {
        if (m_bypassed) return;
        for (size_t i = 0; i < nframes; ++i)
        {
            float in_l = l[i];
            float in_r = r[i];
            float out_l = 0, out_r = 0;

            // Parallel Comb Filters
            float feedback = room_size * 0.9f;
            for(int j=0; j<4; ++j) {
                out_l += process_comb(m_combs_l[j], in_l, feedback);
                out_r += process_comb(m_combs_r[j], in_r, feedback);
            }

            // Series All-pass Filters
            for(int j=0; j<2; ++j) {
                out_l = process_allpass(m_allpass_l[j], out_l);
                out_r = process_allpass(m_allpass_r[j], out_r);
            }

            l[i] = in_l * (1.0f - mix) + out_l * mix * 0.25f;
            r[i] = in_r * (1.0f - mix) + out_r * mix * 0.25f;
        }
    }

    std::string get_state() const override {
        nlohmann::json j;
        j["room_size"] = room_size;
        j["damp"] = damp;
        j["mix"] = mix;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        try {
            auto j = nlohmann::json::parse(state);
            if (j.contains("room_size")) room_size = j["room_size"];
            if (j.contains("damp")) damp = j["damp"];
            if (j.contains("mix")) mix = j["mix"];
            if (j.contains("bypassed")) m_bypassed = j["bypassed"];
        } catch(...) {}
    }

    std::vector<std::string> get_presets() override {
        return {"Small Room", "Large Hall", "Cathedral", "Dark Plate"};
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "Small Room") { room_size = 0.3f; damp = 0.5f; mix = 0.2f; }
        else if (name == "Large Hall") { room_size = 0.7f; damp = 0.2f; mix = 0.4f; }
        else if (name == "Cathedral") { room_size = 0.9f; damp = 0.1f; mix = 0.5f; }
        else if (name == "Dark Plate") { room_size = 0.6f; damp = 0.8f; mix = 0.3f; }
    }

private:
    struct DelayLine {
        std::vector<float> buffer;
        size_t pos = 0;
        float filter_state = 0;
    };

    float process_comb(DelayLine& dl, float input, float feedback) {
        float output = dl.buffer[dl.pos];
        dl.filter_state = (output * (1.0f - damp)) + (dl.filter_state * damp);
        dl.buffer[dl.pos] = input + (dl.filter_state * feedback);
        dl.pos = (dl.pos + 1) % dl.buffer.size();
        return output;
    }

    float process_allpass(DelayLine& dl, float input) {
        float buf_out = dl.buffer[dl.pos];
        float output = -input + buf_out;
        dl.buffer[dl.pos] = input + (buf_out * 0.5f);
        dl.pos = (dl.pos + 1) % dl.buffer.size();
        return output;
    }

    std::vector<DelayLine> m_combs_l, m_combs_r;
    std::vector<DelayLine> m_allpass_l, m_allpass_r;
};

} // namespace tpanar_ns
