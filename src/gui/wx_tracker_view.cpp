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

#include "wx_tracker_view.h"
#include "theme.h"
#include "../core/engine.h"
#include "../edit/cmd_edit_block.h"

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>

namespace tpanar_ns {

wxBEGIN_EVENT_TABLE(TrackerView, wxScrolledWindow)
    EVT_PAINT(TrackerView::OnPaint)
    EVT_KEY_DOWN(TrackerView::OnKeyDown)
    EVT_LEFT_DOWN(TrackerView::OnMouseDown)
    EVT_MOTION(TrackerView::OnMouseDrag)
    EVT_LEFT_UP(TrackerView::OnMouseUp)
    EVT_RIGHT_DOWN(TrackerView::OnRightClick)
wxEND_EVENT_TABLE()

namespace {
    constexpr int kMaxTrackerColumns = 3;
    constexpr int kFieldsPerNoteColumn = 2;
    constexpr int kCharsPerNoteColumn = 8;
    constexpr int kHeaderButtonArea = 44;

    enum {
        ID_TRANSPOSE_UP1   = wxID_HIGHEST + 200,
        ID_TRANSPOSE_DOWN1,
        ID_TRANSPOSE_UP12,
        ID_TRANSPOSE_DOWN12,
        // Pattern-scope (whole current track in current pattern)
        ID_TRANS_PAT_UP1,
        ID_TRANS_PAT_DOWN1,
        ID_TRANS_PAT_UP12,
        ID_TRANS_PAT_DOWN12,
        // Song-scope (whole track across all patterns)
        ID_TRANS_SONG_UP1,
        ID_TRANS_SONG_DOWN1,
        ID_TRANS_SONG_UP12,
        ID_TRANS_SONG_DOWN12,
        ID_SEL_SUBCOLUMN,
        ID_SEL_TRACK,
    };

    int tracker_column_count(const Pattern& pattern, int actual_track) {
        if (actual_track < 0 || actual_track >= (int)pattern.track_count()) return 0;
        return std::min((int)pattern.column_count((size_t)actual_track), kMaxTrackerColumns);
    }
}

static const char* note_names[] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};

TrackerView::TrackerView(wxWindow* parent, wxWindowID id, Pattern& pattern, Engine& engine)
    : wxScrolledWindow(parent, id), m_engine(engine), m_pattern(&pattern)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetDoubleBuffered(true);
    // wxWANTS_CHARS ensures Tab and other special keys reach OnKeyDown
    SetWindowStyle(GetWindowStyle() | wxWANTS_CHARS);
    // Use a fixed-width font for the grid
    SetFont(wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    recalculate_size();
}

void TrackerView::rebuild_visible_tracks() {
    m_visible_tracks.clear();
    for (size_t t = 0; t < m_engine.track_count(); ++t) {
        const auto kind = m_engine.track(t).kind();
        if (m_engine.is_tempo_track(t) || kind == TrackKind::Note) {
            m_visible_tracks.push_back((int)t);
        }
    }
}

int TrackerView::visible_track_count() const {
    return (int)m_visible_tracks.size();
}

int TrackerView::actual_track_index(int visible_track) const {
    if (visible_track < 0 || visible_track >= (int)m_visible_tracks.size()) return -1;
    return m_visible_tracks[(size_t)visible_track];
}

bool TrackerView::is_valid_visible_track(int visible_track) const {
    return actual_track_index(visible_track) >= 0;
}

void TrackerView::recalculate_size() {
    if (!m_pattern) return;

    rebuild_visible_tracks();
    int track_count = visible_track_count();
    int row_count = (int)m_pattern->row_count();
    int char_w = 8;
    int row_h = 18;
    int header_w = 40;

    m_track_ui.clear();
    int cur_x = header_w;
    for (int t = 0; t < track_count; ++t) {
        const int actual_track = actual_track_index(t);
        if (actual_track < 0) continue;
        const auto& track_obj = m_engine.track((size_t)actual_track);
        TrackUI tui;
        tui.x = cur_x;
        if (is_tempo_cell(t)) {
            const int label_w = std::max(7 * char_w + 12, (int)track_obj.name().size() * char_w + 12);
            tui.w = label_w;
        } else {
            int num_cols = tracker_column_count(*m_pattern, actual_track);
            const int content_w = (int)(num_cols * kCharsPerNoteColumn * char_w + 10);
            const int label_w = (int)track_obj.name().size() * char_w + 12 + kHeaderButtonArea;
            tui.w = std::max(content_w, label_w);
        }
        tui.btn_plus_x = cur_x + tui.w - 20;
        tui.btn_minus_x = cur_x + tui.w - 40;
        tui.actual_track = actual_track;
        m_track_ui.push_back(tui);
        cur_x += tui.w + 10;
    }

    int total_w = cur_x + 50;
    int total_h = 20 + row_count * row_h + 20;
    SetVirtualSize(total_w, total_h);
    SetScrollRate(8, 18);
}

int TrackerView::get_center_row_y() {
    int row_h = 18;
    int h = GetClientSize().GetHeight();
    int center_y = 20 + (h - 20) / 2;
    // Align center_y to row grid
    return ((center_y - 20) / row_h) * row_h + 20;
}

int TrackerView::get_field_at(int track, int x) {
    if (track < 0 || track >= (int)m_track_ui.size() || !m_pattern) return 0;
    if (is_tempo_cell(track)) return 0;
    const int actual_track = actual_track_index(track);
    if (actual_track < 0) return 0;
    int rx = x - m_track_ui[track].x - 2;
    int char_w = 8;
    int char_x = rx / char_w;
    int num_cols = tracker_column_count(*m_pattern, actual_track);
    if (num_cols <= 0) return 0;

    const int clamped_char_x = std::max(0, std::min(char_x, num_cols * kCharsPerNoteColumn - 1));
    int col = clamped_char_x / kCharsPerNoteColumn;
    int field_x = clamped_char_x % kCharsPerNoteColumn;
    if (field_x < 5) return col * kFieldsPerNoteColumn + 0; // Note
    return col * kFieldsPerNoteColumn + 1; // Volume
}

int TrackerView::get_field_x(int track, int abs_field, int& width) {
    if (track < 0 || track >= (int)m_track_ui.size() || !m_pattern) return 0;
    if (is_tempo_cell(track)) {
        width = 3 * 8;
        return m_track_ui[track].x + 6;
    }
    const int actual_track = actual_track_index(track);
    if (actual_track < 0) return 0;
    int num_cols = tracker_column_count(*m_pattern, actual_track);
    int char_w = 8;
    int x = m_track_ui[track].x + 2;
    const int clamped_field = std::max(0, std::min(abs_field, std::max(0, num_cols * kFieldsPerNoteColumn - 1)));
    int col = clamped_field / kFieldsPerNoteColumn;
    int field = clamped_field % kFieldsPerNoteColumn;
    x += col * kCharsPerNoteColumn * char_w;
    if (field == 0) { width = 4 * char_w; }
    else { x += 5 * char_w; width = 2 * char_w; }
    return x;
}

