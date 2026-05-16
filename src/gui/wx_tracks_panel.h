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

#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/dc.h>
#include <wx/bitmap.h>
#include <map>
#include <set>
#include <tuple>
#include <cstdint>

namespace tpanar_ns {

class Engine;
class SampleInstrument;

class TracksPanel : public wxPanel {
public:
    TracksPanel(wxWindow* parent, Engine& engine);
    void update();
    int get_cursor_row() const;

public:
    void set_tab_index(int idx) { m_tab_index = idx; }

private:
    void on_zoom_in(wxCommandEvent& event);
    void on_zoom_out(wxCommandEvent& event);
    void on_view_all(wxCommandEvent& event);
    void on_view_sel(wxCommandEvent& event);
    void on_detach(wxCommandEvent& event);
    void on_cut(wxCommandEvent& event);
    void on_copy(wxCommandEvent& event);
    void on_paste(wxCommandEvent& event);
    void on_silence(wxCommandEvent& event);
    void on_insert_silence(wxCommandEvent& event);

    Engine& m_engine;
    int m_tab_index = -1;
    wxButton* m_zoom_in_btn;
    wxButton* m_zoom_out_btn;
    wxButton* m_view_all_btn;
    wxButton* m_view_sel_btn;
    wxButton* m_detach_btn;
    wxButton* m_cut_btn;
    wxButton* m_copy_btn;
    wxButton* m_paste_btn;
    wxButton* m_silence_btn;
    wxButton* m_insert_btn;
    class TracksView* m_tracks_view;
    class DetachedFrame* m_detached_frame = nullptr;

    wxDECLARE_EVENT_TABLE();
};

class TracksView : public wxScrolledWindow {
public:
    struct AudioRegion {
        size_t start_sample;
        size_t end_sample;
        bool is_overlapping;
    };

    TracksView(wxWindow* parent, wxWindowID id, Engine& engine);

    int get_total_ticks();
    void zoom_in();
    void zoom_out();
    void view_all();
    void view_selection();
    void update_view();
    void OnPaint(wxPaintEvent& event);
    void OnMouseDown(wxMouseEvent& event);
    void OnMouseDrag(wxMouseEvent& event);
    void OnMouseUp(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnMouseRightClick(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnContextMenu(wxContextMenuEvent& event);
    void draw_static(wxDC& dc);
    void draw_dynamic(wxDC& dc);
    void invalidate_static();
    void do_cut();
    void do_copy();
    void do_paste();
    void do_silence();
    void do_insert_silence();
    void do_retrigger_stretch_selection();
    void do_retrigger_stretch_whole_track();
    void do_undo();
    void do_redo();
    // Punch-in / punch-out marker helpers.
    void set_marker_in_at(int unscrolled_x);
    void set_marker_out_at(int unscrolled_x);
    void clear_punch_markers();
    int cursor_row() const { return m_sel_start_tick >= 0 ? m_sel_start_tick : 0; }

    // Called from TracksPanel::update() each timer tick during playback.
    // Scrolls horizontally to keep the playhead in view and repaints only the
    // necessary strip (old + new playhead positions) instead of the full canvas.
    void ensure_playhead_visible();
    void refresh_playhead_strip();
    // Reset playhead tracking so the next refresh_playhead_strip() starts fresh.
    void reset_playhead_tracking() { m_last_playhead_logical_x = -1; }

private:
    int tick_to_x(int tick);
    int x_to_tick(int x);
    int get_track_height(int track_idx) const;
    void toggle_track_minimize(int track_idx);
    int resolve_content_track_index(int display_track_idx) const;
    bool is_track_selected(int track_idx) const;
    std::vector<int> selected_tracks() const;
    void select_single_track(int track_idx);
    void select_track_range(int track_idx);
    void toggle_track_selection(int track_idx);
    std::vector<AudioRegion> detect_overlaps(const SampleInstrument* sampler);

    Engine& m_engine;
    double m_zoom = 10.0;
    int m_scroll_x = 0;
    bool m_needs_initial_view_all = true;

    int m_sel_start_tick = -1;
    int m_sel_end_tick = -1;
    int m_selected_track = -1;
    int m_selection_anchor_track = -1;
    std::set<int> m_selected_tracks;
    bool m_is_selecting = false;
    int m_recording_start_tick = -1;

    // Punch-in / punch-out markers (absolute song rows, -1 = unset).
    int m_marker_in = -1;
    int m_marker_out = -1;
    int m_last_right_click_x = 0; // unscrolled X at last right-click, for "Set marker here"

    // Waveform bitmap cache: key = (data_ptr, width, height, max_samples)
    std::map<std::tuple<uintptr_t, int, int, size_t>, wxBitmap> m_wave_cache;
    double m_cache_zoom = 0.0;
    // Logical X of the playhead in the previous frame (for strip-only repaint).
    int m_last_playhead_logical_x = -1;

    // Static background layer cache (everything except playhead + recording region).
    wxBitmap m_static_bmp;
    bool m_static_dirty = true;
    int m_static_vsx = -1;
    int m_static_vsy = -1;
    wxSize m_static_client_size{0, 0};
    // Playhead tick values cached by draw_static for use in draw_dynamic.
    int m_display_tick = 0;
    int m_play_tick = 0;

    wxDECLARE_EVENT_TABLE();
};

} // namespace tpanar_ns
