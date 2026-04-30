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

#include <wx/app.h>
#include <wx/artprov.h>
#include <wx/dcclient.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include "wx_tracks_panel.h"
#include "wx_detached_frame.h"
#include "../core/engine.h"
#include "../instrument/sample_instrument.h"
#include "../instrument/soundfont_instrument.h"
#include "../sequencer/pattern.h"
#include "../edit/cmd_track_cut.h"
#include "../edit/cmd_track_copy.h"
#include "../edit/cmd_track_paste.h"
#include "../edit/cmd_track_silence.h"
#include "../edit/cmd_track_insert_silence.h"
#include "theme.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace tpanar_ns {

namespace {

constexpr int kTrackHeaderWidth = 290;
constexpr int kMinimizeButtonX = 5;
constexpr int kMinimizeButtonW = 20;
constexpr int kControlButtonW = 30;
constexpr int kControlButtonH = 24;
constexpr int kControlButtonGap = 10;
constexpr int kControlRowX = 40;

enum class HeaderControl {
    Minimize,
    Mute,
    Solo,
    Record,
    VolDown,
    VolUp,
    PanLeft,
    PanRight
};

bool has_track_header_controls(const Engine& engine, int track_idx)
{
    if (track_idx < 0 || (size_t)track_idx >= engine.track_count() || engine.is_tempo_track((size_t)track_idx)) {
        return false;
    }
    const auto kind = engine.track((size_t)track_idx).kind();
    return kind == TrackKind::Audio || kind == TrackKind::Note;
}

bool has_record_arm(const Engine& engine, int track_idx)
{
    return track_idx >= 0 &&
           (size_t)track_idx < engine.track_count() &&
           engine.track((size_t)track_idx).kind() == TrackKind::Audio &&
           !engine.is_tempo_track((size_t)track_idx);
}

wxRect control_rect(int track_y, int track_h, HeaderControl control, bool has_record)
{
    if (control == HeaderControl::Minimize) {
        return wxRect(kMinimizeButtonX, track_y + (track_h - 18) / 2, kMinimizeButtonW, 18);
    }

    int index = 0;
    switch (control) {
        case HeaderControl::Mute:    index = 0; break;
        case HeaderControl::Solo:    index = 1; break;
        case HeaderControl::Record:  index = 2; break;
        case HeaderControl::VolDown: index = has_record ? 3 : 2; break;
        case HeaderControl::VolUp:   index = has_record ? 4 : 3; break;
        case HeaderControl::PanLeft: index = has_record ? 5 : 4; break;
        case HeaderControl::PanRight:index = has_record ? 6 : 5; break;
        case HeaderControl::Minimize: break;
    }

    const int row_y = track_y + track_h - kControlButtonH - 6;
    const int x = kControlRowX + index * (kControlButtonW + kControlButtonGap);
    return wxRect(x, row_y, kControlButtonW, kControlButtonH);
}

wxString pan_status_label(float pan)
{
    if (std::fabs(pan) < 0.05f) return "C";
    const int amount = (int)std::round(std::fabs(pan) * 100.0f);
    return wxString::Format("%c%d", pan < 0.0f ? 'L' : 'R', amount);
}

void draw_header_button(wxDC& dc,
                        const wxRect& rect,
                        const wxString& label,
                        const wxColour& fg,
                        const wxColour& bg,
                        bool active = false)
{
    wxColour fill = bg;
    if (active) {
        fill = wxColour(std::min(255, bg.Red() + 30),
                        std::min(255, bg.Green() + 30),
                        std::min(255, bg.Blue() + 30));
    }
    dc.SetBrush(wxBrush(fill));
    dc.SetPen(wxPen(fg));
    dc.DrawRectangle(rect);
    dc.SetTextForeground(fg);
    dc.DrawText(label, rect.x + (rect.width - dc.GetTextExtent(label).GetWidth()) / 2,
                rect.y + (rect.height - dc.GetTextExtent(label).GetHeight()) / 2);
}

struct TrackEditTarget {
    int content_track_idx = -1;
    Track* display_track = nullptr;
    Track* content_track = nullptr;
    SampleInstrument* sampler = nullptr;
};

bool resolve_track_edit_target(Engine& engine, int display_track_idx, TrackEditTarget& target)
{
    if (display_track_idx < 0 || (size_t)display_track_idx >= engine.track_count()) {
        return false;
    }

    target.display_track = &engine.track((size_t)display_track_idx);
    target.content_track_idx = display_track_idx;
    target.content_track = &engine.track((size_t)target.content_track_idx);
    Instrument* inst = target.content_track->instrument();
    if (!inst || inst->type() != InstrumentType::Sampler) {
        return false;
    }

    target.sampler = static_cast<SampleInstrument*>(inst);
    return true;
}

TrackClipboard::AudioEntry slice_sample_data(const SampleData& data, size_t start_sample, size_t end_sample)
{
    TrackClipboard::AudioEntry entry;
    if (start_sample >= end_sample || start_sample >= data.left.size()) {
        return entry;
    }

    const size_t clipped_end = std::min(end_sample, data.left.size());
    entry.left.assign(data.left.begin() + start_sample, data.left.begin() + clipped_end);
    if (!data.right.empty() && start_sample < data.right.size()) {
        const size_t right_end = std::min(clipped_end, data.right.size());
        entry.right.assign(data.right.begin() + start_sample, data.right.begin() + right_end);
    }
    return entry;
}

bool extract_track_clip(Engine& engine,
                        int display_track_idx,
                        size_t start_sample_global,
                        size_t end_sample_global,
                        TrackClipboard::AudioEntry& clip)
{
    TrackEditTarget target;
    if (!resolve_track_edit_target(engine, display_track_idx, target)) {
        return false;
    }

    if (target.display_track->kind() == TrackKind::Audio || target.display_track->kind() == TrackKind::Pilot) {
        const size_t s_idx =
            target.display_track->audio_sample_index() > 0 ? (size_t)(target.display_track->audio_sample_index() - 1) : 0;
        if (s_idx >= target.sampler->sample_count()) {
            return false;
        }
        const auto& sample_entry = target.sampler->get_sample(s_idx);
        if (!sample_entry.data) {
            return false;
        }
        clip = slice_sample_data(*sample_entry.data, start_sample_global, end_sample_global);
        return !clip.left.empty();
    }

    const uint32_t lpb = engine.lpb();
    const double bpm = engine.tempo();
    const double sample_rate = engine.sample_rate();
    const double samples_per_beat = (sample_rate * 60.0) / bpm;
    const double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;
    auto order = engine.order_list();
    size_t current_sample_pos = 0;

    for (size_t pat_idx = 0; pat_idx < order.size(); ++pat_idx) {
        auto& pattern = engine.pattern(order[pat_idx]);
        const size_t pat_rows = pattern.row_count();
        const size_t pat_samples = (size_t)(pat_rows * samples_per_row);

        for (size_t row = 0; row < pat_rows; ++row) {
            const size_t row_sample_pos = current_sample_pos + (size_t)(row * samples_per_row);
            const size_t num_cols = pattern.column_count(target.content_track_idx);
            for (size_t c = 0; c < num_cols; ++c) {
                const auto& event = pattern.event(target.content_track_idx, row, c);
                if (event.note == 255 || event.note == 254) {
                    continue;
                }

                const size_t s_idx =
                    (event.sample_idx > 0) ? (event.sample_idx - 1) : target.sampler->selected_sample();
                if (s_idx >= target.sampler->sample_count()) {
                    continue;
                }

                const auto& sample_entry = target.sampler->get_sample(s_idx);
                if (!sample_entry.data) {
                    continue;
                }

                const size_t sample_len = sample_entry.data->left.size();
                const size_t note_start_global = row_sample_pos;
                const size_t note_end_global = row_sample_pos + sample_len;
                const size_t intersect_start = std::max(start_sample_global, note_start_global);
                const size_t intersect_end = std::min(end_sample_global, note_end_global);
                if (intersect_start >= intersect_end) {
                    continue;
                }

                clip = slice_sample_data(*sample_entry.data,
                                         intersect_start - note_start_global,
                                         intersect_end - note_start_global);
                return !clip.left.empty();
            }
        }
        current_sample_pos += pat_samples;
    }

    return false;
}

bool cut_track_selection(Engine& engine, int display_track_idx, size_t start_sample_global, size_t end_sample_global)
{
    TrackEditTarget target;
    if (!resolve_track_edit_target(engine, display_track_idx, target)) {
        return false;
    }

    if (target.display_track->kind() == TrackKind::Audio || target.display_track->kind() == TrackKind::Pilot) {
        const size_t s_idx =
            target.display_track->audio_sample_index() > 0 ? (size_t)(target.display_track->audio_sample_index() - 1) : 0;
        if (s_idx >= target.sampler->sample_count()) {
            return false;
        }
        auto& sample_entry = target.sampler->get_sample(s_idx);
        if (!sample_entry.data) {
            return false;
        }
        auto new_data = std::make_shared<SampleData>(*sample_entry.data);
        target.sampler->push_undo(s_idx);
        new_data->cut(start_sample_global, end_sample_global);
        target.sampler->update_sample_data(s_idx, new_data);
        return true;
    }

    const uint32_t lpb = engine.lpb();
    const double bpm = engine.tempo();
    const double sample_rate = engine.sample_rate();
    const double samples_per_beat = (sample_rate * 60.0) / bpm;
    const double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;
    auto order = engine.order_list();
    size_t current_sample_pos = 0;

    struct Mod { size_t start; size_t end; };
    std::map<size_t, std::vector<Mod>> mods;

    for (size_t pat_idx = 0; pat_idx < order.size(); ++pat_idx) {
        auto& pattern = engine.pattern(order[pat_idx]);
        const size_t pat_rows = pattern.row_count();
        const size_t pat_samples = (size_t)(pat_rows * samples_per_row);

        for (size_t row = 0; row < pat_rows; ++row) {
            const size_t row_sample_pos = current_sample_pos + (size_t)(row * samples_per_row);
            const size_t num_cols = pattern.column_count(target.content_track_idx);
            for (size_t c = 0; c < num_cols; ++c) {
                const auto& event = pattern.event(target.content_track_idx, row, c);
                if (event.note == 255 || event.note == 254) {
                    continue;
                }

                const size_t s_idx =
                    (event.sample_idx > 0) ? (event.sample_idx - 1) : target.sampler->selected_sample();
                if (s_idx >= target.sampler->sample_count()) {
                    continue;
                }

                auto& sample_entry = target.sampler->get_sample(s_idx);
                if (!sample_entry.data) {
                    continue;
                }

                const size_t sample_len = sample_entry.data->left.size();
                const size_t note_start_global = row_sample_pos;
                const size_t note_end_global = row_sample_pos + sample_len;
                const size_t intersect_start = std::max(start_sample_global, note_start_global);
                const size_t intersect_end = std::min(end_sample_global, note_end_global);
                if (intersect_start < intersect_end) {
                    mods[s_idx].push_back({intersect_start - note_start_global, intersect_end - note_start_global});
                }
            }
        }
        current_sample_pos += pat_samples;
    }

    bool changed = false;
    for (auto& pair : mods) {
        const size_t s_idx = pair.first;
        auto& sample_entry = target.sampler->get_sample(s_idx);
        auto new_data = std::make_shared<SampleData>(*sample_entry.data);
        std::sort(pair.second.begin(), pair.second.end(), [](const Mod& a, const Mod& b) {
            return a.start > b.start;
        });
        target.sampler->push_undo(s_idx);
        for (const auto& m : pair.second) {
            new_data->cut(m.start, m.end);
        }
        target.sampler->update_sample_data(s_idx, new_data);
        changed = true;
    }

    return changed;
}

bool silence_track_selection(Engine& engine, int display_track_idx, size_t start_sample_global, size_t end_sample_global)
{
    TrackEditTarget target;
    if (!resolve_track_edit_target(engine, display_track_idx, target)) {
        return false;
    }

    if (target.display_track->kind() == TrackKind::Audio || target.display_track->kind() == TrackKind::Pilot) {
        const size_t s_idx =
            target.display_track->audio_sample_index() > 0 ? (size_t)(target.display_track->audio_sample_index() - 1) : 0;
        if (s_idx >= target.sampler->sample_count()) {
            return false;
        }
        auto& sample_entry = target.sampler->get_sample(s_idx);
        if (!sample_entry.data) {
            return false;
        }
        auto new_data = std::make_shared<SampleData>(*sample_entry.data);
        target.sampler->push_undo(s_idx);
        new_data->silence(start_sample_global, end_sample_global);
        target.sampler->update_sample_data(s_idx, new_data);
        return true;
    }

    const uint32_t lpb = engine.lpb();
    const double bpm = engine.tempo();
    const double sample_rate = engine.sample_rate();
    const double samples_per_beat = (sample_rate * 60.0) / bpm;
    const double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;
    auto order = engine.order_list();
    size_t current_sample_pos = 0;

    struct Mod { size_t start; size_t end; };
    std::map<size_t, std::vector<Mod>> mods;

    for (size_t pat_idx = 0; pat_idx < order.size(); ++pat_idx) {
        auto& pattern = engine.pattern(order[pat_idx]);
        const size_t pat_rows = pattern.row_count();
        const size_t pat_samples = (size_t)(pat_rows * samples_per_row);

        for (size_t row = 0; row < pat_rows; ++row) {
            const size_t row_sample_pos = current_sample_pos + (size_t)(row * samples_per_row);
            const size_t num_cols = pattern.column_count(target.content_track_idx);
            for (size_t c = 0; c < num_cols; ++c) {
                const auto& event = pattern.event(target.content_track_idx, row, c);
                if (event.note == 255 || event.note == 254) {
                    continue;
                }

                const size_t s_idx =
                    (event.sample_idx > 0) ? (event.sample_idx - 1) : target.sampler->selected_sample();
                if (s_idx >= target.sampler->sample_count()) {
                    continue;
                }

                auto& sample_entry = target.sampler->get_sample(s_idx);
                if (!sample_entry.data) {
                    continue;
                }

                const size_t sample_len = sample_entry.data->left.size();
                const size_t note_start_global = row_sample_pos;
                const size_t note_end_global = row_sample_pos + sample_len;
                const size_t intersect_start = std::max(start_sample_global, note_start_global);
                const size_t intersect_end = std::min(end_sample_global, note_end_global);
                if (intersect_start < intersect_end) {
                    mods[s_idx].push_back({intersect_start - note_start_global, intersect_end - note_start_global});
                }
            }
        }
        current_sample_pos += pat_samples;
    }

    bool changed = false;
    for (auto& pair : mods) {
        const size_t s_idx = pair.first;
        auto& sample_entry = target.sampler->get_sample(s_idx);
        auto new_data = std::make_shared<SampleData>(*sample_entry.data);
        target.sampler->push_undo(s_idx);
        for (const auto& m : pair.second) {
            new_data->silence(m.start, m.end);
        }
        target.sampler->update_sample_data(s_idx, new_data);
        changed = true;
    }

    return changed;
}

bool insert_silence_into_track(Engine& engine, int display_track_idx, size_t insert_pos_global, size_t insert_duration)
{
    TrackEditTarget target;
    if (!resolve_track_edit_target(engine, display_track_idx, target)) {
        return false;
    }

    if (target.display_track->kind() == TrackKind::Audio || target.display_track->kind() == TrackKind::Pilot) {
        const size_t s_idx =
            target.display_track->audio_sample_index() > 0 ? (size_t)(target.display_track->audio_sample_index() - 1) : 0;
        if (s_idx >= target.sampler->sample_count()) {
            return false;
        }
        auto& sample_entry = target.sampler->get_sample(s_idx);
        if (!sample_entry.data) {
            return false;
        }
        auto new_data = std::make_shared<SampleData>(*sample_entry.data);
        target.sampler->push_undo(s_idx);
        new_data->insert_silence(insert_pos_global, insert_duration);
        target.sampler->update_sample_data(s_idx, new_data);
        return true;
    }

    const uint32_t lpb = engine.lpb();
    const double bpm = engine.tempo();
    const double sample_rate = engine.sample_rate();
    const double samples_per_beat = (sample_rate * 60.0) / bpm;
    const double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;
    auto order = engine.order_list();
    size_t current_sample_pos = 0;

    struct Mod { size_t pos; };
    std::map<size_t, std::vector<Mod>> mods;

    for (size_t pat_idx = 0; pat_idx < order.size(); ++pat_idx) {
        auto& pattern = engine.pattern(order[pat_idx]);
        const size_t pat_rows = pattern.row_count();
        const size_t pat_samples = (size_t)(pat_rows * samples_per_row);

        for (size_t row = 0; row < pat_rows; ++row) {
            const size_t row_sample_pos = current_sample_pos + (size_t)(row * samples_per_row);
            const size_t num_cols = pattern.column_count(target.content_track_idx);
            for (size_t c = 0; c < num_cols; ++c) {
                const auto& event = pattern.event(target.content_track_idx, row, c);
                if (event.note == 255 || event.note == 254) {
                    continue;
                }

                const size_t s_idx =
                    (event.sample_idx > 0) ? (event.sample_idx - 1) : target.sampler->selected_sample();
                if (s_idx >= target.sampler->sample_count()) {
                    continue;
                }

                auto& sample_entry = target.sampler->get_sample(s_idx);
                if (!sample_entry.data) {
                    continue;
                }

                const size_t sample_len = sample_entry.data->left.size();
                if (insert_pos_global >= row_sample_pos && insert_pos_global < row_sample_pos + sample_len) {
                    mods[s_idx].push_back({insert_pos_global - row_sample_pos});
                }
            }
        }
        current_sample_pos += pat_samples;
    }

    bool changed = false;
    for (auto& pair : mods) {
        const size_t s_idx = pair.first;
        auto& sample_entry = target.sampler->get_sample(s_idx);
        auto new_data = std::make_shared<SampleData>(*sample_entry.data);
        std::sort(pair.second.begin(), pair.second.end(), [](const Mod& a, const Mod& b) {
            return a.pos > b.pos;
        });
        target.sampler->push_undo(s_idx);
        for (const auto& m : pair.second) {
            new_data->insert_silence(m.pos, insert_duration);
        }
        target.sampler->update_sample_data(s_idx, new_data);
        changed = true;
    }

    return changed;
}

bool paste_clip_into_track(Engine& engine,
                           int display_track_idx,
                           size_t paste_pos_global,
                           const TrackClipboard::AudioEntry& clip)
{
    if (clip.left.empty()) {
        return false;
    }

    TrackEditTarget target;
    if (!resolve_track_edit_target(engine, display_track_idx, target)) {
        return false;
    }

    auto apply_clip = [&clip](SampleData& data, size_t insert_pos) {
        insert_pos = std::min(insert_pos, data.left.size());

        std::vector<float> new_left;
        new_left.insert(new_left.end(), data.left.begin(), data.left.begin() + insert_pos);
        new_left.insert(new_left.end(), clip.left.begin(), clip.left.end());
        new_left.insert(new_left.end(), data.left.begin() + insert_pos, data.left.end());

        std::vector<float> new_right;
        if (!data.right.empty()) {
            new_right.insert(new_right.end(), data.right.begin(), data.right.begin() + insert_pos);
            if (!clip.right.empty()) {
                new_right.insert(new_right.end(), clip.right.begin(), clip.right.end());
            } else {
                new_right.insert(new_right.end(), clip.left.size(), 0.0f);
            }
            new_right.insert(new_right.end(), data.right.begin() + insert_pos, data.right.end());
        }

        data.left = std::move(new_left);
        data.right = std::move(new_right);
    };

    if (target.display_track->kind() == TrackKind::Audio || target.display_track->kind() == TrackKind::Pilot) {
        const size_t s_idx =
            target.display_track->audio_sample_index() > 0 ? (size_t)(target.display_track->audio_sample_index() - 1) : 0;
        if (s_idx >= target.sampler->sample_count()) {
            return false;
        }
        auto& sample_entry = target.sampler->get_sample(s_idx);
        if (!sample_entry.data) {
            return false;
        }
        auto new_data = std::make_shared<SampleData>(*sample_entry.data);
        target.sampler->push_undo(s_idx);
        apply_clip(*new_data, paste_pos_global);
        target.sampler->update_sample_data(s_idx, new_data);
        return true;
    }

    const uint32_t lpb = engine.lpb();
    const double bpm = engine.tempo();
    const double sample_rate = engine.sample_rate();
    const double samples_per_beat = (sample_rate * 60.0) / bpm;
    const double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;
    auto order = engine.order_list();
    size_t current_sample_pos = 0;

    for (size_t pat_idx = 0; pat_idx < order.size(); ++pat_idx) {
        auto& pattern = engine.pattern(order[pat_idx]);
        const size_t pat_rows = pattern.row_count();
        const size_t pat_samples = (size_t)(pat_rows * samples_per_row);

        for (size_t row = 0; row < pat_rows; ++row) {
            const size_t row_sample_pos = current_sample_pos + (size_t)(row * samples_per_row);
            const size_t num_cols = pattern.column_count(target.content_track_idx);
            for (size_t c = 0; c < num_cols; ++c) {
                const auto& event = pattern.event(target.content_track_idx, row, c);
                if (event.note == 255 || event.note == 254) {
                    continue;
                }

                const size_t s_idx =
                    (event.sample_idx > 0) ? (event.sample_idx - 1) : target.sampler->selected_sample();
                if (s_idx >= target.sampler->sample_count()) {
                    continue;
                }

                auto& sample_entry = target.sampler->get_sample(s_idx);
                if (!sample_entry.data) {
                    continue;
                }

                const size_t sample_len = sample_entry.data->left.size();
                if (paste_pos_global < row_sample_pos || paste_pos_global >= row_sample_pos + sample_len) {
                    continue;
                }

                auto new_data = std::make_shared<SampleData>(*sample_entry.data);
                target.sampler->push_undo(s_idx);
                apply_clip(*new_data, paste_pos_global - row_sample_pos);
                target.sampler->update_sample_data(s_idx, new_data);
                return true;
            }
        }
        current_sample_pos += pat_samples;
    }

    return false;
}

} // namespace