int TrackerView::track_total_fields(int track) const {
    if (!m_pattern || !is_valid_visible_track(track)) return 4;
    if (is_tempo_cell(track)) return 1;
    return tracker_column_count(*m_pattern, actual_track_index(track)) * kFieldsPerNoteColumn;
}

int TrackerView::cursor_abs_field() const {
    if (!m_pattern) return 0;
    const int actual_track = actual_track_index(m_cursor_track);
    int num_cols = actual_track >= 0 ? tracker_column_count(*m_pattern, actual_track) : 0;
    return m_cursor_col * kFieldsPerNoteColumn + std::min(m_cursor_field, kFieldsPerNoteColumn - 1);
}

void TrackerView::set_cursor_from_abs_field(int track, int abs_field) {
    const int actual_track = actual_track_index(track);
    int num_cols = (m_pattern && actual_track >= 0) ? tracker_column_count(*m_pattern, actual_track) : 0;
    const int clamped_field = std::max(0, std::min(abs_field, std::max(0, num_cols * kFieldsPerNoteColumn - 1)));
    m_cursor_col = num_cols > 0 ? clamped_field / kFieldsPerNoteColumn : 0;
    m_cursor_field = clamped_field % kFieldsPerNoteColumn;
    clamp_cursor();
}

void TrackerView::sync_record_track_from_cursor() {
    const int actual_track = actual_track_index(m_cursor_track);
    if (actual_track >= 0) {
        m_engine.set_record_track((size_t)actual_track);
    }
}

uint8_t TrackerView::sanitize_field_value(int track, int abs_field, uint8_t value) const {
    const int actual_track = actual_track_index(track);
    if (actual_track >= 0 && m_engine.is_tempo_track((size_t)actual_track) && abs_field == 0) {
        return value;
    }
    return value;
}

bool TrackerView::is_tempo_cell(int track) const {
    const int actual_track = actual_track_index(track);
    return actual_track >= 0 && m_engine.is_tempo_track((size_t)actual_track);
}

uint8_t TrackerView::get_tempo_value(int row) const {
    if (!m_pattern || row < 0 || row >= (int)m_pattern->row_count()) return 0;
    return m_pattern->event(0, (size_t)row, 0).param1;
}

void TrackerView::set_tempo_value(int row, uint8_t value) {
    if (!m_pattern || row < 0 || row >= (int)m_pattern->row_count()) return;
    uint8_t old_v = get_tempo_value(row);
    if (old_v == value) return;
    std::vector<CmdEditBlock::CellEdit> edits;
    edits.push_back({0, (size_t)row, 0, old_v, value});
    m_engine.undo_stack().execute(std::make_unique<CmdEditBlock>(*m_pattern, edits));
}

void TrackerView::OnPaint(wxPaintEvent& event) {
    wxAutoBufferedPaintDC dc(this);
    PrepareDC(dc);
    draw(dc);
}

