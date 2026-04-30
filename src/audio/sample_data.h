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
#include <vector>
#include <cmath>
#include <algorithm>

namespace tpanar_ns
{

    struct SampleData
    {
        ::std::vector<float> left;
        ::std::vector<float> right;
        int sample_rate = 44100;

        void to_mono_l() { right.clear(); }
        void to_mono_r() { if (!right.empty()) left = right; right.clear(); }
        void to_mono_mix() {
            if (right.empty()) return;
            for (size_t i = 0; i < left.size(); ++i) {
                left[i] = (left[i] + right[i]) * 0.5f;
            }
            right.clear();
        }
        void to_stereo() {
            if (!right.empty()) return;
            right = left;
        }

        void normalize(size_t start, size_t end) {
            float max_amp = 0.0f;
            end = std::min(end, left.size());
            for (size_t i = start; i < end; ++i) {
                max_amp = std::max(max_amp, std::abs(left[i]));
                if (!right.empty()) max_amp = std::max(max_amp, std::abs(right[i]));
            }
            if (max_amp < 1e-6f) return;
            float factor = 1.0f / max_amp;
            for (size_t i = start; i < end; ++i) {
                left[i] *= factor;
                if (!right.empty()) right[i] *= factor;
            }
        }

        void adjust_volume(size_t start, size_t end, float factor) {
            end = std::min(end, left.size());
            for (size_t i = start; i < end; ++i) {
                left[i] *= factor;
                if (!right.empty()) right[i] *= factor;
            }
        }

        void silence(size_t start, size_t end) {
            end = std::min(end, left.size());
            for (size_t i = start; i < end; ++i) {
                left[i] = 0.0f;
                if (!right.empty()) right[i] = 0.0f;
            }
        }

        void insert_silence(size_t pos, size_t len) {
            pos = std::min(pos, left.size());
            left.insert(left.begin() + pos, len, 0.0f);
            if (!right.empty()) {
                right.insert(right.begin() + pos, len, 0.0f);
            }
        }

        void crop(size_t start, size_t end) {
            end = std::min(end, left.size());
            if (start >= end) return;
            left.assign(left.begin() + start, left.begin() + end);
            if (!right.empty()) {
                right.assign(right.begin() + start, right.begin() + end);
            }
        }

        void fade_in(size_t start, size_t end, bool log) {
            end = std::min(end, left.size());
            size_t len = end - start;
            if (len == 0) return;
            for (size_t i = 0; i < len; ++i) {
                float t = (float)i / (float)len;
                float gain = log ? (powf(10.0f, t) - 1.0f) / 9.0f : t;
                left[start + i] *= gain;
                if (!right.empty()) right[start + i] *= gain;
            }
        }

        void fade_out(size_t start, size_t end, bool log) {
            end = std::min(end, left.size());
            size_t len = end - start;
            if (len == 0) return;
            for (size_t i = 0; i < len; ++i) {
                float t = 1.0f - (float)i / (float)len;
                float gain = log ? (powf(10.0f, t) - 1.0f) / 9.0f : t;
                left[start + i] *= gain;
                if (!right.empty()) right[start + i] *= gain;
            }
        }

        SampleData cut(size_t start, size_t end) {
            end = std::min(end, left.size());
            SampleData result;
            result.sample_rate = sample_rate;
            if (start >= end) return result;

            result.left.assign(left.begin() + start, left.begin() + end);
            left.erase(left.begin() + start, left.begin() + end);
            
            if (!right.empty()) {
                result.right.assign(right.begin() + start, right.begin() + end);
                right.erase(right.begin() + start, right.begin() + end);
            }
            return result;
        }

        void paste_at(size_t pos, const SampleData& other) {
            pos = std::min(pos, left.size());
            left.insert(left.begin() + pos, other.left.begin(), other.left.end());
            if (!other.right.empty()) {
                if (right.empty()) right.resize(left.size() - other.left.size(), 0.0f);
                right.insert(right.begin() + pos, other.right.begin(), other.right.end());
            } else if (!right.empty()) {
                right.insert(right.begin() + pos, other.left.size(), 0.0f);
            }
        }
    };

} // namespace tpanar_ns
