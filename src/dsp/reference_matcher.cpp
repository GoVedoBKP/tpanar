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

#include "reference_matcher.h"
#include <cmath>
#include <algorithm>

namespace tpanar_ns
{

ReferenceMatcherDSP::ReferenceMatcherDSP()
{
    m_current_preset = "Default";
    reset();
    for (auto& f : m_eq_filters) f.reset();
}

void ReferenceMatcherDSP::reset()
{
    m_compressor_env = 0.0f;
    for (auto& f : m_eq_filters) f.reset();
}

bool ReferenceMatcherDSP::load_reference(const std::string& path)
{
    bool success = m_analyzer.load_reference(path);
    if (success) {
        m_sample_rate = static_cast<float>(m_analyzer.get_sample_rate());
        auto profile = m_analyzer.get_match_profile(m_target_rms_db);
        m_target_width = profile.target_width;
        m_compressor_threshold = profile.compression_threshold;
        m_compressor_ratio = profile.compression_ratio;
        m_compressor_makeup = profile.makeup_gain;
        update_gains();
        update_eq_filters();
    }
    return success;
}

void ReferenceMatcherDSP::update_gains()
{
    if (!m_analyzer.is_loaded() || !m_match_rms) {
        m_gain_correction = 1.0f;
        return;
    }

    auto profile = m_analyzer.get_match_profile(m_target_rms_db);
    m_gain_correction = profile.makeup_gain;
}

void ReferenceMatcherDSP::update_eq_filters()
{
    if (!m_analyzer.is_loaded() || !m_match_eq) {
        for (size_t i = 0; i < ReferenceAnalyzer::NUM_BANDS; ++i) {
            m_eq_filters[i].set_peaking(
                ReferenceAnalyzer::BANDS[i], 1.414f, 0.0f, m_sample_rate);
        }
        return;
    }

    auto corrections = m_analyzer.get_spectral_envelope();
    for (size_t i = 0; i < ReferenceAnalyzer::NUM_BANDS; ++i) {
        float correction = corrections[i];
        correction = std::max(-12.0f, std::min(12.0f, correction * 0.8f));
        m_eq_filters[i].set_peaking(
            ReferenceAnalyzer::BANDS[i], 1.414f, correction, m_sample_rate);
    }
}

void ReferenceMatcherDSP::process(float* l, float* r, size_t nframes)
{
    if (m_bypassed || !m_analyzer.is_loaded()) return;

    float attack_coeff = std::exp(-1.0f / (0.005f * m_sample_rate));
    float release_coeff = std::exp(-1.0f / (0.1f * m_sample_rate));

    for (size_t i = 0; i < nframes; ++i) {
        float sl = l[i];
        float sr = r[i];

        float orig_l = sl;
        float orig_r = sr;

        if (m_match_width) {
            float mono = (sl + sr) * 0.5f;
            float diff = sl - sr;
            float target_diff = diff * m_target_width;
            float new_mono = (orig_l + orig_r) * 0.5f;
            sl = new_mono + target_diff * 0.5f;
            sr = new_mono - target_diff * 0.5f;
        }

        if (m_match_eq) {
            for (size_t b = 0; b < ReferenceAnalyzer::NUM_BANDS; ++b) {
                sl = m_eq_filters[b].process_l(sl);
                sr = m_eq_filters[b].process_r(sr);
            }
        }

        if (m_match_rms) {
            sl *= m_gain_correction;
            sr *= m_gain_correction;
        }

        if (m_match_dynamics) {
            float env = std::max(std::abs(sl), std::abs(sr));
            if (env > m_compressor_env) {
                m_compressor_env = env + attack_coeff * (m_compressor_env - env);
            } else {
                m_compressor_env = env + release_coeff * (m_compressor_env - env);
            }

            float gain = 1.0f;
            if (m_compressor_env > m_compressor_threshold && m_compressor_env > 0.00001f) {
                float target_db = 20.0f * std::log10(m_compressor_threshold);
                float env_db = 20.0f * std::log10(m_compressor_env);
                float compressed_db = target_db + (env_db - target_db) / m_compressor_ratio;
                gain = std::pow(10.0f, (compressed_db - env_db) / 20.0f);
            }

            sl *= gain * m_compressor_makeup;
            sr *= gain * m_compressor_makeup;
        }

        if (m_limit_enabled) {
            sl = std::max(-m_limit_threshold, std::min(m_limit_threshold, sl));
            sr = std::max(-m_limit_threshold, std::min(m_limit_threshold, sr));

            float abs_l = std::abs(sl);
            float abs_r = std::abs(sr);
            float peak = std::max(abs_l, abs_r);

            if (peak > m_limit_threshold) {
                float ceiling = m_limit_threshold;
                float ceiling_db = 20.0f * std::log10(ceiling);
                float peak_db = 20.0f * std::log10(peak);
                float gain_reduction_db = peak_db - ceiling_db;
                float gain_reduction = std::pow(10.0f, gain_reduction_db / 20.0f);
                sl = sl / peak * ceiling * (1.0f + (1.0f - peak / ceiling) * 0.5f);
                sr = sr / peak * ceiling * (1.0f + (1.0f - peak / ceiling) * 0.5f);
            }
        }

        if (m_mix < 1.0f) {
            sl = orig_l + (sl - orig_l) * m_mix;
            sr = orig_r + (sr - orig_r) * m_mix;
        }

        l[i] = sl;
        r[i] = sr;
    }
}

std::string ReferenceMatcherDSP::get_state() const
{
    nlohmann::json j;
    j["mix"] = m_mix;
    j["match_rms"] = m_match_rms;
    j["match_eq"] = m_match_eq;
    j["match_width"] = m_match_width;
    j["match_dynamics"] = m_match_dynamics;
    j["limit_enabled"] = m_limit_enabled;
    j["limit_threshold"] = m_limit_threshold;
    j["target_rms_db"] = m_target_rms_db;
    j["reference_path"] = m_analyzer.get_path();
    j["bypassed"] = m_bypassed;
    return j.dump();
}

void ReferenceMatcherDSP::set_state(const std::string& state)
{
    try {
        auto j = nlohmann::json::parse(state);
        if (j.contains("mix")) m_mix = j["mix"];
        if (j.contains("match_rms")) m_match_rms = j["match_rms"];
        if (j.contains("match_eq")) m_match_eq = j["match_eq"];
        if (j.contains("match_width")) m_match_width = j["match_width"];
        if (j.contains("match_dynamics")) m_match_dynamics = j["match_dynamics"];
        if (j.contains("limit_enabled")) m_limit_enabled = j["limit_enabled"];
        if (j.contains("limit_threshold")) m_limit_threshold = j["limit_threshold"];
        if (j.contains("target_rms_db")) m_target_rms_db = j["target_rms_db"];
        if (j.contains("bypassed")) m_bypassed = j["bypassed"];
        if (j.contains("reference_path")) {
            std::string ref_path = j["reference_path"];
            if (!ref_path.empty()) {
                load_reference(ref_path);
            }
        }
        update_gains();
        update_eq_filters();
    } catch (...) {}
}

std::vector<std::string> ReferenceMatcherDSP::get_presets()
{
    return {
        "Default",
        "Loudness Match",
        "Warm Reference",
        "Bright Reference",
        "Subtle Match"
    };
}

void ReferenceMatcherDSP::load_preset(const std::string& name)
{
    m_current_preset = name;

    if (name == "Default") {
        m_mix = 1.0f;
        m_match_rms = true;
        m_match_eq = true;
        m_match_width = true;
        m_match_dynamics = false;
        m_limit_enabled = true;
        m_limit_threshold = 0.95f;
        m_target_rms_db = -14.0f;
    }
    else if (name == "Loudness Match") {
        m_mix = 1.0f;
        m_match_rms = true;
        m_match_eq = true;
        m_match_width = true;
        m_match_dynamics = true;
        m_limit_enabled = true;
        m_limit_threshold = 0.98f;
        m_target_rms_db = -12.0f;
    }
    else if (name == "Warm Reference") {
        m_mix = 0.8f;
        m_match_rms = true;
        m_match_eq = true;
        m_match_width = false;
        m_match_dynamics = false;
        m_limit_enabled = true;
        m_limit_threshold = 0.95f;
        m_target_rms_db = -16.0f;
    }
    else if (name == "Bright Reference") {
        m_mix = 0.7f;
        m_match_rms = true;
        m_match_eq = true;
        m_match_width = true;
        m_match_dynamics = false;
        m_limit_enabled = true;
        m_limit_threshold = 0.93f;
        m_target_rms_db = -14.0f;
    }
    else if (name == "Subtle Match") {
        m_mix = 0.5f;
        m_match_rms = true;
        m_match_eq = false;
        m_match_width = false;
        m_match_dynamics = false;
        m_limit_enabled = true;
        m_limit_threshold = 0.98f;
        m_target_rms_db = -18.0f;
    }

    update_gains();
    update_eq_filters();
}

} // namespace tpanar_ns
