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

#include "dsp_chain.h"
#include "gain.h"
#include "delay.h"
#include "reverb.h"
#include "limiter.h"
#include "exciter.h"
#include "phaser.h"
#include "flanger.h"
#include "echo.h"
#include "compressor.h"
#include "graphical_eq.h"
#include "cabinet.h"
#include "distortion.h"
#include "chorus.h"
#include "stereo_expander.h"
#include "ring_modulator.h"
#include "gate.h"
#include "vocoder.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace tpanar_ns
{

static std::unique_ptr<DSP> create_dsp(const std::string& type) {
    if (type == "Gain") return std::make_unique<GainDSP>();
    if (type == "Delay") return std::make_unique<DelayDSP>();
    if (type == "Reverb") return std::make_unique<ReverbDSP>();
    if (type == "Limiter") return std::make_unique<LimiterDSP>();
    if (type == "Exciter") return std::make_unique<ExciterDSP>();
    if (type == "Phaser") return std::make_unique<PhaserDSP>();
    if (type == "Flanger") return std::make_unique<FlangerDSP>();
    if (type == "Echo") return std::make_unique<EchoDSP>();
    if (type == "Compressor") return std::make_unique<CompressorDSP>();
    if (type == "GraphicalEQ" || type == "Graphical EQ") return std::make_unique<GraphicalEQDSP>();
    if (type == "Cabinet") return std::make_unique<CabinetDSP>();
    if (type == "Distortion") return std::make_unique<DistortionDSP>();
    if (type == "Chorus") return std::make_unique<ChorusDSP>();
    if (type == "StereoExpander" || type == "Stereo Expander") return std::make_unique<StereoExpanderDSP>();
    if (type == "RingModulator" || type == "Ring Modulator") return std::make_unique<RingModulatorDSP>();
    if (type == "Gate") return std::make_unique<GateDSP>();
    if (type == "Vocoder") return std::make_unique<VocoderDSP>();
    return nullptr;
}

void DSPChain::process(float* l,
                       float* r,
                       size_t nframes)
{
    for (size_t i = 0; i < MAX_INSERTS; ++i)
    {
        if (m_enabled[i] && m_effects[i])
            m_effects[i]->process(l, r, nframes);
    }
}

void DSPChain::set(size_t index, ::std::unique_ptr<DSP> dsp)
{
    if (index < MAX_INSERTS)
        m_effects[index] = std::move(dsp);
}

void DSPChain::enable(size_t index, bool en)
{
    if (index < MAX_INSERTS)
        m_enabled[index] = en;
}

void DSPChain::move_up(size_t index)
{
    if (index > 0 && index < MAX_INSERTS) {
        std::swap(m_effects[index], m_effects[index - 1]);
        std::swap(m_enabled[index], m_enabled[index - 1]);
    }
}

void DSPChain::move_down(size_t index)
{
    if (index < MAX_INSERTS - 1) {
        std::swap(m_effects[index], m_effects[index + 1]);
        std::swap(m_enabled[index], m_enabled[index + 1]);
    }
}

void DSPChain::remove(size_t index)
{
    if (index < MAX_INSERTS) {
        m_effects[index].reset();
        m_enabled[index] = false;
    }
}

void DSPChain::save_chain(const std::string& path)
{
    nlohmann::json j;
    to_json(&j);
    std::ofstream f(path);
    if (f.is_open()) f << j.dump(4);
}

void DSPChain::load_chain(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    try {
        auto j = nlohmann::json::parse(f);
        from_json(&j);
    } catch (...) {}
}

void DSPChain::to_json(void* j_ptr) const {
    auto& j = *static_cast<nlohmann::json*>(j_ptr);
    j = nlohmann::json::array();
    for (size_t i = 0; i < MAX_INSERTS; ++i) {
        if (m_effects[i]) {
            nlohmann::json fx;
            fx["type"] = m_effects[i]->type_name();
            fx["state"] = nlohmann::json::parse(m_effects[i]->get_state());
            fx["enabled"] = m_enabled[i];
            j.push_back(fx);
        }
    }
}

void DSPChain::from_json(const void* j_ptr) {
    auto& j = *static_cast<const nlohmann::json*>(j_ptr);
    if (j.is_array()) {
        for (size_t i = 0; i < MAX_INSERTS; ++i) {
            m_effects[i].reset();
            m_enabled[i] = false;
        }

        size_t idx = 0;
        for (const auto& fx : j) {
            if (idx >= MAX_INSERTS) break;
            if (fx.contains("type")) {
                auto dsp = create_dsp(fx["type"]);
                if (dsp) {
                    if (fx.contains("state")) dsp->set_state(fx["state"].dump());
                    m_effects[idx] = std::move(dsp);
                    m_enabled[idx] = fx.value("enabled", true);
                    idx++;
                }
            }
        }
    }
}

} // namespace tpanar_ns