void TrackerView::draw(wxDC& dc) {
    if (!m_pattern) return;

    int track_count = visible_track_count();
    if (track_count != (int)m_track_ui.size()) {
        recalculate_size();
        track_count = visible_track_count();
    }

    wxColour bg_col = ThemeManager::toWxColour(m_engine.m_tracker_bg);
    dc.SetBrush(wxBrush(bg_col));
    dc.SetPen(wxPen(bg_col));
    wxSize client_size = GetClientSize();
    wxRect clip_box;
    if (!dc.GetClippingBox(clip_box) || clip_box.IsEmpty()) {
        clip_box = wxRect(0, 0, client_size.x, client_size.y);
    }
    dc.DrawRectangle(clip_box.x, clip_box.y, clip_box.width, clip_box.height);

    if (track_count == 0) {
        dc.SetTextForeground(*wxWHITE);
        dc.DrawText("No tracks available. Add tracks in Project tab.", 50, 50);
        return;
    }

    int char_w = 8;
    int row_h = 18;
    int row_count = (int)m_pattern->row_count();
    int playing_row = (int)m_engine.current_row();
    bool is_playing = m_engine.transport_state() != TransportState::Stopped;
    int center_row = is_playing ? playing_row : m_cursor_row;
    int center_y = get_center_row_y();

    // 1. Row Highlights
    for (int r = 0; r < row_count; ++r) {
        int ry = center_y + (r - center_row) * row_h;
        if (ry < 20) continue;
        if (ry > client_size.y + 100) break;

        // LPB highlight
        uint32_t lpb = m_engine.lpb();
        if (lpb > 0 && r % lpb == 0) {
            dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight)));
            dc.DrawLine(0, ry, 20000, ry);
        }

        // Playing row highlight
        if (is_playing && r == playing_row) {
            dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_row_highlight)));
            dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_row_highlight)));
            dc.DrawRectangle(0, ry, 20000, row_h);
        }

        // Cursor row highlight
        if (r == m_cursor_row) {
            wxColour cur_row_col = ThemeManager::toWxColour(m_engine.m_tracker_row_highlight);
            if (!HasFocus()) {
                cur_row_col = wxColour(cur_row_col.Red() / 2, cur_row_col.Green() / 2, cur_row_col.Blue() / 2);
            }
            dc.SetBrush(wxBrush(cur_row_col));
            dc.SetPen(wxPen(cur_row_col));
            dc.DrawRectangle(40, ry, 20000, row_h);
        }

        // Row number
        dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_text));
        dc.DrawText(wxString::Format("%03d", r), 2, ry + 2);
    }

    // 2. Tracks
    for (int t = 0; t < track_count; ++t) {
        if (t >= (int)m_track_ui.size()) break;
        const auto& tui = m_track_ui[t];
        auto& track_obj = m_engine.track((size_t)tui.actual_track);

        // Header
        dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_bg_color)));
        dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_fg_color)));
        dc.DrawRectangle(tui.x, 0, tui.w, 20);
        dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_fg_color));
        const int label_right = m_engine.is_tempo_track((size_t)tui.actual_track) ? (tui.x + tui.w - 5) : (tui.btn_minus_x - 4);
        const int label_chars = std::max(1, (label_right - (tui.x + 5)) / char_w);
        dc.DrawText(track_obj.name().substr(0, (size_t)label_chars), tui.x + 5, 2);

        // +/- Buttons
        if (!m_engine.is_tempo_track((size_t)tui.actual_track)) {
            dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_button_color)));
            dc.DrawRectangle(tui.btn_minus_x, 2, 18, 16);
            dc.DrawRectangle(tui.btn_plus_x, 2, 18, 16);
            dc.DrawText("-", tui.btn_minus_x + 5, 2);
            dc.DrawText("+", tui.btn_plus_x + 5, 2);
        }

        const int actual_track = tui.actual_track;
        int num_cols = tracker_column_count(*m_pattern, actual_track);
        for (int r = 0; r < row_count; ++r) {
            int ry = center_y + (r - center_row) * row_h;
            if (ry < 20) continue;
            if (ry > client_size.y + 100) break;

            auto is_selected = [&](int field) {
                if (!m_sel_active) return false;
                auto start = std::make_pair(m_sel_start.track, m_sel_start.field);
                auto end = std::make_pair(m_sel_end.track, m_sel_end.field);
                if (start > end) std::swap(start, end);
                auto pos = std::make_pair(t, field);
                int min_r = std::min(m_sel_start.row, m_sel_end.row);
                int max_r = std::max(m_sel_start.row, m_sel_end.row);
                return r >= min_r && r <= max_r && pos >= start && pos <= end;
            };

            if (is_tempo_cell(t)) {
                int f_idx = 0;
                int f_w = 0;
                int f_x = get_field_x(t, f_idx, f_w);
                const uint8_t tempo = get_tempo_value(r);
                if (is_selected(f_idx)) {
                    dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
                    dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
                    dc.DrawRectangle(f_x - 1, ry, f_w + 2, row_h);
                    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
                } else if (r == m_cursor_row && t == m_cursor_track) {
                    dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_cursor)));
                    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_cursor)));
                    dc.DrawRectangle(f_x - 1, ry, f_w + 2, row_h);
                    dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_bg));
                } else {
                    dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_effect));
                }
                if (tempo == 0) dc.DrawText("...", f_x, ry + 2);
                else dc.DrawText(wxString::Format("%03u", (unsigned)tempo), f_x, ry + 2);
                continue;
            }

            for (int c = 0; c < num_cols; ++c) {
                const auto& ev = m_pattern->event((size_t)actual_track, (size_t)r, (size_t)c);

                // Note
                int f_idx = c * kFieldsPerNoteColumn + 0;
                int f_w = 0;
                int f_x = get_field_x(t, f_idx, f_w);
                if (is_selected(f_idx)) {
                    dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
                    dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
                    dc.DrawRectangle(f_x - 1, ry, f_w + 2, row_h);
                    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
                } else if (r == m_cursor_row && t == m_cursor_track && c == m_cursor_col && m_cursor_field == 0) {
                    dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_cursor)));
                    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_cursor)));
                    dc.DrawRectangle(f_x - 1, ry, f_w + 2, row_h);
                    dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_bg));
                } else {
                    dc.SetTextForeground(ev.note == 255 ? ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight) : ThemeManager::toWxColour(m_engine.m_tracker_note));
                }
                if (ev.note == 255) dc.DrawText("---", f_x, ry + 2);
                else if (ev.note == 254) dc.DrawText("OFF", f_x, ry + 2);
                else dc.DrawText(wxString::Format("%s%d", note_names[ev.note % 12], ev.note / 12), f_x, ry + 2);

                // Volume
                f_idx = c * kFieldsPerNoteColumn + 1;
                f_x = get_field_x(t, f_idx, f_w);
                if (is_selected(f_idx)) {
                    dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
                    dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
                    dc.DrawRectangle(f_x - 1, ry, f_w + 2, row_h);
                    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
                } else if (r == m_cursor_row && t == m_cursor_track && c == m_cursor_col && m_cursor_field == 1) {
                    dc.SetBrush(wxBrush(ThemeManager::toWxColour(m_engine.m_tracker_cursor)));
                    dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_tracker_cursor)));
                    dc.DrawRectangle(f_x - 1, ry, f_w + 2, row_h);
                    dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_bg));
                } else {
                    dc.SetTextForeground(ThemeManager::toWxColour(m_engine.m_tracker_volume));
                }
                if (ev.volume == 255) dc.DrawText("..", f_x, ry + 2);
                else dc.DrawText(wxString::Format("%02X", ev.volume), f_x, ry + 2);
            }

        }
        dc.SetPen(wxPen(ThemeManager::toWxColour(m_engine.m_fg_color)));
        dc.DrawLine(tui.x + tui.w + 5, 0, tui.x + tui.w + 5, 20000);
    }
}