static void draw_waveform_helper(wxDC& dc, int x, int y, int w, int h, const SampleData& data, const wxColour& col, size_t max_samples = 0) {
    if (data.left.empty() || w <= 0) return;
    dc.SetPen(wxPen(col));
    
    bool is_stereo = !data.right.empty();
    int ch_h = is_stereo ? h / 2 : h;

    size_t data_len = (max_samples > 0) ? std::min(max_samples, data.left.size()) : data.left.size();
    if (data_len == 0) return;

    auto draw_channel = [&](const std::vector<float>& ch_data, int ch_y) {
        int mid_y = ch_y + ch_h / 2;
        double samples_per_pixel = (double)data_len / w;
        for (int i = 0; i < w; ++i) {
            size_t start = (size_t)(i * samples_per_pixel);
            size_t end = (size_t)((i + 1) * samples_per_pixel);
            if (end > data_len) end = data_len;
            if (start >= end) {
                if (start < data_len) {
                    int amp = (int)(ch_data[start] * (ch_h / 2 - 2));
                    dc.DrawLine(x + i, mid_y - amp, x + i, mid_y + amp);
                }
                continue;
            }
            float min_v = 1.0f, max_v = -1.0f;
            for (size_t s = start; s < end; ++s) {
                if (ch_data[s] < min_v) min_v = ch_data[s];
                if (ch_data[s] > max_v) max_v = ch_data[s];
            }
            int y1 = mid_y + (int)(min_v * (ch_h / 2 - 2));
            int y2 = mid_y + (int)(max_v * (ch_h / 2 - 2));
            dc.DrawLine(x + i, y1, x + i, y2);
        }
    };

    draw_channel(data.left, y);
    if (is_stereo) {
        draw_channel(data.right, y + ch_h);
        // Draw a small divider line
        dc.SetPen(wxPen(wxColour(col.Red(), col.Green(), col.Blue(), 80)));
        dc.DrawLine(x, y + ch_h, x + w, y + ch_h);
        dc.SetPen(wxPen(col));
    }
}

