/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once
#include "dsp.h"
#include <array>
#include <nlohmann/json.hpp>

namespace tpanar_ns
{

class DelayDSP : public tpanar_ns::DSP
{
public:
    DelayDSP() { m_current_preset = "Default"; }
    static constexpr size_t MAX_DELAY = 48000;

    float feedback = 0.4f;
    float mix = 0.3f;

    std::string name() const override { return "Delay"; }
    std::string type_name() const override { return "Delay"; }

    void process(float* l,
                 float* r,
                 size_t nframes) override
    {
        if (m_bypassed) return;
        for (size_t i = 0; i < nframes; ++i)
        {
            float dl = m_buffer_l[m_pos];
            float dr = m_buffer_r[m_pos];

            m_buffer_l[m_pos] = l[i] + dl * feedback;
            m_buffer_r[m_pos] = r[i] + dr * feedback;

            l[i] = l[i] * (1 - mix) + dl * mix;
            r[i] = r[i] * (1 - mix) + dr * mix;

            m_pos = (m_pos + 1) % MAX_DELAY;
        }
    }

    std::string get_state() const override {
        nlohmann::json j;
        j["feedback"] = feedback;
        j["mix"] = mix;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        auto j = nlohmann::json::parse(state);
        if (j.contains("feedback")) feedback = j["feedback"];
        if (j.contains("mix")) mix = j["mix"];
        if (j.contains("bypassed")) m_bypassed = j["bypassed"];
    }

    std::vector<std::string> get_presets() override {
        return {"Default", "Slapback", "Long Eco", "Feedback Beast", "Drum Slap"};
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "Default")            { feedback = 0.40f; mix = 0.30f; }
        else if (name == "Slapback")      { feedback = 0.10f; mix = 0.50f; m_pos = (m_pos + MAX_DELAY - 2000) % MAX_DELAY; }
        else if (name == "Long Eco")      { feedback = 0.60f; mix = 0.40f; }
        else if (name == "Feedback Beast"){ feedback = 0.95f; mix = 0.50f; }
        // Very short single repeat (≈30 ms) adds snap to drums without washing them out
        else if (name == "Drum Slap")     { feedback = 0.05f; mix = 0.20f; m_pos = (m_pos + MAX_DELAY - 1323) % MAX_DELAY; }
    }

private:
    ::std::array<float, MAX_DELAY> m_buffer_l{};
    ::std::array<float, MAX_DELAY> m_buffer_r{};
    size_t m_pos = 0;
};

} // namespace tpanar_ns