void TrackerView::OnKeyDown(wxKeyEvent& event) {
    int key = event.GetKeyCode();
    int wx_mods = event.GetModifiers();
    int modifiers = 0;
    if (wx_mods & wxMOD_CONTROL) modifiers |= 0x1000;
    if (wx_mods & wxMOD_ALT)     modifiers |= 0x2000;
    if (wx_mods & wxMOD_SHIFT)   modifiers |= 0x4000;

    Action action = m_engine.m_key_bindings.get_action(key, modifiers);
    bool shift = (wx_mods & wxMOD_SHIFT);

    if (m_cursor_track >= visible_track_count()) return;
    int total_fields = track_total_fields(m_cursor_track);
    int abs_field = cursor_abs_field();

    int old_row = m_cursor_row;
    int old_track = m_cursor_track;
    int old_field = abs_field;

    bool navigated = false;

    switch (key) {
        case WXK_UP:
            // Always move 1 row up (Renoise convention); step only applies after note entry
            m_cursor_row--;
            if (m_cursor_row < 0) m_cursor_row = (int)m_pattern->row_count() - 1;
            navigated = true;
            break;
        case WXK_DOWN:
            // Always move 1 row down (Renoise convention)
            m_cursor_row++;
            if (m_cursor_row >= (int)m_pattern->row_count()) m_cursor_row = 0;
            navigated = true;
            break;
        case WXK_LEFT:
            abs_field--;
            if (abs_field < 0) {
                if (m_cursor_track > 0) {
                    m_cursor_track--;
                    abs_field = track_total_fields(m_cursor_track) - 1;
                } else abs_field = 0;
            }
            set_cursor_from_abs_field(m_cursor_track, abs_field);
            navigated = true;
            break;
        case WXK_RIGHT:
            abs_field++;
            if (abs_field >= total_fields) {
                if (m_cursor_track < visible_track_count() - 1) {
                    m_cursor_track++;
                    abs_field = 0;
                } else abs_field = total_fields - 1;
            }
            set_cursor_from_abs_field(m_cursor_track, abs_field);
            navigated = true;
            break;
        case WXK_TAB:
            // Tab/Shift+Tab: jump to note field of next/previous track (Renoise convention)
            if (shift) {
                if (m_cursor_track > 0) m_cursor_track--;
            } else {
                if (m_cursor_track < visible_track_count() - 1) m_cursor_track++;
            }
            m_cursor_col   = 0;
            m_cursor_field = 0;
            navigated = true;
            break;
        case WXK_PAGEUP:
            m_cursor_row = std::max(0, m_cursor_row - 16);
            navigated = true;
            break;
        case WXK_PAGEDOWN:
            m_cursor_row = std::min((int)m_pattern->row_count() - 1, m_cursor_row + 16);
            navigated = true;
            break;
        case WXK_HOME:
            if (wx_mods & wxMOD_CONTROL) {
                // Ctrl+Home: go to very beginning
                m_cursor_track = 0;
                m_cursor_col   = 0;
                m_cursor_field = 0;
                m_cursor_row   = 0;
            } else if (m_cursor_col != 0 || m_cursor_field != 0) {
                // First press: go to first field of this track
                m_cursor_col   = 0;
                m_cursor_field = 0;
            } else {
                // Already at first field: go to row 0
                m_cursor_row = 0;
            }
            navigated = true;
            break;
        case WXK_END:
            if (wx_mods & wxMOD_CONTROL) {
                // Ctrl+End: go to very end
                m_cursor_track = visible_track_count() - 1;
                m_cursor_row   = (int)m_pattern->row_count() - 1;
            } else {
                m_cursor_row = (int)m_pattern->row_count() - 1;
            }
            navigated = true;
            break;
        case WXK_DELETE:
        case WXK_BACK:
            if (m_engine.m_record_enabled.load()) {
                delete_current_field();
                if (key == WXK_BACK) {
                    m_cursor_row--;
                    if (m_cursor_row < 0) m_cursor_row = (int)m_pattern->row_count() - 1;
                }
            }
            break;
        default: {
            if (m_engine.m_record_enabled.load() && is_tempo_cell(m_cursor_track)) {
                int digit = -1;
                if (key >= '0' && key <= '9') digit = key - '0';
                if (digit != -1) {
                    uint8_t current_v = get_tempo_value(m_cursor_row);
                    int candidate = current_v == 0 ? digit : (int)current_v * 10 + digit;
                    uint8_t new_v = (candidate <= 255) ? (uint8_t)candidate : (uint8_t)digit;
                    set_tempo_value(m_cursor_row, new_v);
                    Refresh();
                    return;
                }
            }

            // Handle hex input for fields that support it
            if (m_engine.m_record_enabled.load() && m_cursor_field == 1) {
                int hex_val = -1;
                if (key >= '0' && key <= '9') hex_val = key - '0';
                else if (key >= 'A' && key <= 'F') hex_val = 10 + (key - 'A');
                else if (key >= 'a' && key <= 'f') hex_val = 10 + (key - 'a');

                if (hex_val != -1) {
                    int abs_f = cursor_abs_field();
                    const int actual_track = actual_track_index(m_cursor_track);
                    if (actual_track < 0) return;
                    uint8_t current_v = m_pattern->get_field((size_t)actual_track, (size_t)m_cursor_row, (size_t)abs_f);
                    
                    // Simple "shift and add" for 2-digit hex fields
                    uint8_t new_v = (uint8_t)((current_v << 4) | (hex_val & 0x0F));
                    if (current_v == 255) new_v = (uint8_t)hex_val; // For Volume 255 = default
                    new_v = sanitize_field_value(m_cursor_track, abs_f, new_v);

                    if (current_v != new_v) {
                        std::vector<CmdEditBlock::CellEdit> edits;
                        edits.push_back({(size_t)actual_track, (size_t)m_cursor_row, (size_t)abs_f, current_v, new_v});
                        m_engine.undo_stack().execute(std::make_unique<CmdEditBlock>(*m_pattern, edits));
                        Refresh();
                    }
                    return;
                }
            }

            if (!handle_action(action)) {
                event.Skip();
            }
            return;
        }
    }

    if (navigated) {
        clamp_cursor();
        sync_record_track_from_cursor();
        if (shift) {
            if (!m_sel_active) { m_sel_active = true; m_sel_start = {old_track, old_row, old_field}; }
            int cur_f = cursor_abs_field();
            m_sel_end = {m_cursor_track, m_cursor_row, cur_f};
        } else {
            m_sel_active = false;
        }
    }

    ensure_cursor_visible();
    Refresh();
}

void TrackerView::OnMouseDown(wxMouseEvent& event) {
    SetFocus();
    int mx, my;
    CalcUnscrolledPosition(event.GetX(), event.GetY(), &mx, &my);

    if (my >= 0 && my < 20) { // Header
        for (size_t t = 0; t < m_track_ui.size(); ++t) {
            const auto& ui = m_track_ui[t];
            if (m_engine.is_tempo_track((size_t)ui.actual_track)) continue;
            if (mx >= ui.btn_plus_x && mx < ui.btn_plus_x + 18) {
                const size_t current = m_engine.pattern().column_count((size_t)ui.actual_track);
                if (current < (size_t)kMaxTrackerColumns) {
                    m_engine.pattern().set_column_count((size_t)ui.actual_track, current + 1);
                    recalculate_size();
                    Refresh();
                }
                return;
            }
            if (mx >= ui.btn_minus_x && mx < ui.btn_minus_x + 18) {
                size_t current = m_engine.pattern().column_count((size_t)ui.actual_track);
                if (current > (size_t)kMaxTrackerColumns) {
                    m_engine.pattern().set_column_count((size_t)ui.actual_track, (size_t)kMaxTrackerColumns);
                    recalculate_size();
                    Refresh();
                } else if (current > 1) {
                    m_engine.pattern().set_column_count((size_t)ui.actual_track, current - 1);
                    recalculate_size();
                    Refresh();
                }
                return;
            }
        }
    } else {
        int row_h = 18;
        int center_y = get_center_row_y();
        bool is_playing = m_engine.transport_state() != TransportState::Stopped;
        int center_row = is_playing ? (int)m_engine.current_row() : m_cursor_row;

        int row = center_row + (my - center_y) / row_h;
        row = std::max(0, std::min((int)m_pattern->row_count() - 1, row));
        m_cursor_row = row;

        int track = -1;
        for (size_t t = 0; t < m_track_ui.size(); ++t) {
            if (mx >= m_track_ui[t].x && mx < m_track_ui[t].x + m_track_ui[t].w) { track = (int)t; break; }
        }
        if (track != -1) m_cursor_track = track;
        
        int field = get_field_at(m_cursor_track, mx);
        set_cursor_from_abs_field(m_cursor_track, field);
        sync_record_track_from_cursor();

        if (event.ShiftDown()) {
            if (!m_sel_active) { 
                m_sel_active = true; 
                m_sel_start = {m_cursor_track, m_cursor_row, field};
            }
            m_sel_end = {m_cursor_track, m_cursor_row, field};
        } else { m_sel_active = false; }
        
        m_selecting = true;
        if (!event.ShiftDown()) {
            m_sel_start = {m_cursor_track, m_cursor_row, field};
            m_sel_end = {m_cursor_track, m_cursor_row, field};
        }
        
        ensure_cursor_visible();
        Refresh();
    }
}