enum {
    ID_ZOOM_IN = 10001,
    ID_ZOOM_OUT,
    ID_VIEW_ALL,
    ID_VIEW_SEL,
    ID_DETACH,
    ID_CUT,
    ID_COPY,
    ID_PASTE,
    ID_SILENCE,
    ID_INSERT_SILENCE,
    ID_MENU_CUT,
    ID_MENU_COPY,
    ID_MENU_PASTE,
    ID_MENU_SILENCE,
    ID_MENU_INSERT_SILENCE,
    ID_MENU_RETRIGGER_SELECTION,
    ID_MENU_RETRIGGER_WHOLE,
    ID_MENU_UNDO,
    ID_MENU_REDO
};

wxBEGIN_EVENT_TABLE(TracksPanel, wxPanel)
    EVT_BUTTON(ID_ZOOM_IN, TracksPanel::on_zoom_in)
    EVT_BUTTON(ID_ZOOM_OUT, TracksPanel::on_zoom_out)
    EVT_BUTTON(ID_VIEW_ALL, TracksPanel::on_view_all)
    EVT_BUTTON(ID_VIEW_SEL, TracksPanel::on_view_sel)
    EVT_BUTTON(ID_DETACH, TracksPanel::on_detach)
    EVT_BUTTON(ID_CUT, TracksPanel::on_cut)
    EVT_BUTTON(ID_COPY, TracksPanel::on_copy)
    EVT_BUTTON(ID_PASTE, TracksPanel::on_paste)
    EVT_BUTTON(ID_SILENCE, TracksPanel::on_silence)
    EVT_BUTTON(ID_INSERT_SILENCE, TracksPanel::on_insert_silence)
wxEND_EVENT_TABLE()

