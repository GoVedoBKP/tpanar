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
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
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

constexpr int kTrackHeaderWidth = 320;
constexpr int kMinimizeButtonX = 5;
constexpr int kMaxWaveCacheWidth = 16384; // bitmaps wider than this are not cached
constexpr int kMinimizeButtonW = 20;
constexpr int kControlButtonW = 28;
constexpr int kControlButtonH = 20;
constexpr int kControlButtonGapX = 6;
constexpr int kControlButtonGapY = 4;
constexpr int kControlRightMargin = 10;
constexpr int kHeaderTextX = 30;
constexpr int kHeaderTextRightPadding = 10;
constexpr int kTimelineHeaderHeight = 20;
constexpr int kPositionRulerHeight = 18;
constexpr int kTracksContentTop = kTimelineHeaderHeight + kPositionRulerHeight;

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
        return wxRect(kMinimizeButtonX, track_y + 6, kMinimizeButtonW, 18);
    }

    int row = 0;
    int col = 0;
    int row_cols = 0;
    switch (control) {
        case HeaderControl::Mute:
            row = 0; col = 0; row_cols = has_record ? 3 : 2; break;
        case HeaderControl::Solo:
            row = 0; col = 1; row_cols = has_record ? 3 : 2; break;
        case HeaderControl::Record:
            row = 0; col = 2; row_cols = 3; break;
        case HeaderControl::VolDown:
            row = 1; col = 0; row_cols = 4; break;
        case HeaderControl::VolUp:
            row = 1; col = 1; row_cols = 4; break;
        case HeaderControl::PanLeft:
            row = 1; col = 2; row_cols = 4; break;
        case HeaderControl::PanRight:
            row = 1; col = 3; row_cols = 4; break;
        case HeaderControl::Minimize: break;
    }

    const int row_width = row_cols * kControlButtonW + std::max(0, row_cols - 1) * kControlButtonGapX;
    const int x = kTrackHeaderWidth - kControlRightMargin - row_width + col * (kControlButtonW + kControlButtonGapX);
    const int row_y = track_y + 8 + row * (kControlButtonH + kControlButtonGapY);
    return wxRect(x, row_y, kControlButtonW, kControlButtonH);
}

wxString pan_status_label(float pan)
{
    if (std::fabs(pan) < 0.05f) return "C";
    const int amount = (int)std::round(std::fabs(pan) * 100.0f);
    return wxString::Format("%c%d", pan < 0.0f ? 'L' : 'R', amount);
}

wxString fit_header_text(wxDC& dc, const wxString& text, int max_width)
{
    if (max_width <= 0 || text.empty()) return wxString();
    if (dc.GetTextExtent(text).GetWidth() <= max_width) return text;

    static const wxString ellipsis = "...";
    wxString fitted = text;
    while (!fitted.empty() &&
           dc.GetTextExtent(fitted + ellipsis).GetWidth() > max_width) {
        fitted.RemoveLast();
    }
    return fitted.empty() ? ellipsis : fitted + ellipsis;
}

wxString track_kind_label(TrackKind kind)
{
    switch (kind) {
        case TrackKind::Note:  return "Note";
        case TrackKind::Audio: return "Audio";
        case TrackKind::Pilot: return "Pilot";
    }
    return "Track";
}

double sample_frames_per_row(const Engine& engine, uint32_t sample_rate)
{
    const double engine_samples_per_row =
        (engine.lpb() > 0 && engine.tempo() > 0.0)
            ? (((double)engine.sample_rate() * 60.0) / engine.tempo()) / (double)engine.lpb()
            : (double)engine.sample_rate();
    const double resolved_sample_rate = sample_rate > 0 ? (double)sample_rate : (double)engine.sample_rate();
    return engine_samples_per_row * (resolved_sample_rate / (double)engine.sample_rate());
}

size_t sample_frame_count(const SampleData& sample)
{
    return std::max(sample.left.size(), sample.right.size());
}

int sample_rows_for_display(const Engine& engine, const SampleData& sample)
{
    const double frames_per_row = sample_frames_per_row(engine, sample.sample_rate);
    if (frames_per_row <= 0.0) return 0;
    return (int)std::ceil((double)sample_frame_count(sample) / frames_per_row);
}

