/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once
#include "dsp.h"
#include "../util/linear_smoother.h"
#include <nlohmann/json.hpp>

namespace tpanar_ns
{

class GainDSP : public tpanar_ns::DSP
{
public:
    GainDSP() : m_smoother(1.0f) { m_current_preset = "Unity"; }
    float gain = 1.0f;

    std::string name() const override { return "Gain"; }
    std::string type_name() const override { return "Gain"; }

    void process(float* l,
                 float* r,
                 size_t nframes) override
    {
        if (m_bypassed) return;
        m_smoother.set_target(gain, nframes);
        for (size_t i = 0; i < nframes; ++i)
        {
            float cur = m_smoother.next();
            l[i] *= cur;
            r[i] *= cur;
        }
    }

    std::string get_state() const override {
        nlohmann::json j;
        j["gain"] = gain;
        j["bypassed"] = m_bypassed;
        return j.dump();
    }

    void set_state(const std::string& state) override {
        auto j = nlohmann::json::parse(state);
        if (j.contains("gain")) {
            gain = j["gain"];
            m_smoother = LinearSmoother(gain);
        }
        if (j.contains("bypassed")) m_bypassed = j["bypassed"];
    }

    std::vector<std::string> get_presets() override {
        return {"Unity", "Silent", "Boost (+6dB)", "Boost (+12dB)", "Attenuate (-6dB)"};
    }

    void load_preset(const std::string& name) override {
        m_current_preset = name;
        if (name == "Unity") gain = 1.0f;
        else if (name == "Silent") gain = 0.0f;
        else if (name == "Boost (+6dB)") gain = 2.0f;
        else if (name == "Boost (+12dB)") gain = 4.0f;
        else if (name == "Attenuate (-6dB)") gain = 0.5f;
    }

private:
    LinearSmoother m_smoother;
};

} // namespace tpanar_ns