TracksPanel::TracksPanel(wxWindow* parent, Engine& engine)
    : wxPanel(parent, wxID_ANY), m_engine(engine)
{
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    int btn_h = 28;

    m_zoom_in_btn = new wxButton(this, ID_ZOOM_IN, "Zoom In", wxDefaultPosition, wxSize(-1, btn_h));
    m_zoom_in_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_BUTTON, wxSize(16, 16)));
    m_zoom_out_btn = new wxButton(this, ID_ZOOM_OUT, "Zoom Out", wxDefaultPosition, wxSize(-1, btn_h));
    m_zoom_out_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_MINUS, wxART_BUTTON, wxSize(16, 16)));
    m_view_all_btn = new wxButton(this, ID_VIEW_ALL, "View All", wxDefaultPosition, wxSize(-1, btn_h));
    m_view_all_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_HOME, wxART_BUTTON, wxSize(16, 16)));
    m_view_sel_btn = new wxButton(this, ID_VIEW_SEL, "View Sel", wxDefaultPosition, wxSize(-1, btn_h));
    m_view_sel_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FIND, wxART_BUTTON, wxSize(16, 16)));

    m_cut_btn = new wxButton(this, ID_CUT, "Cut", wxDefaultPosition, wxSize(-1, btn_h));
    m_cut_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_CUT, wxART_BUTTON, wxSize(16, 16)));
    m_copy_btn = new wxButton(this, ID_COPY, "Copy", wxDefaultPosition, wxSize(-1, btn_h));
    m_copy_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_COPY, wxART_BUTTON, wxSize(16, 16)));
    m_paste_btn = new wxButton(this, ID_PASTE, "Paste", wxDefaultPosition, wxSize(-1, btn_h));
    m_paste_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_PASTE, wxART_BUTTON, wxSize(16, 16)));
    m_silence_btn = new wxButton(this, ID_SILENCE, "Silence", wxDefaultPosition, wxSize(-1, btn_h));
    m_silence_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_CROSS_MARK, wxART_BUTTON, wxSize(16, 16)));
    m_insert_btn = new wxButton(this, ID_INSERT_SILENCE, "Insert", wxDefaultPosition, wxSize(-1, btn_h));
    m_insert_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_BUTTON, wxSize(16, 16)));

    m_detach_btn = new wxButton(this, ID_DETACH, "", wxDefaultPosition, wxSize(btn_h, btn_h));
    m_detach_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FULL_SCREEN, wxART_BUTTON, wxSize(16, 16)));
    m_detach_btn->SetToolTip("Detach / re-attach tracks view");

    btn_sizer->Add(m_zoom_in_btn, 0, wxALL, 2);
    btn_sizer->Add(m_zoom_out_btn, 0, wxALL, 2);
    btn_sizer->Add(m_view_all_btn, 0, wxALL, 2);
    btn_sizer->Add(m_view_sel_btn, 0, wxALL, 2);
    btn_sizer->AddStretchSpacer();
    btn_sizer->Add(m_cut_btn, 0, wxALL, 2);
    btn_sizer->Add(m_copy_btn, 0, wxALL, 2);
    btn_sizer->Add(m_paste_btn, 0, wxALL, 2);
    btn_sizer->Add(m_silence_btn, 0, wxALL, 2);
    btn_sizer->Add(m_insert_btn, 0, wxALL, 2);
    btn_sizer->Add(m_detach_btn, 0, wxALL, 2);

    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, 2);

    m_tracks_view = new TracksView(this, wxID_ANY, m_engine);
    main_sizer->Add(m_tracks_view, 1, wxEXPAND | wxALL, 0);

    SetSizer(main_sizer);
}

void TracksPanel::update() {
    static int last_total_ticks = -1;
    static int last_track_count = -1;
    int current_total = m_tracks_view->get_total_ticks();
    int current_tracks = (int)m_engine.track_count();
    if (current_total != last_total_ticks || current_tracks != last_track_count) {
        m_tracks_view->update_view();
        last_total_ticks = current_total;
        last_track_count = current_tracks;
    }
    m_tracks_view->Refresh();
}

void TracksPanel::on_zoom_in(wxCommandEvent& event) { m_tracks_view->zoom_in(); }
void TracksPanel::on_zoom_out(wxCommandEvent& event) { m_tracks_view->zoom_out(); }
void TracksPanel::on_view_all(wxCommandEvent& event) { m_tracks_view->view_all(); }
void TracksPanel::on_view_sel(wxCommandEvent& event) { m_tracks_view->view_selection(); }
void TracksPanel::on_cut(wxCommandEvent& event) { m_tracks_view->do_cut(); }
void TracksPanel::on_copy(wxCommandEvent& event) { m_tracks_view->do_copy(); }
void TracksPanel::on_paste(wxCommandEvent& event) { m_tracks_view->do_paste(); }
void TracksPanel::on_silence(wxCommandEvent& event) { m_tracks_view->do_silence(); }
void TracksPanel::on_insert_silence(wxCommandEvent& event) { m_tracks_view->do_insert_silence(); }
void TracksPanel::on_detach(wxCommandEvent& event) {
    if (m_detached_frame) {
        return;
    }
    Hide();
    m_detached_frame = new DetachedFrame(this, "Tracks", GetParent(), m_tab_index);
    m_detached_frame->set_on_detach_callback([this]() { m_detached_frame = nullptr; });
}

wxBEGIN_EVENT_TABLE(TracksView, wxScrolledWindow)
    EVT_PAINT(TracksView::OnPaint)
    EVT_SIZE(TracksView::OnSize)
    EVT_LEFT_DOWN(TracksView::OnMouseDown)
    EVT_MOTION(TracksView::OnMouseDrag)
    EVT_LEFT_UP(TracksView::OnMouseUp)
    EVT_MOUSEWHEEL(TracksView::OnMouseWheel)
    EVT_RIGHT_DOWN(TracksView::OnMouseRightClick)
    EVT_KEY_DOWN(TracksView::OnKeyDown)
    EVT_CONTEXT_MENU(TracksView::OnContextMenu)
wxEND_EVENT_TABLE()

TracksView::TracksView(wxWindow* parent, wxWindowID id, Engine& engine)
    : wxScrolledWindow(parent, id), m_engine(engine)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_zoom = 10.0;
    SetScrollRate(1, 1);
    m_needs_initial_view_all = true;
}

void TracksView::OnSize(wxSizeEvent& event) {
    if (m_needs_initial_view_all && GetClientSize().x > 150) {
        view_all();
    }
    event.Skip();
}

int TracksView::get_total_ticks() {
    auto order = m_engine.order_list();
    int total_rows = 0;
    for (auto pat_idx : order) {
        total_rows += (int)m_engine.pattern(pat_idx).row_count();
    }
    return total_rows;
}

int TracksView::tick_to_x(int tick) { return (int)(tick * m_zoom); }
int TracksView::x_to_tick(int x) { return (m_zoom > 0) ? (int)((double)x / m_zoom + 0.5) : 0; }

int TracksView::resolve_content_track_index(int display_track_idx) const {
    if (display_track_idx < 0 || (size_t)display_track_idx >= m_engine.track_count()) {
        return -1;
    }

    auto has_sampler = [](const Track& track) {
        const Instrument* inst = track.instrument();
        return inst && inst->type() == InstrumentType::Sampler;
    };

    const Track& display_track = m_engine.track(display_track_idx);
    if (has_sampler(display_track)) {
        return display_track_idx;
    }
    return -1;
}

int TracksView::get_track_height(int track_idx) const {
    if (track_idx < 0 || (size_t)track_idx >= m_engine.track_count()) {
        return 80;
    }
    auto& track = m_engine.track(track_idx);
    return track.is_minimized() ? 20 : 80;
}

void TracksView::toggle_track_minimize(int track_idx) {
    if (track_idx < 0 || (size_t)track_idx >= m_engine.track_count()) {
        return;
    }
    auto& track = m_engine.track(track_idx);
    track.set_minimized(!track.is_minimized());
    update_view();
    Refresh();
}

bool TracksView::is_track_selected(int track_idx) const
{
    return m_selected_tracks.find(track_idx) != m_selected_tracks.end();
}

std::vector<int> TracksView::selected_tracks() const
{
    std::vector<int> tracks(m_selected_tracks.begin(), m_selected_tracks.end());
    if (tracks.empty() && m_selected_track >= 0 && (size_t)m_selected_track < m_engine.track_count()) {
        tracks.push_back(m_selected_track);
    }
    return tracks;
}

void TracksView::select_single_track(int track_idx)
{
    m_selected_tracks.clear();
    if (track_idx >= 0 && (size_t)track_idx < m_engine.track_count()) {
        m_selected_tracks.insert(track_idx);
        m_selected_track = track_idx;
        m_selection_anchor_track = track_idx;
    } else {
        m_selected_track = -1;
        m_selection_anchor_track = -1;
    }
}