void TrackerView::OnMouseDrag(wxMouseEvent& event) {
    if (m_selecting) {
        int mx, my;
        CalcUnscrolledPosition(event.GetX(), event.GetY(), &mx, &my);
        int row_h = 18;
        int center_y = get_center_row_y();
        bool is_playing = m_engine.transport_state() != TransportState::Stopped;
        int center_row = is_playing ? (int)m_engine.current_row() : m_cursor_row;

        int row = center_row + (my - center_y) / row_h;
        row = std::max(0, std::min((int)m_pattern->row_count() - 1, row));
        
        int track = 0;
        for (size_t t = 0; t < m_track_ui.size(); ++t) {
            if (mx >= m_track_ui[t].x && mx < m_track_ui[t].x + m_track_ui[t].w) { track = (int)t; break; }
        }

        int field = get_field_at(track, mx);

        if (!m_sel_active) { m_sel_active = true; }
        m_sel_end = {track, row, field};
        Refresh();
    }
}

void TrackerView::OnMouseUp(wxMouseEvent& event) {
    m_selecting = false;
}

void TrackerView::set_current_row(int row) {
    if (row == m_cursor_row) {
        return;
    }
    m_cursor_row = row;
    clamp_cursor();
    Refresh();
}

void TrackerView::set_pattern(Pattern& pattern) {
    m_pattern = &pattern;
    recalculate_size();
    Refresh();
}

void TrackerView::ensure_cursor_visible() {
    // Horizontal scrolling:
    if (m_cursor_track < (int)m_track_ui.size()) {
        int tx = m_track_ui[m_cursor_track].x;
        int tw = m_track_ui[m_cursor_track].w;
        int x, y;
        GetViewStart(&x, &y);
        int xu, yu;
        GetScrollPixelsPerUnit(&xu, &yu);
        int vx = x * xu;
        int vw = GetClientSize().x;

        if (tx < vx) Scroll(tx / xu, y);
        else if (tx + tw > vx + vw) Scroll((tx + tw - vw + 20) / xu, y);
    }
}

void TrackerView::delete_current_field() {
    if (!m_pattern) return;
    if (is_tempo_cell(m_cursor_track)) {
        set_tempo_value(m_cursor_row, 0);
        Refresh();
        return;
    }
    const int actual_track = actual_track_index(m_cursor_track);
    if (actual_track < 0) return;
    int abs_f = cursor_abs_field();
    uint8_t old_v = m_pattern->get_field((size_t)actual_track, (size_t)m_cursor_row, (size_t)abs_f);
    uint8_t new_v = 0;
    if (m_cursor_field == 0) new_v = 255;
    else if (m_cursor_field == 1) new_v = 255;
    new_v = sanitize_field_value(m_cursor_track, abs_f, new_v);

    if (old_v != new_v) {
        std::vector<CmdEditBlock::CellEdit> edits;
        edits.push_back({(size_t)actual_track, (size_t)m_cursor_row, (size_t)abs_f, old_v, new_v});
        m_engine.undo_stack().execute(std::make_unique<CmdEditBlock>(*m_pattern, edits));
        Refresh();
    }
}

void TrackerView::clamp_cursor() {
    if (m_pattern) {
        rebuild_visible_tracks();
        if (m_cursor_row < 0) m_cursor_row = 0;
        if (m_cursor_row >= (int)m_pattern->row_count()) m_cursor_row = (int)m_pattern->row_count() - 1;
        if (m_cursor_track < 0) m_cursor_track = 0;
        if (visible_track_count() <= 0) {
            m_cursor_track = 0;
            m_cursor_col = 0;
            m_cursor_field = 0;
            return;
        }
        if (m_cursor_track >= visible_track_count()) m_cursor_track = visible_track_count() - 1;

        if (is_tempo_cell(m_cursor_track)) {
            m_cursor_col = 0;
            m_cursor_field = 0;
            return;
        }

        int num_cols = tracker_column_count(*m_pattern, actual_track_index(m_cursor_track));
        if (num_cols == 0) {
            m_cursor_col = 0;
            m_cursor_field = 0;
        } else {
            if (m_cursor_col < 0) m_cursor_col = 0;
            if (m_cursor_col >= num_cols) m_cursor_col = num_cols - 1;
            if (m_cursor_field < 0) m_cursor_field = 0;
            if (m_cursor_field > 1) m_cursor_field = 1;
        }
    }
}

void TrackerView::insert_note(uint8_t note) {
    if (!m_pattern) return;
    const int actual_track = actual_track_index(m_cursor_track);
    if (actual_track < 0 || m_engine.is_tempo_track((size_t)actual_track)) return;
    int target_row = m_cursor_row;
    if (m_engine.is_playing() && m_engine.m_record_enabled.load()) {
        target_row = (int)m_engine.current_row();
    }

    uint8_t final_note = note;
    if (note != 254) {
        int octave_note = note + m_engine.base_octave() * 12;
        if (octave_note > 119) octave_note = 119;
        final_note = (uint8_t)octave_note;
    }

    if (m_engine.m_record_enabled.load()) {
        int abs_f = m_cursor_col * kFieldsPerNoteColumn + 0;
        uint8_t old_note = m_pattern->get_field((size_t)actual_track, (size_t)target_row, (size_t)abs_f);
        if (old_note != final_note) {
            std::vector<CmdEditBlock::CellEdit> edits;
            edits.push_back({(size_t)actual_track, (size_t)target_row, (size_t)abs_f, old_note, final_note});
            m_engine.undo_stack().execute(std::make_unique<CmdEditBlock>(*m_pattern, edits));
        }

        if (!m_engine.is_playing()) {
            m_cursor_row = std::min((int)m_pattern->row_count() - 1, m_cursor_row + (int)m_engine.step_size());
        }
    }
    m_engine.preview_note((size_t)actual_track, final_note, (size_t)m_cursor_col);
    Refresh();
}

