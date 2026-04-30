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
#include "reference_analyzer.h"
#include <nlohmann/json.hpp>
#include <string>

namespace tpanar_ns
{

class ReferenceMatcherDSP : public tpanar_ns::DSP
{
public:
    struct Biquad {
        float b0, b1, b2, a1, a2;
        float x1_l, x2_l, y1_l, y2_l;
        float x1_r, x2_r, y1_r, y2_r;

        void reset() {
            x1_l = x2_l = y1_l = y2_l = 0;
            x1_r = x2_r = y1_r = y2_r = 0;
        }

        void set_peaking(float freq, float q, float gain_db, float sample_rate) {
            float a = std::pow(10.0f, gain_db / 40.0f);
            float w0 = 2.0f * M_PI * freq / sample_rate;
            float alpha = std::sin(w0) / (2.0f * q);
            float cos_w0 = std::cos(w0);

            float b0_raw = 1.0f + alpha * a;
            float b1_raw = -2.0f * cos_w0;
            float b2_raw = 1.0f - alpha * a;
            float a0_raw = 1.0f + alpha / a;
            float a1_raw = -2.0f * cos_w0;
            float a2_raw = 1.0f - alpha / a;

            b0 = b0_raw / a0_raw;
            b1 = b1_raw / a0_raw;
            b2 = b2_raw / a0_raw;
            a1 = a1_raw / a0_raw;
            a2 = a2_raw / a0_raw;
        }

        inline float process_l(float in) {
            float out = b0 * in + b1 * x1_l + b2 * x2_l - a1 * y1_l - a2 * y2_l;
            x2_l = x1_l; x1_l = in; y2_l = y1_l; y1_l = out;
            return out;
        }

        inline float process_r(float in) {
            float out = b0 * in + b1 * x1_r + b2 * x2_r - a1 * y1_r - a2 * y2_r;
            x2_r = x1_r; x1_r = in; y2_r = y1_r; y1_r = out;
            return out;
        }
    };

    struct LimiterBiquad {
        float b0, b1, b2, a1, a2;
        float x1_l, x2_l, y1_l, y2_l;
        float x1_r, x2_r, y1_r, y2_r;

        void reset() {
            x1_l = x2_l = y1_l = y2_l = 0;
            x1_r = x2_r = y1_r = y2_r = 0;
        }

        void set_lowpass(float freq, float q, float sample_rate) {
            float w0 = 2.0f * M_PI * freq / sample_rate;
            float alpha = std::sin(w0) / (2.0f * q);
            float cos_w0 = std::cos(w0);

            b0 = (1.0f - cos_w0) / 2.0f;
            b1 = 1.0f - cos_w0;
            b2 = (1.0f - cos_w0) / 2.0f;
            float a0 = 1.0f + alpha;
            b0 /= a0; b1 /= a0; b2 /= a0;
            a1 = -2.0f * cos_w0 / a0;
            a2 = (1.0f - alpha) / a0;
        }

        inline float process_l(float in) {
            float out = b0 * in + b1 * x1_l + b2 * x2_l - a1 * y1_l - a2 * y2_l;
            x2_l = x1_l; x1_l = in; y2_l = y1_l; y1_l = out;
            return out;
        }

        inline float process_r(float in) {
            float out = b0 * in + b1 * x1_r + b2 * x2_r - a1 * y1_r - a2 * y2_r;
            x2_r = x1_r; x1_r = in; y2_r = y1_r; y1_r = out;
            return out;
        }
    };

    ReferenceMatcherDSP();

    std::string name() const override { return "Reference Matcher"; }
    std::string type_name() const override { return "ReferenceMatcher"; }

    bool load_reference(const std::string& path);
    bool is_reference_loaded() const { return m_analyzer.is_loaded(); }
    const std::string& get_reference_path() const { return m_analyzer.get_path(); }

    void process(float* l, float* r, size_t nframes) override;

    void set_mix(float mix) { m_mix = std::max(0.0f, std::min(1.0f, mix)); }
    float get_mix() const { return m_mix; }

    void set_match_rms(bool b) { m_match_rms = b; update_gains(); }
    bool get_match_rms() const { return m_match_rms; }

    void set_match_eq(bool b) { m_match_eq = b; update_eq_filters(); }
    bool get_match_eq() const { return m_match_eq; }

    void set_match_width(bool b) { m_match_width = b; }
    bool get_match_width() const { return m_match_width; }

    void set_match_dynamics(bool b) { m_match_dynamics = b; }
    bool get_match_dynamics() const { return m_match_dynamics; }

    void set_target_rms(float db) { m_target_rms_db = db; update_gains(); }
    float get_target_rms() const { return m_target_rms_db; }

    void set_limit(bool b) { m_limit_enabled = b; }
    bool get_limit() const { return m_limit_enabled; }

    void set_limit_threshold(float t) { m_limit_threshold = t; }
    float get_limit_threshold() const { return m_limit_threshold; }

    ReferenceAnalyzer::MatchProfile get_profile() const {
        return m_analyzer.get_match_profile(m_target_rms_db);
    }

    std::string get_state() const override;
    void set_state(const std::string& state) override;

    std::vector<std::string> get_presets() override;
    void load_preset(const std::string& name) override;

private:
    void update_gains();
    void update_eq_filters();
    void reset();

    ReferenceAnalyzer m_analyzer;

    bool m_match_rms = true;
    bool m_match_eq = true;
    bool m_match_width = true;
    bool m_match_dynamics = false;
    bool m_limit_enabled = true;

    float m_mix = 1.0f;
    float m_target_rms_db = -14.0f;
    float m_gain_correction = 1.0f;
    float m_limit_threshold = 0.95f;

    std::array<Biquad, ReferenceAnalyzer::NUM_BANDS> m_eq_filters;

    float m_compressor_env = 0.0f;
    float m_compressor_threshold = 0.6f;
    float m_compressor_ratio = 3.0f;
    float m_compressor_makeup = 1.0f;

    float m_target_width = 0.5f;
    float m_input_width = 0.5f;

    float m_sample_rate = 44100.0f;
};

} // namespace tpanar_ns