void TracksView::select_track_range(int track_idx)
{
    if (track_idx < 0 || (size_t)track_idx >= m_engine.track_count()) {
        return;
    }

    const int anchor = (m_selection_anchor_track >= 0) ? m_selection_anchor_track : track_idx;
    m_selected_tracks.clear();
    for (int t = std::min(anchor, track_idx); t <= std::max(anchor, track_idx); ++t) {
        m_selected_tracks.insert(t);
    }
    m_selected_track = track_idx;
}

void TracksView::toggle_track_selection(int track_idx)
{
    if (track_idx < 0 || (size_t)track_idx >= m_engine.track_count()) {
        return;
    }

    auto it = m_selected_tracks.find(track_idx);
    if (it != m_selected_tracks.end()) {
        m_selected_tracks.erase(it);
        if (m_selected_track == track_idx) {
            m_selected_track = m_selected_tracks.empty() ? -1 : *m_selected_tracks.rbegin();
        }
        if (m_selection_anchor_track == track_idx) {
            m_selection_anchor_track = m_selected_track;
        }
    } else {
        m_selected_tracks.insert(track_idx);
        m_selected_track = track_idx;
        m_selection_anchor_track = track_idx;
    }
}

void TracksView::OnPaint(wxPaintEvent& event) {
    wxPaintDC dc(this);
    PrepareDC(dc);
    draw(dc);
}