bool TrackerView::handle_action(Action action) {
    int note = -1;
    switch (action) {
        case Action::Undo: m_engine.undo_stack().undo(); Refresh(); return true;
        case Action::Redo: m_engine.undo_stack().redo(); Refresh(); return true;

        case Action::Copy: {
            if (!m_sel_active) return true;
            auto& clip = m_engine.clipboard();
            clip.cells.clear();
            auto start = std::make_pair(m_sel_start.track, m_sel_start.field);
            auto end = std::make_pair(m_sel_end.track, m_sel_end.field);
            if (start > end) std::swap(start, end);
            int min_r = std::min(m_sel_start.row, m_sel_end.row);
            int max_r = std::max(m_sel_start.row, m_sel_end.row);
            clip.width_tracks = end.first - start.first + 1;
            clip.height_rows = max_r - min_r + 1;
            for (int t = start.first; t <= end.first; ++t) {
                int total_f = track_total_fields(t);
                const int actual_track = actual_track_index(t);
                if (actual_track < 0) continue;
                for (int r = min_r; r <= max_r; ++r) {
                    for (int f = 0; f < total_f; ++f) {
                        auto cur_pos = std::make_pair(t, f);
                        if (cur_pos >= start && cur_pos <= end) {
                            uint8_t val = m_pattern->get_field((size_t)actual_track, (size_t)r, (size_t)f);
                            clip.cells.push_back({t - start.first, r - min_r, f, val});
                        }
                    }
                }
            }
            return true;
        }
        case Action::Cut: {
            if (!m_sel_active) return true;
            handle_action(Action::Copy);
            handle_action(Action::Clear);
            return true;
        }
        case Action::Paste: {
            const auto& clip = m_engine.clipboard();
            if (clip.cells.empty()) return true;
            std::vector<CmdEditBlock::CellEdit> edits;
            for (const auto& cell : clip.cells) {
                int target_t = m_cursor_track + cell.rel_track;
                int target_r = m_cursor_row + cell.rel_row;
                int target_f = cell.abs_field;
                const int actual_track = actual_track_index(target_t);
                if (actual_track >= 0 && target_r < (int)m_pattern->row_count()) {
                    int total_f = track_total_fields(target_t);
                    if (target_f < total_f) {
                        uint8_t new_v = sanitize_field_value(target_t, target_f, cell.value);
                        uint8_t old_v = m_pattern->get_field((size_t)actual_track, (size_t)target_r, (size_t)target_f);
                        if (old_v != new_v)
                            edits.push_back({(size_t)actual_track, (size_t)target_r, (size_t)target_f, old_v, new_v});
                    }
                }
            }
            if (!edits.empty()) {
                m_engine.undo_stack().execute(std::make_unique<CmdEditBlock>(*m_pattern, edits));
                Refresh();
            }
            return true;
        }

        case Action::NoteOff: note = 254; break;
        // ... (Note actions remain same)
        case Action::NoteC: note = 0; break;
        case Action::NoteCs: note = 1; break;
        case Action::NoteD: note = 2; break;
        case Action::NoteDs: note = 3; break;
        case Action::NoteE: note = 4; break;
        case Action::NoteF: note = 5; break;
        case Action::NoteFs: note = 6; break;
        case Action::NoteG: note = 7; break;
        case Action::NoteGs: note = 8; break;
        case Action::NoteA: note = 9; break;
        case Action::NoteAs: note = 10; break;
        case Action::NoteB: note = 11; break;
        case Action::NoteC2: note = 12; break;
        case Action::NoteCs2: note = 13; break;
        case Action::NoteD2: note = 14; break;
        case Action::NoteDs2: note = 15; break;
        case Action::NoteE2: note = 16; break;
        case Action::NoteF2: note = 17; break;
        case Action::NoteFs2: note = 18; break;
        case Action::NoteG2: note = 19; break;
        case Action::NoteGs2: note = 20; break;
        case Action::NoteA2: note = 21; break;
        case Action::NoteAs2: note = 22; break;
        case Action::NoteB2: note = 23; break;
        case Action::NoteC3: note = 24; break;
        
        case Action::JumpToRow0:  m_cursor_row = 0; Refresh(); return true;
        case Action::JumpToRow16: m_cursor_row = 16; clamp_cursor(); Refresh(); return true;
        case Action::JumpToRow32: m_cursor_row = 32; clamp_cursor(); Refresh(); return true;
        case Action::JumpToRow48: m_cursor_row = 48; clamp_cursor(); Refresh(); return true;

        case Action::JumpToNextColumn: {
            int next_track = m_cursor_track;
            for (int i = 0; i < visible_track_count(); ++i) {
                next_track++;
                if (next_track >= visible_track_count()) next_track = 0;
                if (is_tempo_cell(next_track)) {
                    m_cursor_track = next_track;
                    m_cursor_col = 0;
                    m_cursor_field = 0;
                    sync_record_track_from_cursor();
                    ensure_cursor_visible(); Refresh(); return true;
                }
                int next_cols = tracker_column_count(*m_pattern, actual_track_index(next_track));
                if (next_cols > 0) {
                    m_cursor_track = next_track;
                    m_cursor_col = 0;
                    m_cursor_field = 0;
                    sync_record_track_from_cursor();
                    ensure_cursor_visible(); Refresh(); return true;
                }
            }
            ensure_cursor_visible(); Refresh(); return true;
        }
        case Action::JumpToPrevColumn: {
            int prev_track = m_cursor_track;
            for (int i = 0; i < visible_track_count(); ++i) {
                prev_track--;
                if (prev_track < 0) prev_track = visible_track_count() - 1;
                if (is_tempo_cell(prev_track)) {
                    m_cursor_track = prev_track;
                    m_cursor_col = 0;
                    m_cursor_field = 0;
                    sync_record_track_from_cursor();
                    ensure_cursor_visible(); Refresh(); return true;
                }
                int prev_cols = tracker_column_count(*m_pattern, actual_track_index(prev_track));
                if (prev_cols > 0) {
                    m_cursor_track = prev_track;
                    m_cursor_col = prev_cols - 1;
                    m_cursor_field = 0;
                    sync_record_track_from_cursor();
                    ensure_cursor_visible(); Refresh(); return true;
                }
            }
            ensure_cursor_visible(); Refresh(); return true;
        }

        case Action::IncPatternIndex: {
            size_t pos = m_engine.m_edit_order_pos.load();
            auto order = m_engine.order_list();
            if (pos < order.size()) {
                if (order[pos] < m_engine.pattern_count() - 1) {
                    order[pos]++;
                    m_engine.set_order(order);
                    m_engine.set_active_pattern(order[pos]);
                    set_pattern(m_engine.pattern());
                    Refresh();
                }
            }
            return true;
        }
        case Action::DecPatternIndex: {
            size_t pos = m_engine.m_edit_order_pos.load();
            auto order = m_engine.order_list();
            if (pos < order.size()) {
                if (order[pos] > 0) {
                    order[pos]--;
                    m_engine.set_order(order);
                    m_engine.set_active_pattern(order[pos]);
                    set_pattern(m_engine.pattern());
                    Refresh();
                }
            }
            return true;
        }

        case Action::SelectAll:
            m_sel_active = true;
            m_sel_start = {0, 0, 0};
            m_sel_end = {visible_track_count() - 1, (int)m_pattern->row_count() - 1, std::max(0, track_total_fields(visible_track_count() - 1) - 1)};
            Refresh();
            return true;

        case Action::Clear:
            if (m_sel_active) {
                std::vector<CmdEditBlock::CellEdit> edits;
                auto start = std::make_pair(m_sel_start.track, m_sel_start.field);
                auto end = std::make_pair(m_sel_end.track, m_sel_end.field);
                if (start > end) std::swap(start, end);
                int min_r = std::min(m_sel_start.row, m_sel_end.row);
                int max_r = std::max(m_sel_start.row, m_sel_end.row);

                for (int t = start.first; t <= end.first; ++t) {
                    const int actual_track = actual_track_index(t);
                    if (actual_track < 0) continue;
                    int num_cols = tracker_column_count(*m_pattern, actual_track);
                    int total_f = num_cols * kFieldsPerNoteColumn;
                    for (int r = min_r; r <= max_r; ++r) {
                        for (int f = 0; f < total_f; ++f) {
                            auto cur_pos = std::make_pair(t, f);
                            if (cur_pos >= start && cur_pos <= end) {
                                uint8_t old_v = m_pattern->get_field((size_t)actual_track, (size_t)r, (size_t)f);
                                uint8_t new_v = 0;
                                if (f < num_cols * kFieldsPerNoteColumn) {
                                    if (f % kFieldsPerNoteColumn == 0) new_v = 255; // Note
                                    else if (f % kFieldsPerNoteColumn == 1) new_v = 255; // Vol
                                }
                                if (old_v != new_v)
                                    edits.push_back({(size_t)actual_track, (size_t)r, (size_t)f, old_v, new_v});
                            }
                        }
                    }
                }
                if (!edits.empty()) {
                    m_engine.undo_stack().execute(std::make_unique<CmdEditBlock>(*m_pattern, edits));
                    Refresh();
                }
                return true;
            } else if (m_engine.m_record_enabled.load()) {
                delete_current_field();
                return true;
            }
            break;

        case Action::InsertRow:
            if (m_engine.m_record_enabled.load()) {
                m_pattern->insert_row(m_cursor_row);
                Refresh(); return true;
            }
            break;
        case Action::DeleteRow:
            if (m_engine.m_record_enabled.load()) {
                m_pattern->delete_row(m_cursor_row);
                Refresh(); return true;
            }
            break;

        case Action::InsertPattern:
            m_engine.add_pattern_to_order();
            Refresh(); return true;
        case Action::DeletePattern: {
            size_t pos = m_engine.m_edit_order_pos.load();
            m_engine.remove_pattern_from_order(pos);
            if (pos >= m_engine.m_order.size() && pos > 0) m_engine.m_edit_order_pos.store(pos - 1);
            if (!m_engine.m_order.empty()) m_engine.set_active_pattern(m_engine.m_order[m_engine.m_edit_order_pos.load()]);
            set_pattern(m_engine.pattern());
            Refresh(); return true;
        }
        case Action::DuplicatePattern: {
            size_t pos = m_engine.m_edit_order_pos.load();
            m_engine.copy_pattern_in_order(pos);
            Refresh(); return true;
        }

        default: return false;
    }

    if (note != -1) {
        insert_note((uint8_t)note);
        ensure_cursor_visible();
        Refresh();
        return true;
    }
    return false;
}

