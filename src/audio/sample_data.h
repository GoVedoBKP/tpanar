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
#include <memory>

namespace tpanar_ns
{

    struct WaveformOverview
    {
        struct MinMax { float min, max; };
        ::std::vector<MinMax> left;
        ::std::vector<MinMax> right;
        size_t block_size = 128;
    };

    struct SampleData
    {
        ::std::vector<float> left;
        ::std::vector<float> right;
        int sample_rate = 44100;

        mutable ::std::shared_ptr<WaveformOverview> overview;

        void invalidate_overview() { overview.reset(); }

        void to_mono_l() { right.clear(); invalidate_overview(); }
        void to_mono_r() { if (!right.empty()) left = right; right.clear(); invalidate_overview(); }
        void to_mono_mix() {
            if (right.empty()) return;
            for (size_t i = 0; i < left.size(); ++i) {
                left[i] = (left[i] + right[i]) * 0.5f;
            }
            right.clear();
            invalidate_overview();
        }
        void to_stereo() {
            if (!right.empty()) return;
            right = left;
            invalidate_overview();
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
            invalidate_overview();
        }

        void adjust_volume(size_t start, size_t end, float factor) {
            end = std::min(end, left.size());
            for (size_t i = start; i < end; ++i) {
                left[i] *= factor;
                if (!right.empty()) right[i] *= factor;
            }
            invalidate_overview();
        }

        void silence(size_t start, size_t end) {
            end = std::min(end, left.size());
            for (size_t i = start; i < end; ++i) {
                left[i] = 0.0f;
                if (!right.empty()) right[i] = 0.0f;
            }
            invalidate_overview();
        }

        void insert_silence(size_t pos, size_t len) {
            pos = std::min(pos, left.size());
            left.insert(left.begin() + pos, len, 0.0f);
            if (!right.empty()) {
                right.insert(right.begin() + pos, len, 0.0f);
            }
            invalidate_overview();
        }

        void crop(size_t start, size_t end) {
            end = std::min(end, left.size());
            if (start >= end) return;
            left.assign(left.begin() + start, left.begin() + end);
            if (!right.empty()) {
                right.assign(right.begin() + start, right.begin() + end);
            }
            invalidate_overview();
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
            invalidate_overview();
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
            invalidate_overview();
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
            invalidate_overview();
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
            invalidate_overview();
        }

        void update_overview(size_t block_size = 128) const {
            if (overview && overview->block_size == block_size) return;
            
            overview = std::make_shared<WaveformOverview>();
            overview->block_size = block_size;
            
            auto generate = [&](const std::vector<float>& data, std::vector<WaveformOverview::MinMax>& dest) {
                if (data.empty()) return;
                size_t num_blocks = (data.size() + block_size - 1) / block_size;
                dest.reserve(num_blocks);
                for (size_t b = 0; b < num_blocks; ++b) {
                    size_t b_start = b * block_size;
                    size_t b_end = std::min(b_start + block_size, data.size());
                    float min_v = 1.0f, max_v = -1.0f;
                    for (size_t s = b_start; s < b_end; ++s) {
                        if (data[s] < min_v) min_v = data[s];
                        if (data[s] > max_v) max_v = data[s];
                    }
                    dest.push_back({min_v, max_v});
                }
            };
            
            generate(left, overview->left);
            generate(right, overview->right);
        }
    };

} // namespace tpanar_ns