void TracksView::draw(wxDC& dc) {
    dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_bg)));
    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_bg)));
    wxSize virtual_size = GetVirtualSize();
    dc.DrawRectangle(0, 0, virtual_size.GetWidth(), virtual_size.GetHeight());

    int track_h = 80;
    int header_w = kTrackHeaderWidth;
    int num_tracks = (int)m_engine.track_count();
    auto order = m_engine.order_list();
    uint32_t lpb = m_engine.lpb();

    // Draw Time Scale (Header)
    dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
    dc.DrawRectangle(header_w, 0, virtual_size.GetWidth() - header_w, 20);

    dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_text));
    wxFont header_font(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    dc.SetFont(header_font);

    int total_rows = 0;
    size_t current_pos = m_engine.current_order_pos();
    int play_tick = 0;

    for (size_t i = 0; i < order.size(); ++i) {
        auto& pat = m_engine.pattern(order[i]);
        int pat_rows = (int)pat.row_count();
        int px = header_w + tick_to_x(total_rows);

        // Calculate playhead position
        if (i < current_pos) {
            play_tick += pat_rows;
        } else if (i == current_pos) {
            play_tick += (int)m_engine.current_row();
        }

        // Pattern boundary
        dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
        dc.DrawLine(px, 0, px, virtual_size.GetHeight());

        // Pattern label
        wxString buf;
        buf.Printf("POS %zu (PAT %zu)", i, order[i]);
        dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_text));
        dc.DrawText(buf, px + 5, 2);

        // Beat markers
        if (lpb > 0) {
            for (int r = 0; r < pat_rows; r += lpb) {
                int bx = px + tick_to_x(r);
                dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
                dc.DrawLine(bx, 15, bx, 25);
                if (r % (lpb * 4) == 0) {
                    wxString bbuf;
                    bbuf.Printf("%d", r / lpb);
                    dc.DrawText(bbuf, bx + 2, 28);
                }
            }
        }

        total_rows += pat_rows;
    }

    // Draw Tracks
    int cur_y = 30;
    double bpm = m_engine.tempo();
    double sample_rate = m_engine.sample_rate();
    double samples_per_beat = (sample_rate * 60.0) / bpm;
    double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;

    for (int t = 0; t < num_tracks; ++t) {
        auto& track_obj = m_engine.track(t);
        const int content_track_idx = resolve_content_track_index(t);
        Track* content_track = content_track_idx >= 0 ? &m_engine.track(content_track_idx) : nullptr;
        Instrument* content_inst = content_track ? content_track->instrument() : track_obj.instrument();
        const bool is_audio_take_track =
            !m_engine.is_tempo_track((size_t)t) &&
            (track_obj.kind() == TrackKind::Audio || track_obj.kind() == TrackKind::Pilot) &&
            content_inst &&
            content_inst->type() == InstrumentType::Sampler;
        int track_h_actual = get_track_height(t);
        int ty = cur_y;
        cur_y += track_h_actual;

        // Track Header
        wxColour header_bg = ThemeManager::toWxColour(m_engine.m_bg_color);
        const wxColour sel_col = ThemeManager::toWxColour(m_engine.m_selection_color);
        if (is_track_selected(t)) {
            header_bg = wxColour(sel_col.Red(), sel_col.Green(), sel_col.Blue(), 56);
        }
        dc.SetBrush(wxBrush(header_bg));
        dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_fg_color)));
        dc.DrawRectangle(0, ty, header_w, track_h_actual - 1);
        if (is_track_selected(t)) {
            dc.SetPen(wxPen(sel_col, 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(0, ty, header_w, track_h_actual - 1);
        }

        // Minimize button (- for expanded, + for minimized)
        const wxRect min_rect = control_rect(ty, track_h_actual, HeaderControl::Minimize, false);
        
        dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
        dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_fg_color)));
        dc.DrawRectangle(min_rect);
        
        dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_fg_color));
        wxFont btn_font(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
        dc.SetFont(btn_font);
        wxString btn_text = track_obj.is_minimized() ? "+" : "-";
        dc.DrawText(btn_text, min_rect.x + 6, min_rect.y + 2);

        wxFont bold_font(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
        dc.SetFont(bold_font);
        dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_fg_color));
        wxString name = track_obj.name().substr(0, 18);
        dc.DrawText(name, 30, ty + 5);

        // Instrument Info (only shown when not minimized)
        if (!track_obj.is_minimized()) {
            wxFont normal_font(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
            dc.SetFont(normal_font);
            dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_text));
            const char* kind_str = "";
            switch (track_obj.kind()) {
                case TrackKind::Note:  kind_str = "[Note]"; break;
                case TrackKind::Audio: kind_str = "[Audio]"; break;
                case TrackKind::Pilot: kind_str = "[Pilot]"; break;
            }

            wxString info_line = kind_str;
            if (track_obj.kind() == TrackKind::Audio && content_track_idx >= 0 && content_track_idx != t) {
                info_line.Printf("[Audio -> TRK %d]", content_track_idx);
            }

            if (content_inst) {
                wxString inst_name = content_inst->name().substr(0, 20);
                dc.DrawText(inst_name, 30, ty + 20);
            }

            wxString status_line = wxString::Format("%s  Vol %d%%  Pan %s",
                                                    info_line,
                                                    (int)std::round(track_obj.volume() * 100.0f),
                                                    pan_status_label(track_obj.get_pan()));
            if (track_obj.muted()) status_line += "  M";
            if (track_obj.solo())  status_line += "  S";
            if (has_record_arm(m_engine, t) && m_engine.is_audio_track_armed((size_t)t)) {
                status_line += m_engine.is_recording_audio_tracks() ? "  REC*" : "  REC";
            }
            dc.DrawText(status_line, 30, ty + 36);

            if (has_track_header_controls(m_engine, t)) {
                const bool track_has_record = has_record_arm(m_engine, t);
                const wxColour fg = ThemeManager::toWxColour(m_engine.m_fg_color);
                const wxColour base = ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight);
                const wxColour active = ThemeManager::toWxColour(m_engine.m_tracker_cursor);
                wxFont control_font(8, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
                dc.SetFont(control_font);
                draw_header_button(dc, control_rect(ty, track_h_actual, HeaderControl::Mute, track_has_record),
                                   "M", fg, track_obj.muted() ? active : base, track_obj.muted());
                draw_header_button(dc, control_rect(ty, track_h_actual, HeaderControl::Solo, track_has_record),
                                   "S", fg, track_obj.solo() ? active : base, track_obj.solo());
                if (track_has_record) {
                    const bool armed = m_engine.is_audio_track_armed((size_t)t);
                    draw_header_button(dc, control_rect(ty, track_h_actual, HeaderControl::Record, track_has_record),
                                        "R", fg, armed ? active : base, armed);
                }
                draw_header_button(dc, control_rect(ty, track_h_actual, HeaderControl::VolDown, track_has_record),
                                   "-", fg, base);
                draw_header_button(dc, control_rect(ty, track_h_actual, HeaderControl::VolUp, track_has_record),
                                   "+", fg, base);
                draw_header_button(dc, control_rect(ty, track_h_actual, HeaderControl::PanLeft, track_has_record),
                                   "<", fg, base);
                draw_header_button(dc, control_rect(ty, track_h_actual, HeaderControl::PanRight, track_has_record),
                                   ">", fg, base);
            }
        }

        // Track Content Area (only shown when not minimized)
        if (!track_obj.is_minimized()) {
            if (m_engine.is_tempo_track((size_t)t)) {
                int rows_done = 0;
                for (auto pat_idx : order) {
                    auto& pat = m_engine.pattern(pat_idx);
                    const int pat_rows = (int)pat.row_count();
                    const int px = header_w + tick_to_x(rows_done);

                    if (lpb > 0) {
                        for (int r = 0; r < pat_rows; r += lpb) {
                            const int click_x = px + tick_to_x(r);
                            const bool accented = (r % (lpb * 4)) == 0;
                            wxColour guide = ThemeManager::toWxColour(
                                accented ? m_engine.m_tracker_cursor : m_engine.m_tracker_lpb_highlight);
                            guide.Set(guide.Red(), guide.Green(), guide.Blue(), accented ? 200 : 120);
                            dc.SetPen(wxPen(guide, accented ? 2 : 1));
                            dc.DrawLine(click_x, ty + 5, click_x, ty + track_h_actual - 6);
                        }
                    }

                    rows_done += pat_rows;
                }
                continue;
            }

            if (is_audio_take_track) {
                auto* sampler = static_cast<SampleInstrument*>(content_inst);
                const size_t sample_idx =
                    track_obj.audio_sample_index() > 0 ? (size_t)(track_obj.audio_sample_index() - 1) : 0;
                if (sample_idx < sampler->sample_count()) {
                    const auto& sample = sampler->get_sample(sample_idx);
                    if (sample.data) {
                        int nw = tick_to_x((int)std::ceil((double)sample.data->left.size() / samples_per_row));
                        if (nw < 2) nw = 2;
                        const size_t samples_to_draw =
                            std::min(sample.data->left.size(), (size_t)((double)nw / m_zoom * samples_per_row));
                        dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
                        dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
                        dc.DrawRectangle(header_w, ty + 5, nw, track_h_actual - 10);
                        draw_waveform_helper(dc, header_w, ty + 5, nw, track_h_actual - 10,
                                             *sample.data,
                                             ThemeManager::toWxColour(m_engine.m_tracker_note),
                                             samples_to_draw);
                    }
                }
                continue;
            }

            const int pattern_track_idx = content_track_idx >= 0 ? content_track_idx : t;
            int rows_done = 0;
            for (auto pat_idx : order) {
                auto& pat = m_engine.pattern(pat_idx);
                int pat_rows = (int)pat.row_count();
                int px = header_w + tick_to_x(rows_done);

                size_t num_cols = pat.column_count(pattern_track_idx);
                for (int r = 0; r < pat_rows; ++r) {
                    for (size_t c = 0; c < num_cols; ++c) {
                        const auto& ev = pat.event(pattern_track_idx, r, c);
                        if (ev.note != 255) {
                            int nx = px + tick_to_x(r);

                            if (ev.note == 254) { // Note Off
                                dc.SetPen(wxPen(wxColour(255, 100, 100)));
                                dc.DrawLine(nx, ty + 5, nx, ty + track_h_actual - 5);
                            } else {
                                // Find note length
                                int note_len = 1;
                                bool found_end = false;
                                for (int r2 = r + 1; r2 < pat_rows; ++r2) {
                                    if (pat.event(pattern_track_idx, r2, c).note != 255) {
                                        note_len = r2 - r;
                                        found_end = true;
                                        break;
                                    }
                                }
                                if (!found_end) note_len = pat_rows - r;

                                if (content_inst && content_inst->type() == InstrumentType::Sampler) {
                                    SampleInstrument* sampler = static_cast<SampleInstrument*>(content_inst);
                                    size_t s_idx = (ev.sample_idx > 0) ? (ev.sample_idx - 1) : sampler->selected_sample();
                                    if (s_idx < sampler->sample_count()) {
                                        auto& sample = sampler->get_sample(s_idx);
                                        if (sample.data) {
                                            double sample_duration_rows = (double)sample.data->left.size() / samples_per_row;
                                            int nw_limit = tick_to_x(note_len);
                                            int nw_sample = tick_to_x(sample_duration_rows);
                                            int nw = std::min(nw_limit, nw_sample);
                                            if (nw < 2) nw = 2;

                                            size_t samples_to_draw = (size_t)((double)nw / m_zoom * samples_per_row);
                                            samples_to_draw = std::min(samples_to_draw, sample.data->left.size());

                                            // Detect overlaps
                                            auto overlaps = detect_overlaps(sampler);
                                            bool is_overlapping = (s_idx < overlaps.size()) && overlaps[s_idx].is_overlapping;
                                            
                                            // Draw sample background
                                            if (is_overlapping) {
                                                dc.SetBrush(wxBrush(wxColour(255, 200, 0, 128)));
                                                dc.SetPen(wxPen(wxColour(255, 150, 0)));
                                            } else {
                                                dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
                                                dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
                                            }
                                            dc.DrawRectangle(nx, ty + 5, nw, track_h_actual - 10);
                                            
                                            // Draw waveform
                                            draw_waveform_helper(dc, nx, ty + 5, nw, track_h_actual - 10, *sample.data, ThemeManager::toWxColour(m_engine.m_tracker_note), samples_to_draw);
                                            
                                            // Draw overlap indicator if needed
                                            if (is_overlapping) {
                                                dc.SetPen(wxPen(wxColour(255, 100, 0), 2));
                                                dc.DrawRectangle(nx, ty + 5, nw, track_h_actual - 10);
                                            }
                                        }
                                    }
                                } else {
                                    int nw = tick_to_x(note_len);
                                    if (nw < 2) nw = 2;
                                    // Just a block
                                    dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_note)));
                                    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_note)));
                                    dc.DrawRectangle(nx, ty + 5, nw, track_h_actual - 10);
                                    
                                    dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_bg));
                                    wxFont note_font(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
                                    dc.SetFont(note_font);
                                    const char* notes[] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
                                    wxString nbuf;
                                    nbuf.Printf("%s%d", notes[ev.note % 12], ev.note / 12);
                                    if (nw > 20) dc.DrawText(nbuf, nx + 2, ty + 15);
                                }
                            }
                        }
                    }
                }
                rows_done += pat_rows;
            }
        }

        // Horizontal line between tracks
        dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
        dc.DrawLine(header_w, ty + track_h_actual - 1, header_w + tick_to_x(total_rows), ty + track_h_actual - 1);
    }

    // Current Playback Marker
    if (m_engine.transport_state() != TransportState::Stopped) {
        int play_x = header_w + tick_to_x(play_tick);
        dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_cursor)));
        dc.DrawLine(play_x, 0, play_x, virtual_size.GetHeight());
    }

    // Selection - highlight on selected track AND guide on all tracks
    if (m_sel_start_tick != -1 && m_sel_end_tick != -1) {
        int s1 = std::min(m_sel_start_tick, m_sel_end_tick);
        int s2 = std::max(m_sel_start_tick, m_sel_end_tick);
        if (s1 == s2) s2 = s1 + 1;  // Minimum 1 tick width for visibility
        
        int sx1 = header_w + tick_to_x(s1);
        int sx2 = header_w + tick_to_x(s2);
        if (sx2 == sx1) sx2 = sx1 + 2;  // Minimum 2 pixels for visibility
        
        // Draw selection guide across all tracks
        wxColour sel_col = ThemeManager::toWxColour(m_engine.m_selection_color);
        dc.SetBrush(wxBrush(wxColour(sel_col.Red(), sel_col.Green(), sel_col.Blue(), 32)));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(sx1, 0, sx2 - sx1, virtual_size.GetHeight());

        // Draw selection highlight on the selected track if it's a sampler
        for (int selected_track_idx : selected_tracks()) {
            if (selected_track_idx < 0 || selected_track_idx >= num_tracks) {
                continue;
            }
            const int content_track_idx = resolve_content_track_index(selected_track_idx);
            auto& sel_track = m_engine.track(content_track_idx >= 0 ? content_track_idx : selected_track_idx);
            auto inst = sel_track.instrument();
            if (inst && inst->type() == InstrumentType::Sampler) {
                int cur_y_pos = 30;
                for (int t = 0; t < selected_track_idx; ++t) {
                    cur_y_pos += get_track_height(t);
                }

                const int sel_track_h = get_track_height(selected_track_idx);
                dc.SetBrush(wxBrush(wxColour(sel_col.Red(), sel_col.Green(), sel_col.Blue(), 64)));
                dc.SetPen(wxPen(sel_col, 2));
                dc.DrawRectangle(sx1, cur_y_pos, sx2 - sx1, sel_track_h);
            }
        }
        
        // Border lines across all tracks
        dc.SetPen(wxPen(wxColour(sel_col.Red(), sel_col.Green(), sel_col.Blue(), 128), 1, wxPENSTYLE_DOT));
        dc.DrawLine(sx1, 0, sx1, virtual_size.GetHeight());
        dc.DrawLine(sx2, 0, sx2, virtual_size.GetHeight());
    }
}