void TrackerView::OnRightClick(wxMouseEvent& event) {
    if (!m_pattern) return;
    SetFocus();

    // Translate mouse to pattern cell
    int mx, my;
    CalcUnscrolledPosition(event.GetX(), event.GetY(), &mx, &my);

    if (my >= 20) {
        int row_h = 18;
        int center_y = get_center_row_y();
        bool is_playing = m_engine.transport_state() != TransportState::Stopped;
        int center_row = is_playing ? (int)m_engine.current_row() : m_cursor_row;
        int row = center_row + (my - center_y) / row_h;
        row = std::max(0, std::min((int)m_pattern->row_count() - 1, row));
        m_cursor_row = row;

        for (size_t t = 0; t < m_track_ui.size(); ++t) {
            if (mx >= m_track_ui[t].x && mx < m_track_ui[t].x + m_track_ui[t].w) {
                m_cursor_track = (int)t; break;
            }
        }
        int field = get_field_at(m_cursor_track, mx);
        set_cursor_from_abs_field(m_cursor_track, field);
        sync_record_track_from_cursor();
        Refresh();
    }

    wxMenu menu;

    // Transpose sub-menu — three sections: Selection / Pattern / Song
    wxMenu* trans_menu = new wxMenu;

    if (m_sel_active) {
        trans_menu->Append(ID_TRANSPOSE_UP1,    wxT("Selection +1 Semitone"));
        trans_menu->Append(ID_TRANSPOSE_DOWN1,  wxT("Selection -1 Semitone"));
        trans_menu->Append(ID_TRANSPOSE_UP12,   wxT("Selection +12 Semitones (Octave Up)"));
        trans_menu->Append(ID_TRANSPOSE_DOWN12, wxT("Selection -12 Semitones (Octave Down)"));
        trans_menu->AppendSeparator();
    }

    trans_menu->Append(ID_TRANS_PAT_UP1,    wxT("Pattern Track +1 Semitone"));
    trans_menu->Append(ID_TRANS_PAT_DOWN1,  wxT("Pattern Track -1 Semitone"));
    trans_menu->Append(ID_TRANS_PAT_UP12,   wxT("Pattern Track +12 Semitones (Octave Up)"));
    trans_menu->Append(ID_TRANS_PAT_DOWN12, wxT("Pattern Track -12 Semitones (Octave Down)"));
    trans_menu->AppendSeparator();

    trans_menu->Append(ID_TRANS_SONG_UP1,    wxT("Song Track +1 Semitone"));
    trans_menu->Append(ID_TRANS_SONG_DOWN1,  wxT("Song Track -1 Semitone"));
    trans_menu->Append(ID_TRANS_SONG_UP12,   wxT("Song Track +12 Semitones (Octave Up)"));
    trans_menu->Append(ID_TRANS_SONG_DOWN12, wxT("Song Track -12 Semitones (Octave Down)"));

    menu.AppendSubMenu(trans_menu, wxT("Transpose"));

    menu.AppendSeparator();

    // Selection sub-menu
    wxMenu* sel_menu = new wxMenu;
    sel_menu->Append(ID_SEL_SUBCOLUMN, wxT("Select this Sub-column"));
    sel_menu->Append(ID_SEL_TRACK,     wxT("Select Entire Track"));
    menu.AppendSubMenu(sel_menu, wxT("Select"));

    // Bind handlers
    menu.Bind(wxEVT_MENU, [&](wxCommandEvent& ev) {
        int id = ev.GetId();

        // Selection-scope transpose
        if (id == ID_TRANSPOSE_UP1 || id == ID_TRANSPOSE_DOWN1 ||
            id == ID_TRANSPOSE_UP12 || id == ID_TRANSPOSE_DOWN12) {
            int semitones = 0;
            if      (id == ID_TRANSPOSE_UP1)   semitones = +1;
            else if (id == ID_TRANSPOSE_DOWN1)  semitones = -1;
            else if (id == ID_TRANSPOSE_UP12)   semitones = +12;
            else if (id == ID_TRANSPOSE_DOWN12) semitones = -12;
            do_transpose(semitones);

        // Pattern-scope transpose (whole track in current pattern)
        } else if (id == ID_TRANS_PAT_UP1 || id == ID_TRANS_PAT_DOWN1 ||
                   id == ID_TRANS_PAT_UP12 || id == ID_TRANS_PAT_DOWN12) {
            int semitones = 0;
            if      (id == ID_TRANS_PAT_UP1)   semitones = +1;
            else if (id == ID_TRANS_PAT_DOWN1)  semitones = -1;
            else if (id == ID_TRANS_PAT_UP12)   semitones = +12;
            else if (id == ID_TRANS_PAT_DOWN12) semitones = -12;
            do_transpose_pattern(semitones);

        // Song-scope transpose (track across all patterns)
        } else if (id == ID_TRANS_SONG_UP1 || id == ID_TRANS_SONG_DOWN1 ||
                   id == ID_TRANS_SONG_UP12 || id == ID_TRANS_SONG_DOWN12) {
            int semitones = 0;
            if      (id == ID_TRANS_SONG_UP1)   semitones = +1;
            else if (id == ID_TRANS_SONG_DOWN1)  semitones = -1;
            else if (id == ID_TRANS_SONG_UP12)   semitones = +12;
            else if (id == ID_TRANS_SONG_DOWN12) semitones = -12;
            do_transpose_song(semitones);

        } else if (id == ID_SEL_SUBCOLUMN) {
            // Select all rows of the current sub-column (3 fields: note, sample, vol)
            const int actual_track = actual_track_index(m_cursor_track);
            int num_cols_sc = actual_track >= 0 ? tracker_column_count(*m_pattern, actual_track) : 0;
            int base_f = (num_cols_sc > 0) ? (m_cursor_col * kFieldsPerNoteColumn) : 0;
            int end_f  = (num_cols_sc > 0) ? (m_cursor_col * kFieldsPerNoteColumn + (kFieldsPerNoteColumn - 1)) : 0;
            m_sel_start = {m_cursor_track, 0, base_f};
            m_sel_end   = {m_cursor_track, (int)m_pattern->row_count() - 1, end_f};
            m_sel_active = true;
            Refresh();
        } else if (ev.GetId() == ID_SEL_TRACK) {
            // Select all rows and all fields of the current track
            int last_f = std::max(0, track_total_fields(m_cursor_track) - 1);
            m_sel_start = {m_cursor_track, 0, 0};
            m_sel_end   = {m_cursor_track, (int)m_pattern->row_count() - 1, last_f};
            m_sel_active = true;
            Refresh();
        }
    });

    PopupMenu(&menu);
}

