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

#include "reference_analyzer.h"
#include "../io/audio_file.h"
#include <fftw3.h>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace tpanar_ns
{

ReferenceAnalyzer::ReferenceAnalyzer()
{
    m_spectral_envelope.fill(0.0f);
    m_spectral_magnitudes.fill(0.0f);
}

bool ReferenceAnalyzer::load_reference(const std::string& path)
{
    std::vector<float> left, right;

    if (!AudioFile::load_audio(path, left, right, m_sample_rate)) {
        m_loaded = false;
        return false;
    }

    if (left.empty() || right.empty()) {
        m_loaded = false;
        return false;
    }

    m_path = path;

    size_t nframes = std::min(left.size(), right.size());

    float sum_squares = 0.0f;
    m_peak = 0.0f;

    for (size_t i = 0; i < nframes; ++i) {
        float l = left[i];
        float r = right[i];
        float sample = (l + r) * 0.5f;
        sum_squares += sample * sample;
        m_peak = std::max(m_peak, std::max(std::abs(l), std::abs(r)));
    }

    m_rms = std::sqrt(sum_squares / static_cast<float>(nframes));

    compute_stereo_stats(left, right);
    compute_spectral_envelope(left, right);

    m_loaded = true;
    return true;
}

void ReferenceAnalyzer::compute_stereo_stats(const std::vector<float>& left,
                                              const std::vector<float>& right)
{
    size_t n = std::min(left.size(), right.size());
    if (n == 0) return;

    float sum_lr = 0.0f;
    float sum_l2 = 0.0f;
    float sum_r2 = 0.0f;
    float sum_l_minus_r = 0.0f;
    float sum_l_minus_r2 = 0.0f;

    for (size_t i = 0; i < n; ++i) {
        float l = left[i];
        float r = right[i];
        sum_lr += l * r;
        sum_l2 += l * l;
        sum_r2 += r * r;
        float diff = l - r;
        sum_l_minus_r += diff;
        sum_l_minus_r2 += diff * diff;
    }

    float denom = std::sqrt(sum_l2 * sum_r2);
    m_stereo_correlation = (denom > 1e-10f) ? (sum_lr / denom) : 0.0f;
    m_stereo_correlation = std::max(-1.0f, std::min(1.0f, m_stereo_correlation));

    float mono_sum = sum_l2 + sum_r2;
    float diff_sum = sum_l_minus_r2;
    m_stereo_width = (mono_sum > 1e-10f) ?
        std::sqrt(diff_sum / mono_sum) : 0.0f;
}

void ReferenceAnalyzer::compute_spectral_envelope(const std::vector<float>& left,
                                                   const std::vector<float>& right)
{
    size_t n = std::min(left.size(), right.size());
    if (n == 0) return;

    size_t fft_size = 4096;
    size_t num_overlap = 3;
    size_t hop = fft_size / num_overlap;
    size_t num_frames = (n > fft_size) ? ((n - fft_size) / hop + 1) : 1;

    if (num_frames == 0) num_frames = 1;

    std::vector<double> in_l(fft_size, 0.0);
    std::vector<double> in_r(fft_size, 0.0);
    std::vector<double> out_l(fft_size * 2, 0.0);
    std::vector<double> out_r(fft_size * 2, 0.0);

    fftw_plan plan_l = fftw_plan_dft_r2c_1d(
        static_cast<int>(fft_size), in_l.data(),
        reinterpret_cast<fftw_complex*>(out_l.data()), FFTW_ESTIMATE);
    fftw_plan plan_r = fftw_plan_dft_r2c_1d(
        static_cast<int>(fft_size), in_r.data(),
        reinterpret_cast<fftw_complex*>(out_r.data()), FFTW_ESTIMATE);

    std::array<std::vector<float>, NUM_BANDS> band_sums;
    std::array<int, NUM_BANDS> band_counts;
    for (size_t i = 0; i < NUM_BANDS; ++i) {
        band_sums[i].resize(100, 0.0f);
        band_counts[i] = 0;
    }

    size_t center_freq = m_sample_rate / 2;
    size_t bin_width = center_freq / (fft_size / 2);

    std::vector<float> band_edges;
    for (size_t i = 0; i <= NUM_BANDS; ++i) {
        float low = (i == 0) ? 20.0f : BANDS[i-1] * 0.8f;
        float high = (i == NUM_BANDS) ? 20000.0f : BANDS[i] * 1.25f;
        band_edges.push_back(low);
    }

    for (size_t frame = 0; frame < num_frames; ++frame) {
        size_t start = frame * hop;
        if (start + fft_size > n) break;

        for (size_t i = 0; i < fft_size; ++i) {
            float window = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fft_size - 1)));
            float s_l = (start + i < left.size()) ? left[start + i] : 0.0f;
            float s_r = (start + i < right.size()) ? right[start + i] : 0.0f;
            in_l[i] = s_l * window;
            in_r[i] = s_r * window;
        }

        fftw_execute(plan_l);
        fftw_execute(plan_r);

        std::array<float, NUM_BANDS> frame_mags;
        frame_mags.fill(0.0f);

        for (size_t bin = 1; bin < fft_size / 2; ++bin) {
            float freq = bin * bin_width;
            if (freq < 20.0f || freq > 20000.0f) continue;

            float mag_l = std::sqrt(out_l[bin*2] * out_l[bin*2] + out_l[bin*2+1] * out_l[bin*2+1]);
            float mag_r = std::sqrt(out_r[bin*2] * out_r[bin*2] + out_r[bin*2+1] * out_r[bin*2+1]);
            float mag = (mag_l + mag_r) * 0.5f;

            for (size_t b = 0; b < NUM_BANDS; ++b) {
                float low = (b == 0) ? 20.0f : BANDS[b] * 0.7f;
                float high = (b == NUM_BANDS-1) ? 20000.0f : BANDS[b] * 1.4f;
                if (freq >= low && freq < high) {
                    frame_mags[b] = std::max(frame_mags[b], mag);
                    break;
                }
            }
        }

        for (size_t b = 0; b < NUM_BANDS; ++b) {
            band_sums[b].push_back(frame_mags[b]);
            if (band_sums[b].size() > 100) band_sums[b].erase(band_sums[b].begin());
        }
    }

    fftw_destroy_plan(plan_l);
    fftw_destroy_plan(plan_r);

    float overall_max = 0.0f;
    for (size_t b = 0; b < NUM_BANDS; ++b) {
        if (!band_sums[b].empty()) {
            float avg = std::accumulate(band_sums[b].begin(), band_sums[b].end(), 0.0f)
                       / static_cast<float>(band_sums[b].size());
            m_spectral_magnitudes[b] = avg;
            overall_max = std::max(overall_max, avg);
        }
    }

    if (overall_max > 1e-10f) {
        for (size_t b = 0; b < NUM_BANDS; ++b) {
            float normalized = m_spectral_magnitudes[b] / overall_max;
            m_spectral_envelope[b] = 20.0f * std::log10(normalized + 1e-10f);
        }
    }
}