void TracksView::OnMouseDown(wxMouseEvent& event) {
    int x, y;
    CalcUnscrolledPosition(event.GetX(), event.GetY(), &x, &y);
    int header_w = kTrackHeaderWidth;
    const bool additive = event.ControlDown() || event.CmdDown();
    const bool range_select = event.ShiftDown();
    
    // Determine which track was clicked
    int num_tracks = (int)m_engine.track_count();
    int cur_y = 30;
    int clicked_track = -1;
    
    for (int t = 0; t < num_tracks; ++t) {
        auto& track_obj = m_engine.track(t);
        int track_h = get_track_height(t);
        int ty = cur_y;
        
        if (y >= ty && y < ty + track_h) {
            clicked_track = t;
            
            if (control_rect(ty, track_h, HeaderControl::Minimize, false).Contains(x, y)) {
                toggle_track_minimize(t);
                return;
            }

            if (!track_obj.is_minimized() && has_track_header_controls(m_engine, t) && x < header_w) {
                const bool track_has_record = has_record_arm(m_engine, t);
                auto& track = m_engine.track((size_t)t);
                if (control_rect(ty, track_h, HeaderControl::Mute, track_has_record).Contains(x, y)) {
                    track.set_mute(!track.muted());
                    m_engine.mark_dirty();
                    Refresh();
                    return;
                }
                if (control_rect(ty, track_h, HeaderControl::Solo, track_has_record).Contains(x, y)) {
                    track.set_solo(!track.solo());
                    m_engine.mark_dirty();
                    Refresh();
                    return;
                }
                if (track_has_record && control_rect(ty, track_h, HeaderControl::Record, track_has_record).Contains(x, y)) {
                    m_engine.set_audio_track_armed((size_t)t, !m_engine.is_audio_track_armed((size_t)t));
                    Refresh();
                    return;
                }
                if (control_rect(ty, track_h, HeaderControl::VolDown, track_has_record).Contains(x, y)) {
                    track.set_volume(std::max(0.0f, track.volume() - 0.05f));
                    m_engine.mark_dirty();
                    Refresh();
                    return;
                }
                if (control_rect(ty, track_h, HeaderControl::VolUp, track_has_record).Contains(x, y)) {
                    track.set_volume(std::min(1.0f, track.volume() + 0.05f));
                    m_engine.mark_dirty();
                    Refresh();
                    return;
                }
                if (control_rect(ty, track_h, HeaderControl::PanLeft, track_has_record).Contains(x, y)) {
                    track.set_pan(track.get_pan() - 0.1f);
                    m_engine.mark_dirty();
                    Refresh();
                    return;
                }
                if (control_rect(ty, track_h, HeaderControl::PanRight, track_has_record).Contains(x, y)) {
                    track.set_pan(track.get_pan() + 0.1f);
                    m_engine.mark_dirty();
                    Refresh();
                    return;
                }
            }
            break;
        }
        cur_y += track_h;
    }
    
    if (clicked_track >= 0) {
        if (range_select) {
            select_track_range(clicked_track);
        } else if (additive) {
            toggle_track_selection(clicked_track);
        } else {
            select_single_track(clicked_track);
        }

        if (x > header_w && is_track_selected(clicked_track)) {
            m_is_selecting = true;
            m_sel_start_tick = x_to_tick(x - header_w);
            m_sel_end_tick = m_sel_start_tick;
        } else {
            m_is_selecting = false;
        }
        Refresh();
    }
}

void TracksView::OnMouseDrag(wxMouseEvent& event) {
    if (m_is_selecting) {
        int x, y;
        CalcUnscrolledPosition(event.GetX(), event.GetY(), &x, &y);
        int header_w = kTrackHeaderWidth;
        m_sel_end_tick = x_to_tick(x - header_w);
        Refresh();
    }
}

void TracksView::OnMouseUp(wxMouseEvent& event) {
    m_is_selecting = false;
}

void TracksView::OnMouseWheel(wxMouseEvent& event) {
    if (event.ControlDown()) {
        if (event.GetWheelRotation() < 0) zoom_out();
        else zoom_in();
    } else {
        event.Skip();
    }
}

void TracksView::zoom_in() { m_zoom *= 1.5; update_view(); }
void TracksView::zoom_out() { m_zoom /= 1.5; if (m_zoom < 0.1) m_zoom = 0.1; update_view(); }
void TracksView::view_all() {
    int total = get_total_ticks();
    if (total > 0) {
        wxSize size = GetClientSize();
        if (size.GetWidth() <= 150) size = GetParent()->GetClientSize();
        if (size.GetWidth() > 150) {
            m_zoom = (double)(size.GetWidth() - 140) / total;
            if (m_zoom < 0.1) m_zoom = 0.1;
            m_needs_initial_view_all = false;
        }
    }
    update_view();
}
void TracksView::view_selection() {
    if (m_sel_start_tick != -1 && m_sel_end_tick != -1) {
        int diff = std::abs(m_sel_end_tick - m_sel_start_tick);
        if (diff > 0) {
            wxSize size = GetParent()->GetClientSize();
            m_zoom = (double)(size.GetWidth() - 140) / diff;
        }
    }
    update_view();
}

void TracksView::update_view() {
    int num_tracks = (int)m_engine.track_count();
    int total_h = 30;
    for (int t = 0; t < num_tracks; ++t) {
        total_h += get_track_height(t);
    }
    int total_w = kTrackHeaderWidth + tick_to_x(get_total_ticks()) + 50;
    total_h += 50;
    SetVirtualSize(total_w, total_h);
    Refresh();
}

void TracksView::OnMouseRightClick(wxMouseEvent& event) {
    // Store position for potential operations
    // The actual context menu is shown via wxContextMenuEvent
    event.Skip();
}

void TracksView::OnContextMenu(wxContextMenuEvent& event) {
    wxMenu menu;
    menu.Append(ID_MENU_CUT, "Cut\tX", "Cut selection to clipboard");
    menu.Append(ID_MENU_COPY, "Copy\tC", "Copy selection to clipboard");
    menu.Append(ID_MENU_PASTE, "Paste\tV", "Paste from clipboard");
    menu.AppendSeparator();
    menu.Append(ID_MENU_SILENCE, "Silence\tS", "Silence selection");
    menu.Append(ID_MENU_INSERT_SILENCE, "Insert Gap\tI", "Insert gap at cursor");

    bool can_retrigger = false;
    if (m_selected_track >= 0 && (size_t)m_selected_track < m_engine.track_count()) {
        const Track& track = m_engine.track((size_t)m_selected_track);
        const int linked_track = track.linked_track();
        can_retrigger = track.kind() == TrackKind::Audio &&
                        track.instrument() &&
                        track.instrument()->type() == InstrumentType::Sampler &&
                        linked_track >= 0 &&
                        (size_t)linked_track < m_engine.track_count() &&
                        m_engine.track((size_t)linked_track).kind() == TrackKind::Note;
    }
    if (can_retrigger) {
        menu.AppendSeparator();
        menu.Append(ID_MENU_RETRIGGER_SELECTION, "Retrigger Stretch Selection", "Stretch the selected audio range to the linked note track");
        menu.Append(ID_MENU_RETRIGGER_WHOLE, "Retrigger Stretch Whole Track", "Stretch the whole audio track to the linked note track");
    }
    menu.AppendSeparator();
    menu.Append(ID_MENU_UNDO, "Undo\tCtrl+Z", "Undo last operation");
    menu.Append(ID_MENU_REDO, "Redo\tCtrl+Y", "Redo last operation");
    
    // Bind menu events to operation handlers
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { do_cut(); }, ID_MENU_CUT);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { do_copy(); }, ID_MENU_COPY);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { do_paste(); }, ID_MENU_PASTE);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { do_silence(); }, ID_MENU_SILENCE);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { do_insert_silence(); }, ID_MENU_INSERT_SILENCE);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { do_retrigger_stretch_selection(); }, ID_MENU_RETRIGGER_SELECTION);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { do_retrigger_stretch_whole_track(); }, ID_MENU_RETRIGGER_WHOLE);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { do_undo(); }, ID_MENU_UNDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { do_redo(); }, ID_MENU_REDO);
    
    PopupMenu(&menu, event.GetPosition());
}

void TracksView::OnKeyDown(wxKeyEvent& event) {
    int key = event.GetKeyCode();
    
    if (event.ControlDown() || event.CmdDown()) {
        if (key == 'Z' || key == 'z') {
            do_undo();
            return;
        } else if (key == 'Y' || key == 'y') {
            do_redo();
            return;
        }
    }
    
    switch (key) {
        case 'X':
        case 'x':
            do_cut();
            break;
        case 'C':
        case 'c':
            do_copy();
            break;
        case 'V':
        case 'v':
            do_paste();
            break;
        case 'S':
        case 's':
            do_silence();
            break;
        case 'I':
        case 'i':
            do_insert_silence();
            break;
        default:
            event.Skip();
            return;
    }
}