void TrackerView::do_transpose(int semitones) {
    if (!m_pattern) return;

    int row_count = (int)m_pattern->row_count();
    int r0, r1, t0, t1;

    if (m_sel_active) {
        t0 = std::min(m_sel_start.track, m_sel_end.track);
        t1 = std::max(m_sel_start.track, m_sel_end.track);
        r0 = std::min(m_sel_start.row,   m_sel_end.row);
        r1 = std::max(m_sel_start.row,   m_sel_end.row);
    } else {
        // Operate on the entire current track
        t0 = t1 = m_cursor_track;
        r0 = 0;
        r1 = row_count - 1;
    }

    std::vector<CmdEditBlock::CellEdit> edits;

    for (int t = t0; t <= t1; ++t) {
        const int actual_track = actual_track_index(t);
        if (actual_track < 0 || is_tempo_cell(t)) continue;
        int num_cols = tracker_column_count(*m_pattern, actual_track);
        for (int r = r0; r <= r1; ++r) {
            for (int c = 0; c < num_cols; ++c) {
                int abs_f = c * kFieldsPerNoteColumn + 0; // note field
                uint8_t note = m_pattern->get_field((size_t)actual_track, (size_t)r, (size_t)abs_f);
                if (note == 255 || note == 254) continue; // empty or note-off

                int new_note = (int)note + semitones;
                new_note = std::max(0, std::min(119, new_note));
                if ((uint8_t)new_note != note)
                    edits.push_back({(size_t)actual_track, (size_t)r, (size_t)abs_f, note, (uint8_t)new_note});
            }
        }
    }

    if (!edits.empty()) {
        m_engine.undo_stack().execute(std::make_unique<CmdEditBlock>(*m_pattern, edits));
        Refresh();
    }
}

void TrackerView::do_transpose_pattern(int semitones) {
    if (!m_pattern) return;
    int track = actual_track_index(m_cursor_track);
    if (track < 0 || is_tempo_cell(m_cursor_track)) return;
    int num_cols = tracker_column_count(*m_pattern, track);
    int row_count = (int)m_pattern->row_count();

    std::vector<CmdEditBlock::CellEdit> edits;
    for (int r = 0; r < row_count; ++r) {
        for (int c = 0; c < num_cols; ++c) {
            int abs_f = c * kFieldsPerNoteColumn;
            uint8_t note = m_pattern->get_field((size_t)track, (size_t)r, (size_t)abs_f);
            if (note == 255 || note == 254) continue;
            int new_note = std::max(0, std::min(119, (int)note + semitones));
            if ((uint8_t)new_note != note)
                edits.push_back({(size_t)track, (size_t)r, (size_t)abs_f, note, (uint8_t)new_note});
        }
    }
    if (!edits.empty()) {
        m_engine.undo_stack().execute(std::make_unique<CmdEditBlock>(*m_pattern, edits));
        Refresh();
    }
}

void TrackerView::do_transpose_song(int semitones) {
    int track = actual_track_index(m_cursor_track);
    if (track < 0 || is_tempo_cell(m_cursor_track)) return;
    size_t pat_count = m_engine.pattern_count();

    // Collect edits per pattern (one undo step per pattern, batched together
    // by applying them sequentially; the current pattern refreshes the view)
    bool any = false;
    for (size_t pi = 0; pi < pat_count; ++pi) {
        Pattern& pat = m_engine.pattern(pi);
        if (track >= (int)pat.track_count()) continue;
        int num_cols = tracker_column_count(pat, track);
        int row_count = (int)pat.row_count();

        std::vector<CmdEditBlock::CellEdit> edits;
        for (int r = 0; r < row_count; ++r) {
            for (int c = 0; c < num_cols; ++c) {
                int abs_f = c * kFieldsPerNoteColumn;
                uint8_t note = pat.get_field(track, r, abs_f);
                if (note == 255 || note == 254) continue;
                int new_note = std::max(0, std::min(119, (int)note + semitones));
                if ((uint8_t)new_note != note)
                    edits.push_back({(size_t)track, (size_t)r, (size_t)abs_f, note, (uint8_t)new_note});
            }
        }
        if (!edits.empty()) {
            m_engine.undo_stack().execute(std::make_unique<CmdEditBlock>(pat, edits));
            any = true;
        }
    }
    if (any) Refresh();
}

} // namespace tpanar_ns
