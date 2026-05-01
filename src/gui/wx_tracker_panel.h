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

#include <wx/wxprec.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/tglbtn.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <cstdint>
#include <unordered_map>

#include "../core/key_bindings.h"

enum {
    ID_ADD_PATTERN = 100,
    ID_REMOVE_PATTERN,
    ID_COPY_PATTERN,
    ID_FOLLOW_PLAYBACK,
    ID_DETACH
};

namespace tpanar_ns {

class Engine;
class TrackerView;

class TrackerPanel : public wxPanel {
public:
    TrackerPanel(wxWindow* parent, Engine& engine);

    void update_pattern_list();
    void update();
    void grab_focus();
    int get_cursor_row() const;
    TrackerView* get_tracker_view() const { return m_tracker; }
    void handle_note_action(Action action);

    void on_add_pattern(wxCommandEvent& event);
    void on_remove_pattern(wxCommandEvent& event);
    void on_copy_pattern(wxCommandEvent& event);
    void on_follow_playback(wxCommandEvent& event);
    void on_detach(wxCommandEvent& event);
public:
    void set_tab_index(int idx) { m_tab_index = idx; }

private:
    void on_pattern_select(wxCommandEvent& event);
    void on_pattern_length(wxCommandEvent& event);

    Engine& m_engine;
    TrackerView* m_tracker;
    wxScrolledWindow* m_main_scroll;
    wxScrolledWindow* m_pattern_scroll;
    wxPanel* m_pattern_list_container;

    wxButton* m_add_pattern_btn;
    wxButton* m_remove_pattern_btn;
    wxButton* m_copy_pattern_btn;
    wxToggleButton* m_follow_btn;
    wxButton* m_detach_btn;

    int m_selected_order_idx = 0;
    int m_tab_index = -1;
    size_t m_last_order_size = 0;
    size_t m_last_pattern_count = 0;
    bool m_follow_playback = false;
    std::unordered_map<long, wxWindow*> m_pattern_length_inputs;
    std::unordered_map<long, wxButton*> m_order_buttons;
    class DetachedFrame* m_detached_frame = nullptr;

    wxDECLARE_EVENT_TABLE();
};

} // namespace tpanar_ns