void TracksView::do_cut() {
    if (m_sel_start_tick == -1 || m_sel_end_tick == -1 || m_selected_track < 0) {
        return;
    }

    const int start_row = std::min(m_sel_start_tick, m_sel_end_tick);
    int end_row = std::max(m_sel_start_tick, m_sel_end_tick);
    if (start_row == end_row) end_row = start_row + 1;

    const uint32_t lpb = m_engine.lpb();
    const double bpm = m_engine.tempo();
    const double sample_rate = m_engine.sample_rate();
    const double samples_per_beat = (sample_rate * 60.0) / bpm;
    const double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;
    const size_t start_sample_global = (size_t)(start_row * samples_per_row);
    const size_t end_sample_global = (size_t)(end_row * samples_per_row);

    std::vector<TrackClipboard::AudioEntry> clips;
    bool any_clip = false;
    bool changed = false;
    for (int track_idx : selected_tracks()) {
        TrackClipboard::AudioEntry clip;
        if (extract_track_clip(m_engine, track_idx, start_sample_global, end_sample_global, clip)) {
            any_clip = true;
        }
        clips.push_back(std::move(clip));
        changed = cut_track_selection(m_engine, track_idx, start_sample_global, end_sample_global) || changed;
    }

    if (any_clip) {
        m_engine.track_clipboard().set_audio_group(clips);
    }
    if (changed) {
        update_view();
        Refresh();
    }
}

void TracksView::do_copy() {
    if (m_sel_start_tick == -1 || m_sel_end_tick == -1 || m_selected_track < 0) {
        return;
    }

    const int start_row = std::min(m_sel_start_tick, m_sel_end_tick);
    int end_row = std::max(m_sel_start_tick, m_sel_end_tick);
    if (start_row == end_row) end_row = start_row + 1;

    const uint32_t lpb = m_engine.lpb();
    const double bpm = m_engine.tempo();
    const double sample_rate = m_engine.sample_rate();
    const double samples_per_beat = (sample_rate * 60.0) / bpm;
    const double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;
    const size_t start_sample_global = (size_t)(start_row * samples_per_row);
    const size_t end_sample_global = (size_t)(end_row * samples_per_row);

    std::vector<TrackClipboard::AudioEntry> clips;
    bool any_clip = false;
    for (int track_idx : selected_tracks()) {
        TrackClipboard::AudioEntry clip;
        if (extract_track_clip(m_engine, track_idx, start_sample_global, end_sample_global, clip)) {
            any_clip = true;
        }
        clips.push_back(std::move(clip));
    }

    if (any_clip) {
        m_engine.track_clipboard().set_audio_group(clips);
    }
}

void TracksView::do_paste() {
    if (m_sel_start_tick == -1 || m_selected_track < 0) {
        return;
    }

    std::vector<TrackClipboard::AudioEntry> clips;
    if (!m_engine.track_clipboard().get_audio_group(clips) || clips.empty()) {
        return;
    }

    const int cursor_row = m_sel_start_tick;
    const uint32_t lpb = m_engine.lpb();
    const double bpm = m_engine.tempo();
    const double sample_rate = m_engine.sample_rate();
    const double samples_per_beat = (sample_rate * 60.0) / bpm;
    const double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;
    const size_t paste_pos_global = (size_t)(cursor_row * samples_per_row);

    const auto tracks = selected_tracks();
    bool changed = false;
    for (size_t i = 0; i < tracks.size(); ++i) {
        const size_t clip_idx = clips.size() == 1 ? 0 : i;
        if (clip_idx >= clips.size()) {
            break;
        }
        changed = paste_clip_into_track(m_engine, tracks[i], paste_pos_global, clips[clip_idx]) || changed;
    }

    if (changed) {
        update_view();
        Refresh();
    }
}

void TracksView::do_silence() {
    if (m_sel_start_tick == -1 || m_sel_end_tick == -1 || m_selected_track < 0) {
        return;
    }

    const int start_row = std::min(m_sel_start_tick, m_sel_end_tick);
    int end_row = std::max(m_sel_start_tick, m_sel_end_tick);
    if (start_row == end_row) end_row = start_row + 1;

    const uint32_t lpb = m_engine.lpb();
    const double bpm = m_engine.tempo();
    const double sample_rate = m_engine.sample_rate();
    const double samples_per_beat = (sample_rate * 60.0) / bpm;
    const double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;
    const size_t start_sample_global = (size_t)(start_row * samples_per_row);
    const size_t end_sample_global = (size_t)(end_row * samples_per_row);

    bool changed = false;
    for (int track_idx : selected_tracks()) {
        changed = silence_track_selection(m_engine, track_idx, start_sample_global, end_sample_global) || changed;
    }

    if (changed) {
        Refresh();
    }
}

void TracksView::do_insert_silence() {
    if (m_sel_start_tick == -1 || m_selected_track < 0) {
        return;
    }

    const uint32_t lpb = m_engine.lpb();
    const double bpm = m_engine.tempo();
    const double sample_rate = m_engine.sample_rate();
    const double samples_per_beat = (sample_rate * 60.0) / bpm;
    const double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;

    int cursor_row = m_sel_start_tick;
    size_t insert_pos_global = (size_t)(cursor_row * samples_per_row);
    size_t insert_duration = (size_t)samples_per_row;
    if (m_sel_end_tick != -1 && m_sel_end_tick != m_sel_start_tick) {
        const int start_row = std::min(m_sel_start_tick, m_sel_end_tick);
        const int end_row = std::max(m_sel_start_tick, m_sel_end_tick);
        insert_duration = (size_t)((end_row - start_row) * samples_per_row);
        insert_pos_global = (size_t)(start_row * samples_per_row);
    }

    bool changed = false;
    for (int track_idx : selected_tracks()) {
        changed = insert_silence_into_track(m_engine, track_idx, insert_pos_global, insert_duration) || changed;
    }

    if (changed) {
        update_view();
        Refresh();
    }
}

void TracksView::do_retrigger_stretch_selection() {
    if (m_selected_track < 0 || (size_t)m_selected_track >= m_engine.track_count()) {
        return;
    }
    if (m_sel_start_tick == -1 || m_sel_end_tick == -1 || m_sel_start_tick == m_sel_end_tick) {
        wxMessageBox("Select an audio range first.", "Retrigger Stretch", wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (!m_engine.retrigger_stretch_audio_track((size_t)m_selected_track,
                                                true,
                                                (size_t)std::min(m_sel_start_tick, m_sel_end_tick),
                                                (size_t)std::max(m_sel_start_tick, m_sel_end_tick))) {
        wxMessageBox("Retrigger stretch needs a linked note track with quantized notes and detectable audio hits in the selected range.",
                     "Retrigger Stretch",
                     wxOK | wxICON_WARNING,
                     this);
        return;
    }

    update_view();
    Refresh();
}

void TracksView::do_retrigger_stretch_whole_track() {
    if (m_selected_track < 0 || (size_t)m_selected_track >= m_engine.track_count()) {
        return;
    }

    if (!m_engine.retrigger_stretch_audio_track((size_t)m_selected_track)) {
        wxMessageBox("Retrigger stretch needs a linked note track with quantized notes and detectable audio hits on this track.",
                     "Retrigger Stretch",
                     wxOK | wxICON_WARNING,
                     this);
        return;
    }

    update_view();
    Refresh();
}

void TracksView::do_undo() {
    m_engine.undo_stack().undo();
    Refresh();
}

void TracksView::do_redo() {
    m_engine.undo_stack().redo();
    Refresh();
}

std::vector<TracksView::AudioRegion> TracksView::detect_overlaps(const SampleInstrument* sampler) {
    std::vector<AudioRegion> regions;
    
    if (!sampler || sampler->sample_count() == 0) {
        return regions;
    }
    
    for (size_t i = 0; i < sampler->sample_count(); ++i) {
        auto& sample = sampler->get_sample(i);
        if (sample.data && !sample.data->left.empty()) {
            AudioRegion region;
            region.start_sample = 0;
            region.end_sample = sample.data->left.size();
            region.is_overlapping = false;
            regions.push_back(region);
        }
    }
    
    for (size_t i = 0; i < regions.size(); ++i) {
        for (size_t j = i + 1; j < regions.size(); ++j) {
            if (regions[i].start_sample < regions[j].end_sample &&
                regions[j].start_sample < regions[i].end_sample) {
                regions[i].is_overlapping = true;
                regions[j].is_overlapping = true;
            }
        }
    }
    
    return regions;
}

} // namespace tpanar_ns