size_t sample_frames_for_display_rows(const Engine& engine, const SampleData& sample, double rows)
{
    const double frames_per_row = sample_frames_per_row(engine, sample.sample_rate);
    if (frames_per_row <= 0.0 || rows <= 0.0) return 0;
    return std::min(sample.left.size(), (size_t)std::llround(rows * frames_per_row));
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

static void draw_waveform_helper(wxDC& dc, int x, int y, int w, int h, const SampleData& data,
                                 const wxColour& col, size_t max_samples = 0,
                                 size_t sample_start = 0) {
    if (data.left.empty() || w <= 0) return;
    dc.SetPen(wxPen(col));
    
    bool is_stereo = !data.right.empty();
    int ch_h = is_stereo ? h / 2 : h;

    const size_t data_end = (max_samples > 0) ? std::min(max_samples, data.left.size()) : data.left.size();
    if (data_end <= sample_start) return;
    const size_t data_len = data_end - sample_start;
    if (data_len == 0) return;

    auto draw_channel = [&](const std::vector<float>& ch_data, int ch_y) {
        int mid_y = ch_y + ch_h / 2;
        double samples_per_pixel = (double)data_len / w;
        for (int i = 0; i < w; ++i) {
            size_t start = sample_start + (size_t)(i * samples_per_pixel);
            size_t end   = sample_start + (size_t)((i + 1) * samples_per_pixel);
            if (end > data_end) end = data_end;
            if (start >= end) {
                if (start < data_end) {
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

static wxBitmap make_wave_bitmap(int w, int h, const SampleData& data,
                                  size_t max_samples,
                                  const wxColour& bg, const wxColour& wave_col)
{
    wxBitmap bmp(std::max(1, w), std::max(1, h));
    wxMemoryDC mdc(bmp);
    mdc.SetBrush(wxBrush(bg));
    mdc.SetPen(wxPen(bg));
    mdc.DrawRectangle(0, 0, w, h);
    draw_waveform_helper(mdc, 0, 0, w, h, data, wave_col, max_samples);
    return bmp;
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
    ID_MENU_REDO,
    ID_MENU_SET_MARKER_IN,
    ID_MENU_SET_MARKER_OUT,
    ID_MENU_CLEAR_MARKERS,
    ID_MENU_PUNCH_RECORD
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

int TracksPanel::get_cursor_row() const {
    return m_tracks_view ? m_tracks_view->cursor_row() : 0;
}

void TracksPanel::update() {
    static int last_total_ticks = -1;
    static int last_track_count = -1;
    static size_t last_order_pos = SIZE_MAX;
    static int last_row = -1;
    static int last_transport = -1;

    int current_total = m_tracks_view->get_total_ticks();
    int current_tracks = (int)m_engine.track_count();
    size_t current_pos = m_engine.current_order_pos();
    int current_row = (int)m_engine.current_row();
    int current_transport = (int)m_engine.transport_state();

    if (current_total != last_total_ticks || current_tracks != last_track_count) {
        m_tracks_view->update_view();  // update_view() calls Refresh() internally
        last_total_ticks = current_total;
        last_track_count = current_tracks;
        last_order_pos = current_pos;
        last_row = current_row;
        last_transport = current_transport;
        return;
    }

    // Always refresh during recording to update live waveform and input VU meters
    if (m_engine.is_recording_audio_tracks()) {
        last_order_pos = current_pos;
        last_row = current_row;
        last_transport = current_transport;
        m_tracks_view->Refresh(false);
        return;
    }

    if (current_pos != last_order_pos || current_row != last_row || current_transport != last_transport) {
        const bool transport_changed = (current_transport != last_transport);
        last_order_pos = current_pos;
        last_row = current_row;
        last_transport = current_transport;
        if (!transport_changed && m_engine.transport_state() != TransportState::Stopped) {
            // Steady-state playback: only repaint the narrow playhead strip.
            m_tracks_view->ensure_playhead_visible();
            m_tracks_view->refresh_playhead_strip();
        } else {
            // Transport state changed or stopped: full repaint for correctness.
            m_tracks_view->reset_playhead_tracking();
            m_tracks_view->Refresh(false);
        }
    }
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
    SetDoubleBuffered(true);
    m_zoom = 10.0;
    SetScrollRate(8, 12);
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
    Refresh(false);
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
    wxAutoBufferedPaintDC dc(this);
    PrepareDC(dc);
    draw(dc);
}

void TracksView::draw(wxDC& dc) {
    // Invalidate waveform bitmap cache when zoom changes or it grows too large.
    if (m_cache_zoom != m_zoom || m_wave_cache.size() > 200) {
        m_wave_cache.clear();
        m_cache_zoom = m_zoom;
    }

    // Compute the visible logical area once for culling throughout this draw pass
    int vsx, vsy;
    GetViewStart(&vsx, &vsy);
    int sux, suy;
    GetScrollPixelsPerUnit(&sux, &suy);
    const int view_x = vsx * sux;
    const int view_y = vsy * suy;
    wxSize csize = GetClientSize();
    const int view_right  = view_x + csize.GetWidth();
    const int view_bottom = view_y + csize.GetHeight();

    wxSize virtual_size = GetVirtualSize();
    dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_bg)));
    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_bg)));
    // Only clear the visible portion to avoid unnecessary work on the full virtual canvas
    dc.DrawRectangle(view_x, view_y, csize.GetWidth(), csize.GetHeight());

    int track_h = 80;
    int header_w = kTrackHeaderWidth;
    int num_tracks = (int)m_engine.track_count();
    auto order = m_engine.order_list();
    uint32_t lpb = m_engine.lpb();
    const int sticky_header_top = view_y;
    const int sticky_timeline_top = sticky_header_top;
    const int sticky_ruler_top = sticky_header_top + kTimelineHeaderHeight;
    const int sticky_header_bottom = sticky_header_top + kTracksContentTop;

    // Draw Time Scale (Header) and the top position ruler.
    dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
    dc.DrawRectangle(header_w, sticky_timeline_top, std::max(0, view_right - header_w), kTimelineHeaderHeight);
    dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_bg_color)));
    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_bg_color)));
    dc.DrawRectangle(header_w, sticky_ruler_top, std::max(0, view_right - header_w), kPositionRulerHeight);

    dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_text));
    wxFont header_font(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    dc.SetFont(header_font);

    int total_rows = 0;
    size_t current_pos = m_engine.current_order_pos();
    int play_tick = 0;
    const bool transport_playing = m_engine.transport_state() != TransportState::Stopped;
    const int stopped_tick = std::max(0, cursor_row());
    int display_tick = stopped_tick;
    int loop_start_tick = -1;
    int loop_end_tick = -1;
    const bool loop_enabled = m_engine.transport().m_loop_pattern.load();

    for (size_t i = 0; i < order.size(); ++i) {
        auto& pat = m_engine.pattern(order[i]);
        int pat_rows = (int)pat.row_count();
        const int pattern_start_row = total_rows;
        int px = header_w + tick_to_x(total_rows);
        int pat_end_x = px + tick_to_x(pat_rows);

        // Calculate playhead position (must run even when pattern is off-screen)
        if (i < current_pos) {
            play_tick += pat_rows;
        } else if (i == current_pos) {
            play_tick += (int)m_engine.current_row();
        }

        if (loop_enabled &&
            ((transport_playing && i == current_pos) ||
             (!transport_playing &&
              stopped_tick >= pattern_start_row &&
              stopped_tick < pattern_start_row + pat_rows))) {
            loop_start_tick = pattern_start_row;
            loop_end_tick = pattern_start_row + pat_rows;
        }

        total_rows += pat_rows;

        // Skip drawing this pattern if it is entirely outside the visible horizontal range
        if (pat_end_x <= view_x || px >= view_right) continue;

        // Pattern boundary
        dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
        dc.DrawLine(px, sticky_header_top, px, view_bottom);

        // Pattern label
        wxString buf;
        buf.Printf("POS %zu (PAT %zu)", i, order[i]);
        dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_text));
        dc.DrawText(buf, px + 5, sticky_timeline_top + 2);

        // Beat markers
        if (lpb > 0) {
            const int visible_row_start =
                std::max(0, (int)std::floor((double)(std::max(view_x, header_w) - px) / m_zoom));
            const int visible_row_end =
                std::min(pat_rows, (int)std::ceil((double)(view_right - px) / m_zoom) + 1);
            for (int r = std::max(0, (visible_row_start / (int)lpb) * (int)lpb); r < visible_row_end; r += (int)lpb) {
                int bx = px + tick_to_x(r);
                if (bx < view_x || bx >= view_right) continue;
                dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
                dc.DrawLine(bx, sticky_timeline_top + 12, bx, sticky_timeline_top + kTimelineHeaderHeight);
                if (r % (lpb * 4) == 0) {
                    wxString bbuf;
                    bbuf.Printf("%d", r / lpb);
                    dc.DrawText(bbuf, bx + 2, sticky_ruler_top + 1);
                }
            }
        }
    }

    if (transport_playing) {
        display_tick = play_tick;
    } else if (loop_enabled && loop_start_tick < 0 && !order.empty()) {
        int row_accum = 0;
        for (size_t i = 0; i < order.size(); ++i) {
            const int pat_rows = (int)m_engine.pattern(order[i]).row_count();
            if (i + 1 == order.size() || stopped_tick < row_accum + pat_rows) {
                loop_start_tick = row_accum;
                loop_end_tick = row_accum + pat_rows;
                break;
            }
            row_accum += pat_rows;
        }
    }

    const int ruler_y = sticky_ruler_top + kPositionRulerHeight / 2;
    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_text)));
    dc.DrawLine(header_w, ruler_y, header_w + tick_to_x(total_rows), ruler_y);

    if (loop_enabled && loop_start_tick >= 0 && loop_end_tick > loop_start_tick) {
        const int loop_start_x = header_w + tick_to_x(loop_start_tick);
        const int loop_end_x = header_w + tick_to_x(loop_end_tick);
        const wxColour loop_col = ThemeManager::toWxColour(m_engine.m_selection_color);
        dc.SetPen(wxPen(loop_col, 2));
        dc.DrawLine(loop_start_x, ruler_y - 5, loop_end_x, ruler_y - 5);
        dc.DrawLine(loop_start_x, sticky_timeline_top + 2, loop_start_x, sticky_header_bottom - 2);
        dc.DrawLine(loop_end_x, sticky_timeline_top + 2, loop_end_x, sticky_header_bottom - 2);
        dc.SetBrush(wxBrush(loop_col));
        dc.SetPen(*wxTRANSPARENT_PEN);
        wxPoint loop_in[] = {
            wxPoint(loop_start_x, ruler_y - 8),
            wxPoint(loop_start_x + 8, ruler_y - 5),
            wxPoint(loop_start_x, ruler_y - 2)
        };
        wxPoint loop_out[] = {
            wxPoint(loop_end_x, ruler_y - 8),
            wxPoint(loop_end_x - 8, ruler_y - 5),
            wxPoint(loop_end_x, ruler_y - 2)
        };
        dc.DrawPolygon(3, loop_in);
        dc.DrawPolygon(3, loop_out);
    }

    const int playhead_x = header_w + tick_to_x(display_tick);
    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_cursor), 2));
    dc.DrawLine(playhead_x, sticky_timeline_top + 1, playhead_x, sticky_header_bottom - 2);
    dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_cursor)));
    dc.SetPen(*wxTRANSPARENT_PEN);
    wxPoint playhead_marker[] = {
        wxPoint(playhead_x - 6, ruler_y + 1),
        wxPoint(playhead_x + 6, ruler_y + 1),
        wxPoint(playhead_x, sticky_header_bottom - 2)
    };
    dc.DrawPolygon(3, playhead_marker);

    // Track recording start position
    if (m_engine.is_recording_audio_tracks()) {
        if (m_recording_start_tick < 0) m_recording_start_tick = play_tick;
    } else {
        m_recording_start_tick = -1;
    }

    // Draw Tracks
    int cur_y = kTracksContentTop;
    int first_visible_track = 0;
    while (first_visible_track < num_tracks) {
        const int next_h = get_track_height(first_visible_track);
        if (cur_y + next_h > view_y) break;
        cur_y += next_h;
        ++first_visible_track;
    }
    double bpm = m_engine.tempo();
    double sample_rate = m_engine.sample_rate();
    double samples_per_beat = (sample_rate * 60.0) / bpm;
    double samples_per_row = (lpb > 0) ? (samples_per_beat / lpb) : 44100.0;

    for (int t = first_visible_track; t < num_tracks; ++t) {
        int track_h_actual = get_track_height(t);
        int ty = cur_y;
        if (ty >= view_bottom) break;
        cur_y += track_h_actual;

        auto& track_obj = m_engine.track(t);
        const int content_track_idx = resolve_content_track_index(t);
        Track* content_track = content_track_idx >= 0 ? &m_engine.track(content_track_idx) : nullptr;
        Instrument* content_inst = content_track ? content_track->instrument() : track_obj.instrument();
        const bool is_audio_take_track =
            !m_engine.is_tempo_track((size_t)t) &&
            (track_obj.kind() == TrackKind::Audio || track_obj.kind() == TrackKind::Pilot) &&
            content_inst &&
            content_inst->type() == InstrumentType::Sampler;

        // Track Header
        wxColour header_bg = ThemeManager::toWxColour(m_engine.m_bg_color);
        const wxColour sel_col = ThemeManager::toWxColour(m_engine.m_selection_color);
        const bool track_is_selected = is_track_selected(t);
        const bool track_is_primary  = (m_selected_track == t);
        if (track_is_primary) {
            header_bg = wxColour(sel_col.Red(), sel_col.Green(), sel_col.Blue(), 90);
        } else if (track_is_selected) {
            header_bg = wxColour(sel_col.Red(), sel_col.Green(), sel_col.Blue(), 56);
        }
        dc.SetBrush(wxBrush(header_bg));
        dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_fg_color)));
        dc.DrawRectangle(0, ty, header_w, track_h_actual - 1);
        // 4-px left stripe across the full track row for every selected track.
        if (track_is_selected) {
            dc.SetBrush(wxBrush(sel_col));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(0, ty, 4, track_h_actual);
        }
        // Inset border for the primary (active) track — makes it unambiguous.
        if (track_is_primary) {
            dc.SetPen(wxPen(sel_col, 2));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(1, ty, header_w - 1, track_h_actual - 1);
        }
        // 2-px stripe at the header/content boundary continues the selection stripe.
        if (track_is_selected) {
            dc.SetBrush(wxBrush(sel_col));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(header_w, ty, 2, track_h_actual - 1);
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
        const bool track_has_controls = !track_obj.is_minimized() && has_track_header_controls(m_engine, t);
        const bool track_has_record = track_has_controls && has_record_arm(m_engine, t);
        int text_max_width = header_w - kHeaderTextX - kHeaderTextRightPadding;
        if (track_has_controls) {
            text_max_width =
                std::max(40, control_rect(ty, track_h_actual, HeaderControl::VolDown, track_has_record).GetLeft() -
                                 kHeaderTextX - kHeaderTextRightPadding);
        }
        wxString name = fit_header_text(dc, track_obj.name(), text_max_width);
        dc.DrawText(name, kHeaderTextX, ty + 5);

        // Instrument Info (only shown when not minimized)
        if (!track_obj.is_minimized()) {
            wxFont normal_font(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
            dc.SetFont(normal_font);
            dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_text));
            wxString info_line = track_kind_label(track_obj.kind());
            if (content_inst) {
                info_line += " / " + content_inst->name();
            }
            dc.DrawText(fit_header_text(dc, info_line, text_max_width), kHeaderTextX, ty + 24);

            wxString status_line = wxString::Format("Vol %d%%  Pan %s",
                                                    (int)std::round(track_obj.volume() * 100.0f),
                                                    pan_status_label(track_obj.get_pan()));
            if (track_obj.kind() == TrackKind::Audio && content_track_idx >= 0 && content_track_idx != t) {
                status_line += wxString::Format("  Link T%d", content_track_idx);
            }
            if (track_obj.muted()) status_line += "  M";
            if (track_obj.solo())  status_line += "  S";
            if (has_record_arm(m_engine, t) && m_engine.is_audio_track_armed((size_t)t)) {
                status_line += m_engine.is_recording_audio_tracks() ? "  REC*" : "  REC";
            }
            dc.DrawText(fit_header_text(dc, status_line, text_max_width), kHeaderTextX, ty + 56);

            // Input VU meter for armed audio tracks with an assigned input
            if (track_has_record && m_engine.is_audio_track_armed((size_t)t)) {
                int in_l = -1, in_r = -1;
                track_obj.get_audio_input(in_l, in_r);
                if (in_l >= 0 && (uint32_t)in_l < m_engine.m_num_ins) {
                    float level = m_engine.input_level((uint32_t)in_l);
                    const int vu_x = kHeaderTextX;
                    const int vu_y = ty + 68;
                    const int vu_max_w = std::max(20, text_max_width);
                    const int vu_h = 6;
                    // Background
                    dc.SetBrush(wxBrush(wxColour(40, 20, 20)));
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.DrawRectangle(vu_x, vu_y, vu_max_w, vu_h);
                    // Filled bar
                    const int vu_w = std::min(vu_max_w, (int)(level * vu_max_w));
                    if (vu_w > 0) {
                        const wxColour vu_col = level > 0.8f ? wxColour(220, 40, 40) :
                                                level > 0.5f ? wxColour(220, 180, 40) : wxColour(60, 200, 60);
                        dc.SetBrush(wxBrush(vu_col));
                        dc.DrawRectangle(vu_x, vu_y, vu_w, vu_h);
                    }
                }
            }

            if (track_has_controls) {
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
                    const int pat_end_x = px + tick_to_x(pat_rows);
                    rows_done += pat_rows;

                    if (pat_end_x <= view_x || px >= view_right) continue;

                    if (lpb > 0) {
                        const int visible_row_start =
                            std::max(0, (int)std::floor((double)(std::max(view_x, header_w) - px) / m_zoom));
                        const int visible_row_end =
                            std::min(pat_rows, (int)std::ceil((double)(view_right - px) / m_zoom) + 1);
                        for (int r = std::max(0, (visible_row_start / (int)lpb) * (int)lpb); r < visible_row_end; r += (int)lpb) {
                            const int click_x = px + tick_to_x(r);
                            if (click_x < view_x || click_x >= view_right) continue;
                            const bool accented = (r % (lpb * 4)) == 0;
                            wxColour guide = ThemeManager::toWxColour(
                                accented ? m_engine.m_tracker_cursor : m_engine.m_tracker_lpb_highlight);
                            guide.Set(guide.Red(), guide.Green(), guide.Blue(), accented ? 200 : 120);
                            dc.SetPen(wxPen(guide, accented ? 2 : 1));
                            dc.DrawLine(click_x, ty + 5, click_x, ty + track_h_actual - 6);
                        }
                    }
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
                        const int sample_rows = sample_rows_for_display(m_engine, *sample.data);
                        const int full_nw = std::max(2, tick_to_x(sample_rows));
                        const double sample_spp = (double)sample_frame_count(*sample.data) / (double)full_nw;
                        const int skip_px = std::max(0, view_x - header_w);
                        const int vis_x   = header_w + skip_px;
                        const int vis_w   = std::min(full_nw - skip_px, view_right - vis_x);
                        const int wave_h  = track_h_actual - 10;
                        const wxColour wave_bg  = ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight);
                        const wxColour wave_col = ThemeManager::toWxColour(m_engine.m_tracker_note);
                        if (full_nw > 0 && full_nw <= kMaxWaveCacheWidth) {
                            const auto key = std::make_tuple(
                                reinterpret_cast<uintptr_t>(sample.data.get()), full_nw, wave_h, size_t(0));
                            auto it = m_wave_cache.find(key);
                            if (it == m_wave_cache.end()) {
                                it = m_wave_cache.emplace(
                                    key, make_wave_bitmap(full_nw, wave_h, *sample.data, 0, wave_bg, wave_col))
                                    .first;
                            }
                            if (vis_w > 0) {
                                wxMemoryDC mdc;
                                mdc.SelectObjectAsSource(it->second);
                                dc.Blit(vis_x, ty + 5, vis_w, wave_h, &mdc, skip_px, 0);
                            }
                        } else {
                            // Fallback for very wide waveforms (not cached).
                            dc.SetBrush(wxBrush(wave_bg));
                            dc.SetPen(wxPen(wave_bg));
                            dc.DrawRectangle(header_w, ty + 5, full_nw, wave_h);
                            if (vis_w > 0 && sample_spp > 0.0) {
                                const size_t s_start = (size_t)(skip_px * sample_spp);
                                const size_t s_end   = s_start + (size_t)(vis_w * sample_spp) + 2;
                                draw_waveform_helper(dc, vis_x, ty + 5, vis_w, wave_h,
                                                     *sample.data, wave_col, s_end, s_start);
                            }
                        }
                    }
                }

                // Live recording waveform overlay
                if (track_obj.is_audio_capture_active() && m_recording_start_tick >= 0) {
                    SampleData* live_sd = track_obj.capture_data();
                    const size_t wp = track_obj.capture_write_pos();
                    const double live_spr = live_sd ? sample_frames_per_row(m_engine, live_sd->sample_rate) : 0.0;
                    if (live_sd && wp > 0 && live_spr > 0.0) {
                        const int rec_x     = header_w + tick_to_x(m_recording_start_tick);
                        const int full_lw   = std::max(2, (int)((double)wp / live_spr * m_zoom));
                        const double live_spp = (double)wp / (double)full_lw;
                        // Viewport clip
                        const int l_skip_px = std::max(0, view_x - rec_x);
                        const int l_vis_x   = rec_x + l_skip_px;
                        const int l_vis_w   = std::min(full_lw - l_skip_px, view_right - l_vis_x);
                        // Background shade
                        dc.SetBrush(wxBrush(wxColour(120, 20, 20, 80)));
                        dc.SetPen(*wxTRANSPARENT_PEN);
                        dc.DrawRectangle(rec_x, ty + 5, full_lw, track_h_actual - 10);
                        if (l_vis_w > 0 && live_spp > 0.0) {
                            const size_t l_s0   = (size_t)(l_skip_px * live_spp);
                            const size_t l_send = std::min(wp, l_s0 + (size_t)(l_vis_w * live_spp) + 2);
                            draw_waveform_helper(dc, l_vis_x, ty + 5, l_vis_w, track_h_actual - 10,
                                                 *live_sd, wxColour(255, 80, 80), l_send, l_s0);
                        }
                    }
                }
                continue;
            }

            const int pattern_track_idx = content_track_idx >= 0 ? content_track_idx : t;
            // Pre-compute overlaps once per track (not inside the per-note inner loop)
            std::vector<AudioRegion> cached_overlaps;
            if (content_inst && content_inst->type() == InstrumentType::Sampler) {
                SampleInstrument* sampler = static_cast<SampleInstrument*>(content_inst);
                cached_overlaps = detect_overlaps(sampler);
            }
            // Hoist the memory DC used for waveform-bitmap blits out of the inner
            // note loop so we pay the construction/destruction cost only once per track.
            wxMemoryDC note_wave_mdc;
            int rows_done = 0;
            for (auto pat_idx : order) {
                auto& pat = m_engine.pattern(pat_idx);
                int pat_rows = (int)pat.row_count();
                int px = header_w + tick_to_x(rows_done);
                int pat_end_x = px + tick_to_x(pat_rows);
                rows_done += pat_rows;

                // Skip patterns entirely outside the visible horizontal range
                if (pat_end_x <= view_x || px >= view_right) continue;

                size_t num_cols = pat.column_count(pattern_track_idx);
                int row_start = 0;
                if (m_zoom > 0.0) {
                    row_start = std::max(0, (int)std::floor((double)(std::max(view_x, header_w) - px) / m_zoom));
                    for (size_t c = 0; c < num_cols; ++c) {
                        for (int prev = row_start - 1; prev >= 0; --prev) {
                            if (pat.event(pattern_track_idx, (size_t)prev, c).note != 255) {
                                row_start = std::min(row_start, prev);
                                break;
                            }
                        }
                    }
                }
                const int row_end = std::min(pat_rows, (int)std::ceil((double)(view_right - px) / m_zoom) + 1);
                for (int r = row_start; r < row_end; ++r) {
                    int nx = px + tick_to_x(r);
                    if (nx >= view_right) break;  // All subsequent rows are also off-screen

                    for (size_t c = 0; c < num_cols; ++c) {
                        const auto& ev = pat.event(pattern_track_idx, r, c);
                        if (ev.note != 255) {
                            if (ev.note == 254) { // Note Off
                                if (nx >= view_x) {
                                    dc.SetPen(wxPen(wxColour(255, 100, 100)));
                                    dc.DrawLine(nx, ty + 5, nx, ty + track_h_actual - 5);
                                }
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
                                            int nw_limit = tick_to_x(note_len);
                                            int nw_sample = tick_to_x(sample_rows_for_display(m_engine, *sample.data));
                                            int nw = std::min(nw_limit, nw_sample);
                                            if (nw < 2) nw = 2;
                                            if (nx + nw <= view_x) continue;  // Block entirely left of viewport

                                            const size_t samples_to_draw =
                                                sample_frames_for_display_rows(m_engine, *sample.data, (double)nw / m_zoom);

                                            // Use pre-computed overlaps
                                            bool is_overlapping = (s_idx < cached_overlaps.size()) && cached_overlaps[s_idx].is_overlapping;

                                            const int w_h = track_h_actual - 10;
                                            const wxColour w_bg  = ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight);
                                            const wxColour w_col = ThemeManager::toWxColour(m_engine.m_tracker_note);

                                            if (is_overlapping) {
                                                // Overlapping notes get an orange background; skip the cache for these.
                                                dc.SetBrush(wxBrush(wxColour(255, 200, 0, 128)));
                                                dc.SetPen(wxPen(wxColour(255, 150, 0)));
                                                dc.DrawRectangle(nx, ty + 5, nw, w_h);
                                                draw_waveform_helper(dc, nx, ty + 5, nw, w_h, *sample.data, w_col, samples_to_draw);
                                                dc.SetPen(wxPen(wxColour(255, 100, 0), 2));
                                                dc.DrawRectangle(nx, ty + 5, nw, w_h);
                                            } else if (nw <= kMaxWaveCacheWidth) {
                                                const auto wkey = std::make_tuple(
                                                    reinterpret_cast<uintptr_t>(sample.data.get()), nw, w_h, samples_to_draw);
                                                auto wit = m_wave_cache.find(wkey);
                                                if (wit == m_wave_cache.end()) {
                                                    wit = m_wave_cache.emplace(
                                                        wkey, make_wave_bitmap(nw, w_h, *sample.data, samples_to_draw, w_bg, w_col))
                                                        .first;
                                                }
                                                note_wave_mdc.SelectObjectAsSource(wit->second);
                                                dc.Blit(nx, ty + 5, nw, w_h, &note_wave_mdc, 0, 0);
                                                note_wave_mdc.SelectObject(wxNullBitmap);
                                            } else {
                                                dc.SetBrush(wxBrush(w_bg));
                                                dc.SetPen(wxPen(w_bg));
                                                dc.DrawRectangle(nx, ty + 5, nw, w_h);
                                                draw_waveform_helper(dc, nx, ty + 5, nw, w_h, *sample.data, w_col, samples_to_draw);
                                            }
                                        }
                                    }
                                } else {
                                    int nw = tick_to_x(note_len);
                                    if (nw < 2) nw = 2;
                                    if (nx + nw <= view_x) continue;  // Block entirely left of viewport
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
            }
        }

        // Horizontal line between tracks
        dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
        dc.DrawLine(header_w, ty + track_h_actual - 1, header_w + tick_to_x(total_rows), ty + track_h_actual - 1);
    }

    // Current Playback Marker
    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_cursor)));
    dc.DrawLine(playhead_x, sticky_header_top, playhead_x, view_bottom);

    // Recording region: vertical marker at recording start + shaded region up to play cursor
    if (m_recording_start_tick >= 0 && m_engine.is_recording_audio_tracks()) {
        const int rec_x = header_w + tick_to_x(m_recording_start_tick);
        const int play_x = header_w + tick_to_x(play_tick);
        // Shaded recording region
        if (play_x > rec_x) {
            dc.SetBrush(wxBrush(wxColour(200, 40, 40, 40)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(rec_x, sticky_header_top, play_x - rec_x, csize.GetHeight());
        }
        // Red vertical line at recording start
        dc.SetPen(wxPen(wxColour(220, 60, 60), 2));
        dc.DrawLine(rec_x, sticky_header_top, rec_x, view_bottom);
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
        dc.DrawRectangle(sx1, view_y, sx2 - sx1, csize.GetHeight());

        // Draw selection highlight on the selected track if it's a sampler
        for (int selected_track_idx : selected_tracks()) {
            if (selected_track_idx < 0 || selected_track_idx >= num_tracks) {
                continue;
            }
            const int content_track_idx = resolve_content_track_index(selected_track_idx);
            auto& sel_track = m_engine.track(content_track_idx >= 0 ? content_track_idx : selected_track_idx);
            auto inst = sel_track.instrument();
            if (inst && inst->type() == InstrumentType::Sampler) {
                int cur_y_pos = kTracksContentTop;
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
        dc.DrawLine(sx1, view_y, sx1, view_bottom);
        dc.DrawLine(sx2, view_y, sx2, view_bottom);
    }

    // Draw punch-in / punch-out markers.
    if (m_marker_in >= 0 || m_marker_out >= 0) {
        const int in_x  = (m_marker_in  >= 0) ? (header_w + tick_to_x(m_marker_in))  : -1;
        const int out_x = (m_marker_out >= 0) ? (header_w + tick_to_x(m_marker_out)) : -1;

        // Shaded punch region between markers.
        if (m_marker_in >= 0 && m_marker_out >= 0 && out_x > in_x) {
            dc.SetBrush(wxBrush(wxColour(255, 180, 0, 28)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(in_x, view_y, out_x - in_x, csize.GetHeight());
        }

        // IN marker: green vertical line + small flag.
        if (in_x >= 0) {
            dc.SetPen(wxPen(wxColour(0, 220, 80), 2));
            dc.DrawLine(in_x, view_y, in_x, view_bottom);
            dc.SetBrush(wxBrush(wxColour(0, 220, 80)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            wxPoint tri_in[] = { wxPoint(in_x, view_y), wxPoint(in_x + 14, view_y), wxPoint(in_x, view_y + 12) };
            dc.DrawPolygon(3, tri_in);
            dc.SetTextForeground(wxColour(0, 0, 0));
            dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
            dc.DrawText("IN", in_x + 1, view_y);
        }

        // OUT marker: red vertical line + small flag.
        if (out_x >= 0) {
            dc.SetPen(wxPen(wxColour(220, 40, 40), 2));
            dc.DrawLine(out_x, view_y, out_x, view_bottom);
            dc.SetBrush(wxBrush(wxColour(220, 40, 40)));
            dc.SetPen(*wxTRANSPARENT_PEN);
            wxPoint tri_out[] = { wxPoint(out_x - 14, view_y), wxPoint(out_x, view_y), wxPoint(out_x, view_y + 12) };
            dc.DrawPolygon(3, tri_out);
            dc.SetTextForeground(wxColour(255, 255, 255));
            dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
            dc.DrawText("OUT", out_x - 13, view_y);
        }
    }
}

void TracksView::OnMouseDown(wxMouseEvent& event) {
    if (event.GetY() < kTracksContentTop) {
        return;
    }
    int x, y;
    CalcUnscrolledPosition(event.GetX(), event.GetY(), &x, &y);
    int header_w = kTrackHeaderWidth;
    const bool additive = event.ControlDown() || event.CmdDown();
    const bool range_select = event.ShiftDown();
    
    // Determine which track was clicked
    int num_tracks = (int)m_engine.track_count();
    int cur_y = kTracksContentTop;
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
                    Refresh(false);
                    return;
                }
                if (control_rect(ty, track_h, HeaderControl::Solo, track_has_record).Contains(x, y)) {
                    track.set_solo(!track.solo());
                    m_engine.mark_dirty();
                    Refresh(false);
                    return;
                }
                if (track_has_record && control_rect(ty, track_h, HeaderControl::Record, track_has_record).Contains(x, y)) {
                    m_engine.set_audio_track_armed((size_t)t, !m_engine.is_audio_track_armed((size_t)t));
                    Refresh(false);
                    return;
                }
                if (control_rect(ty, track_h, HeaderControl::VolDown, track_has_record).Contains(x, y)) {
                    track.set_volume(std::max(0.0f, track.volume() - 0.05f));
                    m_engine.mark_dirty();
                    Refresh(false);
                    return;
                }
                if (control_rect(ty, track_h, HeaderControl::VolUp, track_has_record).Contains(x, y)) {
                    track.set_volume(std::min(1.0f, track.volume() + 0.05f));
                    m_engine.mark_dirty();
                    Refresh(false);
                    return;
                }
                if (control_rect(ty, track_h, HeaderControl::PanLeft, track_has_record).Contains(x, y)) {
                    track.set_pan(track.get_pan() - 0.1f);
                    m_engine.mark_dirty();
                    Refresh(false);
                    return;
                }
                if (control_rect(ty, track_h, HeaderControl::PanRight, track_has_record).Contains(x, y)) {
                    track.set_pan(track.get_pan() + 0.1f);
                    m_engine.mark_dirty();
                    Refresh(false);
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
        Refresh(false);
    }
}

void TracksView::OnMouseDrag(wxMouseEvent& event) {
    if (m_is_selecting) {
        int x, y;
        CalcUnscrolledPosition(event.GetX(), event.GetY(), &x, &y);
        int header_w = kTrackHeaderWidth;
        m_sel_end_tick = x_to_tick(x - header_w);
        Refresh(false);
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
    int total_h = kTracksContentTop;
    for (int t = 0; t < num_tracks; ++t) {
        total_h += get_track_height(t);
    }
    int total_w = kTrackHeaderWidth + tick_to_x(get_total_ticks()) + 50;
    total_h += 50;
    SetVirtualSize(total_w, total_h);
    Refresh(false);
}

// Compute the current playhead logical-X (same formula as draw()).
static int compute_play_tick(Engine& engine) {
    auto order = engine.order_list();
    size_t current_pos = engine.current_order_pos();
    int play_tick = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        const int pat_rows = (int)engine.pattern(order[i]).row_count();
        if (i < current_pos) {
            play_tick += pat_rows;
        } else if (i == current_pos) {
            play_tick += (int)engine.current_row();
            break;
        }
    }
    return play_tick;
}

void TracksView::ensure_playhead_visible() {
    const int play_tick = compute_play_tick(m_engine);
    const int logical_x = kTrackHeaderWidth + tick_to_x(play_tick);

    int vsx, vsy;
    GetViewStart(&vsx, &vsy);
    int sux, suy;
    GetScrollPixelsPerUnit(&sux, &suy);
    if (sux <= 0) return;
    const int scroll_px = vsx * sux;

    wxSize csize = GetClientSize();
    const int view_w = csize.GetWidth();
    // Keep a comfortable margin so the playhead doesn't hug the right edge.
    const int margin = std::max(50, view_w / 5);
    const int content_left = kTrackHeaderWidth; // header is always visible

    // Content area starts after the sticky header; the actual scrollable content
    // starts at kTrackHeaderWidth logical pixels. The visible content window is
    // [scroll_px + content_left .. scroll_px + view_w].
    const int vis_left  = scroll_px + content_left;
    const int vis_right = scroll_px + view_w - margin;

    if (logical_x > vis_right || logical_x < vis_left) {
        // Scroll so that the playhead sits margin pixels from the right edge.
        int new_scroll_px = logical_x - (view_w - margin);
        if (new_scroll_px < 0) new_scroll_px = 0;
        Scroll(new_scroll_px / sux, vsy);
    }
}

void TracksView::refresh_playhead_strip() {
    const int play_tick = compute_play_tick(m_engine);
    const int new_logical_x = kTrackHeaderWidth + tick_to_x(play_tick);

    int vsx, vsy;
    GetViewStart(&vsx, &vsy);
    int sux, suy;
    GetScrollPixelsPerUnit(&sux, &suy);
    const int scroll_px = vsx * sux;

    // A strip 6 px wide is wide enough to cover the 2-px line + 1 px rounding slop
    // on each side. We convert from logical coords to client (screen) coords.
    const int strip_w = 8;

    auto refresh_strip = [&](int logical_x) {
        int screen_x = logical_x - scroll_px - strip_w / 2;
        wxSize csize = GetClientSize();
        if (screen_x + strip_w < 0 || screen_x >= csize.GetWidth()) return;
        screen_x = std::max(0, screen_x);
        RefreshRect(wxRect(screen_x, 0, strip_w, csize.GetHeight()), false);
    };

    if (m_last_playhead_logical_x >= 0 && m_last_playhead_logical_x != new_logical_x) {
        refresh_strip(m_last_playhead_logical_x); // erase old position
    }
    refresh_strip(new_logical_x);
    m_last_playhead_logical_x = new_logical_x;
}

void TracksView::OnMouseRightClick(wxMouseEvent& event) {
    // Capture unscrolled X for "set marker here" operations.
    int ux = 0, uy = 0;
    CalcUnscrolledPosition(event.GetX(), event.GetY(), &ux, &uy);
    m_last_right_click_x = ux;
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

    // Punch-in/out markers
    menu.AppendSeparator();
    menu.Append(ID_MENU_SET_MARKER_IN,  "Set IN marker here\t[",  "Set punch-in marker at this position");
    menu.Append(ID_MENU_SET_MARKER_OUT, "Set OUT marker here\t]", "Set punch-out marker at this position");
    if (m_marker_in >= 0 || m_marker_out >= 0) {
        menu.Append(ID_MENU_CLEAR_MARKERS, "Clear markers", "Remove punch-in and punch-out markers");
        if (m_marker_in >= 0 && m_marker_out > m_marker_in && m_engine.has_armed_audio_tracks())
            menu.Append(ID_MENU_PUNCH_RECORD, "Punch record", "Record between IN and OUT markers");
    }
    
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
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { set_marker_in_at(m_last_right_click_x); }, ID_MENU_SET_MARKER_IN);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { set_marker_out_at(m_last_right_click_x); }, ID_MENU_SET_MARKER_OUT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { clear_punch_markers(); }, ID_MENU_CLEAR_MARKERS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { m_engine.play_punch_in(); }, ID_MENU_PUNCH_RECORD);
    
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
        case '[': {
            // Set IN marker at current selection start (or selection cursor).
            int row = (m_sel_start_tick >= 0) ? m_sel_start_tick : 0;
            set_marker_in_at(tick_to_x(row) + kTrackHeaderWidth);
            break;
        }
        case ']': {
            // Set OUT marker at selection end (or after selection start).
            int row = (m_sel_end_tick >= 0) ? std::max(m_sel_start_tick, m_sel_end_tick)
                                             : (m_sel_start_tick >= 0 ? m_sel_start_tick + 1 : 1);
            set_marker_out_at(tick_to_x(row) + kTrackHeaderWidth);
            break;
        }
        default:
            event.Skip();
            return;
    }
}

void TracksView::set_marker_in_at(int unscrolled_x)
{
    const int header_w = kTrackHeaderWidth;
    const int row = x_to_tick(unscrolled_x - header_w);
    m_marker_in = std::max(0, row);
    m_engine.set_punch_in_row(m_marker_in);
    Refresh();
}

void TracksView::set_marker_out_at(int unscrolled_x)
{
    const int header_w = kTrackHeaderWidth;
    const int row = x_to_tick(unscrolled_x - header_w);
    m_marker_out = std::max(0, row);
    m_engine.set_punch_out_row(m_marker_out);
    Refresh();
}

void TracksView::clear_punch_markers()
{
    m_marker_in = -1;
    m_marker_out = -1;
    m_engine.clear_punch_markers();
    Refresh();
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
        m_engine.refresh_timeline_audio_tracks();
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
        m_engine.refresh_timeline_audio_tracks();
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
        m_engine.refresh_timeline_audio_tracks();
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
        m_engine.refresh_timeline_audio_tracks();
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
