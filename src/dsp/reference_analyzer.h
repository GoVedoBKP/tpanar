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
#include <string>
#include <vector>
#include <array>
#include <cstdint>

namespace tpanar_ns
{

class ReferenceAnalyzer
{
public:
    static constexpr size_t NUM_BANDS = 12;
    static constexpr std::array<float, NUM_BANDS> BANDS = {{
        32.0f, 64.0f, 125.0f, 250.0f, 500.0f, 1000.0f,
        2000.0f, 4000.0f, 8000.0f, 12000.0f, 16000.0f, 20000.0f
    }};

    ReferenceAnalyzer();

    bool load_reference(const std::string& path);
    bool is_loaded() const { return m_loaded; }
    const std::string& get_path() const { return m_path; }

    float get_rms() const { return m_rms; }
    float get_peak() const { return m_peak; }
    float get_stereo_correlation() const { return m_stereo_correlation; }
    float get_stereo_width() const { return m_stereo_width; }
    const std::array<float, NUM_BANDS>& get_spectral_envelope() const { return m_spectral_envelope; }
    uint32_t get_sample_rate() const { return m_sample_rate; }

    std::vector<float> get_gain_correction_curve() const;
    std::vector<float> get_compression_curve() const;

    struct MatchProfile {
        float target_rms;
        float target_peak;
        float target_width;
        std::array<float, NUM_BANDS> eq_corrections;
        float compression_threshold;
        float compression_ratio;
        float makeup_gain;
    };

    MatchProfile get_match_profile(float target_rms = -18.0f) const;

private:
    void compute_spectral_envelope(const std::vector<float>& left, const std::vector<float>& right);
    void compute_stereo_stats(const std::vector<float>& left, const std::vector<float>& right);

    std::string m_path;
    bool m_loaded = false;
    uint32_t m_sample_rate = 44100;

    float m_rms = 0.0f;
    float m_peak = 0.0f;
    float m_stereo_correlation = 0.0f;
    float m_stereo_width = 0.0f;
    std::array<float, NUM_BANDS> m_spectral_envelope;
    std::array<float, NUM_BANDS> m_spectral_magnitudes;
};

} // namespace tpanar_ns
