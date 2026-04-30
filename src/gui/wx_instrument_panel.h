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
#include <wx/tglbtn.h>
#include <wx/listbox.h>
#include <wx/filectrl.h>
#include <wx/scrolwin.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/filedlg.h>
#include <wx/splitter.h>
#include <wx/timer.h>
#include <map>

namespace tpanar_ns {

class Engine;

class InstrumentPanel : public wxPanel {
public:
    InstrumentPanel(wxWindow* parent, Engine& engine);

    void update_instrument_list();
    void update_editor();
    void update_rec_inputs();
    void update_midi_input_choice();

    void cut();
    void copy();
    void paste();

    struct PluginInfo {
        std::string name;
        std::string path;
        int index;
        bool is_lv2;
    };

private:
    Engine& m_engine;
    int m_tab_index = -1;
    int m_selected_instrument = -1;
    int m_selected_sample = -1;
    std::map<int, PluginInfo> m_plugin_map;

    void on_inst_select_idx(int idx);

    wxSplitterWindow* m_main_splitter;
    wxSplitterWindow* m_left_splitter;

    wxPanel* m_left_panel;
    wxPanel* m_right_panel;
    wxPanel* m_inst_list_pane;
    wxPanel* m_file_browser_pane;

    wxButton* m_new_btn;
    wxButton* m_load_btn;
    wxButton* m_save_btn;
    wxButton* m_delete_btn;
    wxFileCtrl* m_file_browser;

    wxScrolledWindow* m_inst_scroll;

    wxPanel* m_sampler_editor;
    wxPanel* m_sample_list_grp;
    wxScrolledWindow* m_sample_scroll;
    wxButton* m_add_sample_btn;
    wxButton* m_sample_play_btn;
    wxButton* m_sample_stop_btn;
    wxButton* m_rec_btn;
    wxChoice* m_rec_mode_ch;
    wxCheckBox* m_mono_btn;
    wxCheckBox* m_preview_fx_check = nullptr;
    wxCheckBox* m_preview_loop_check = nullptr;
    wxChoice* m_rec_input_ch;
    class VUMeter* m_input_vu_l = nullptr;
    class VUMeter* m_input_vu_r = nullptr;
    wxTimer* m_vu_timer = nullptr;
    wxStaticText* m_rec_status_lbl = nullptr;
    class WaveformView* m_waveform_view;
    wxStaticText* m_sample_name_label = nullptr;

    wxPanel* m_sfont_editor;
    wxButton* m_sfont_load_btn;
    wxListBox* m_sfont_browser;
    wxSlider* m_sfont_vol_slider;

    wxPanel*   m_sfz_editor     = nullptr;
    wxButton*  m_sfz_load_btn   = nullptr;
    wxListBox* m_sfz_browser    = nullptr;  // group list
    wxSlider*  m_sfz_vol_slider = nullptr;
    wxStaticText* m_sfz_path_label = nullptr;

    wxPanel* m_plugin_editor;
    wxButton* m_plugin_scan_btn;
    wxListBox* m_plugin_browser;
    wxScrolledWindow* m_plugin_scroll;

    // ZynAddSubFX Editor members
    wxPanel* m_zyn_editor;
    wxChoice* m_zyn_bank_ch;
    wxListBox* m_zyn_preset_browser;
    wxButton* m_zyn_prev_btn;
    wxButton* m_zyn_next_btn;

    wxPanel* m_midi_editor;
    wxSpinCtrl* m_midi_channel;
    wxSpinCtrl* m_midi_program;
    wxChoice* m_midi_input_choice;

    wxPanel* m_voice_editor;
    wxChoice* m_voice_tts_mode_ch;
    wxChoice*   m_voice_language_ch;
    wxChoice*   m_voice_gender_ch;
    wxChoice*   m_voice_variant_ch;
    wxSlider* m_voice_speed_slider;
    wxSlider* m_voice_accent_slider;
    wxSpinCtrl* m_voice_phrase_idx;
    wxListBox* m_voice_phrase_list;
    wxTextCtrl* m_voice_phrase_text;
    wxButton* m_voice_process_btn;

    wxButton* m_zoom_in_btn;
    wxButton* m_zoom_out_btn;
    wxButton* m_view_all_btn;
    wxButton* m_view_sel_btn;
    wxChoice* m_view_mode_ch;
    wxChoice* m_sample_fmt_ch;

    wxButton* m_cut_btn;
    wxButton* m_copy_btn;
    wxButton* m_paste_btn;
    wxButton* m_silence_btn;
    wxButton* m_ins_sil_btn;
    wxButton* m_crop_btn;
    wxButton* m_resample_btn;
    wxButton* m_stretch_btn;
    wxButton* m_fade_in_lin_btn;
    wxButton* m_fade_in_log_btn;
    wxButton* m_fade_out_lin_btn;
    wxButton* m_fade_out_log_btn;
    wxButton* m_norm_btn;
    wxButton* m_vol_btn;
    wxButton* m_undo_btn;
    wxButton* m_redo_btn;
    wxSpinCtrlDouble* m_vol_input;

    wxButton* m_detach_btn;
    class DetachedFrame* m_detached_window = nullptr;

    void on_new(wxCommandEvent& event);
    void on_load(wxCommandEvent& event);
    void on_save(wxCommandEvent& event);
    void on_delete(wxCommandEvent& event);
    void on_inst_select(wxCommandEvent& event);
    void on_inst_name(wxCommandEvent& event);
    void on_inst_type(wxCommandEvent& event);
    void on_detach(wxCommandEvent& event);

    void on_add_sample(wxCommandEvent& event);
    void on_load_sample(wxCommandEvent& event);
    void on_remove_sample(wxCommandEvent& event);
    void on_move_sample_up(wxCommandEvent& event);
    void on_move_sample_down(wxCommandEvent& event);
    void on_save_sample(wxCommandEvent& event);
    void on_sample_play(wxCommandEvent& event);
    void on_sample_stop(wxCommandEvent& event);
    void on_record_sample(wxCommandEvent& event);
    void on_mono_toggle(wxCommandEvent& event);

    void on_zoom_in(wxCommandEvent& event);
    void on_zoom_out(wxCommandEvent& event);
    void on_view_all(wxCommandEvent& event);
    void on_view_sel(wxCommandEvent& event);
    void on_view_mode(wxCommandEvent& event);
    void on_sample_fmt(wxCommandEvent& event);

    void on_silence(wxCommandEvent& event);
    void on_insert_silence(wxCommandEvent& event);
    void on_crop(wxCommandEvent& event);
    void on_resample(wxCommandEvent& event);
    void on_time_stretch(wxCommandEvent& event);
    void on_fade_in_lin(wxCommandEvent& event);
    void on_fade_in_log(wxCommandEvent& event);
    void on_fade_out_lin(wxCommandEvent& event);
    void on_fade_out_log(wxCommandEvent& event);
    void on_normalize(wxCommandEvent& event);
    void on_adjust_vol(wxCommandEvent& event);
    void on_undo(wxCommandEvent& event);
    void on_redo(wxCommandEvent& event);

    void on_plugin_scan(wxCommandEvent& event);
    void on_plugin_select(wxCommandEvent& event);
    void on_plugin_param(wxScrollEvent& event);

    void on_zyn_bank(wxCommandEvent& event);
    void on_zyn_preset(wxCommandEvent& event);
    void on_zyn_prev(wxCommandEvent& event);
    void on_zyn_next(wxCommandEvent& event);

public:
    void set_tab_index(int idx) { m_tab_index = idx; }
};

} // namespace tpanar_ns
