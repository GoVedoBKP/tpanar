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
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/slider.h>
#include <wx/checkbox.h>
#include <wx/listbox.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>

namespace tpanar_ns {

class Engine;

class MixerPanel : public wxPanel {
public:
    MixerPanel(wxWindow* parent, Engine& engine);

    void update_mixer_ui();
    void update_meters();
    void update_effect_editor();
    void update_mastering_panel();

    static const int kSelectedMaster = -1;
    static const int kSelectedTrackBase = 0;
    static const int kSelectedBusBase = 1000;

private:
    Engine& m_engine;
    int m_tab_index = -1;
    int m_selected_track = -1;
    int m_selected_fx_slot = -1;
    bool m_is_updating_ui = false;

    wxSplitterWindow* m_splitter;
    wxPanel* m_upper_pane;
    wxPanel* m_lower_pane;

    wxScrolledWindow* m_track_group;
    wxPanel* m_master_group;
    wxSlider* m_master_gain;
    wxCheckBox* m_master_mute = nullptr;
    wxButton* m_master_sel_btn = nullptr;
    wxButton* m_detach_btn = nullptr;
    wxButton* m_mute_audio_btn = nullptr;
    wxButton* m_mute_notes_btn = nullptr;
    class VUMeter* m_master_meter_l;
    class VUMeter* m_master_meter_r;
    class AnalogVUMeter* m_master_analog_l;
    class AnalogVUMeter* m_master_analog_r;
    class SpectralView* m_master_spectral;
    wxScrolledWindow* m_mastering_ctrl_panel;
    class DetachedFrame* m_detached_frame = nullptr;

    std::vector<std::pair<class VUMeter*, class VUMeter*>> m_track_meters;
    std::vector<std::pair<class VUMeter*, class VUMeter*>> m_bus_meters;

    wxListBox* m_avail_fx_browser;
    wxScrolledWindow* m_fx_chain_group;
    wxScrolledWindow* m_fx_params_group;
    wxButton* m_load_chain_btn;
    wxButton* m_save_chain_btn;

    void on_detach(wxCommandEvent& event);

    void on_master_gain(wxCommandEvent& event);
    void on_master_mute(wxCommandEvent& event);
    void on_track_volume(wxCommandEvent& event);
    void on_track_pan(wxCommandEvent& event);
    void on_track_mute(wxCommandEvent& event);
    void on_track_solo(wxCommandEvent& event);
    void on_track_select(wxCommandEvent& event);

    void mute_all_audio_tracks(bool mute);
    void mute_all_note_tracks(bool mute);
    bool all_audio_tracks_muted() const;
    bool all_note_tracks_muted() const;
    void update_mute_group_buttons();
    
    void on_bus_volume(wxCommandEvent& event);
    void on_bus_pan(wxCommandEvent& event);
    void on_bus_mute(wxCommandEvent& event);
    void on_bus_select(wxCommandEvent& event);

    void on_add_fx(wxCommandEvent& event);
    void on_fx_up(wxCommandEvent& event);
    void on_fx_down(wxCommandEvent& event);
    void on_fx_remove(wxCommandEvent& event);
    void on_fx_bypass(wxCommandEvent& event);
    void on_save_chain(wxCommandEvent& event);
    void on_load_chain(wxCommandEvent& event);

public:
    void set_tab_index(int idx) { m_tab_index = idx; }

};

} // namespace tpanar_ns