ReferenceAnalyzer::MatchProfile ReferenceAnalyzer::get_match_profile(float target_rms) const
{
    MatchProfile profile;
    profile.target_rms = m_rms;
    profile.target_peak = m_peak;
    profile.target_width = m_stereo_width;
    profile.compression_threshold = 0.6f;
    profile.compression_ratio = 3.0f;
    profile.makeup_gain = 1.0f;

    float ref_rms_db = 20.0f * std::log10(m_rms + 1e-10f);
    float target_linear = std::pow(10.0f, target_rms / 20.0f);
    float current_linear = m_rms;
    profile.makeup_gain = target_linear / (current_linear + 1e-10f);
    profile.makeup_gain = std::min(profile.makeup_gain, 2.0f);

    for (size_t b = 0; b < NUM_BANDS; ++b) {
        profile.eq_corrections[b] = m_spectral_envelope[b];
    }

    return profile;
}

std::vector<float> ReferenceAnalyzer::get_gain_correction_curve() const
{
    std::vector<float> curve(NUM_BANDS);
    for (size_t b = 0; b < NUM_BANDS; ++b) {
        curve[b] = m_spectral_envelope[b];
    }
    return curve;
}

std::vector<float> ReferenceAnalyzer::get_compression_curve() const
{
    std::vector<float> curve(128, 1.0f);
    float ref_rms_db = 20.0f * std::log10(m_rms + 1e-10f);

    for (int i = 0; i < 128; ++i) {
        float level = static_cast<float>(i) / 127.0f;
        float level_db = 20.0f * std::log10(level + 1e-10f);

        if (level_db > ref_rms_db + 3.0f) {
            float above = level_db - (ref_rms_db + 3.0f);
            float compressed = (ref_rms_db + 3.0f) + above / 3.0f;
            curve[i] = std::pow(10.0f, (compressed - level_db) / 20.0f);
        } else {
            curve[i] = 1.0f;
        }
    }
    return curve;
}

} // namespace tpanar_ns
