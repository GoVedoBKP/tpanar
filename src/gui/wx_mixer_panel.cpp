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

#include "wx_mixer_panel.h"
#include "theme.h"
#include "wx_vu_meter.h"
#include "wx_spectral_view.h"
#include "wx_analog_vu_meter.h"
#include "wx_detached_frame.h"
#include "../core/engine.h"
#include <wx/spinctrl.h>
#include <wx/app.h>
#include "../dsp/gain.h"
#include "../dsp/delay.h"
#include "../dsp/reverb.h"
#include "../dsp/limiter.h"
#include "../dsp/exciter.h"
#include "../dsp/phaser.h"
#include "../dsp/flanger.h"
#include "../dsp/echo.h"
#include "../dsp/compressor.h"
#include "../dsp/graphical_eq.h"
#include "../dsp/cabinet.h"
#include "../dsp/distortion.h"
#include "../dsp/chorus.h"
#include "../dsp/stereo_expander.h"
#include "../dsp/ring_modulator.h"
#include "../dsp/gate.h"
#include "../dsp/vocoder.h"
#include "../dsp/reference_matcher.h"

#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/scrolwin.h>
#include <wx/artprov.h>
#include <wx/filedlg.h>
#include <algorithm>
#include <memory>

namespace tpanar_ns {

MixerPanel::MixerPanel(wxWindow* parent, Engine& engine)
    : wxPanel(parent, wxID_ANY), m_engine(engine)
{
    m_selected_track = kSelectedMaster;
    m_selected_fx_slot = -1;

    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    // --- Left Side: Tracks and FX ---
    wxPanel* left_side = new wxPanel(this, wxID_ANY);
    wxBoxSizer* left_sizer = new wxBoxSizer(wxVERTICAL);

    m_splitter = new wxSplitterWindow(left_side, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_HORIZONTAL);
    m_splitter->SetMinimumPaneSize(100);

    // --- Upper Pane: Tracks ---
    m_upper_pane = new wxPanel(m_splitter, wxID_ANY);
    wxBoxSizer* upper_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_track_group = new wxScrolledWindow(m_upper_pane, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL);
    m_track_group->SetScrollRate(20, 0);
    upper_sizer->Add(m_track_group, 1, wxEXPAND | wxALL, 2);
    m_upper_pane->SetSizer(upper_sizer);

    // --- Lower Pane: Effects ---
    m_lower_pane = new wxPanel(m_splitter, wxID_ANY);
    wxBoxSizer* lower_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Available Effects List
    wxBoxSizer* avail_sizer = new wxBoxSizer(wxVERTICAL);
    avail_sizer->Add(new wxStaticText(m_lower_pane, wxID_ANY, "Available Effects"), 0, wxALL, 2);
    m_avail_fx_browser = new wxListBox(m_lower_pane, wxID_ANY, wxDefaultPosition, wxSize(150, -1));
    std::vector<std::string> fx_list = {
        "Gain", "Delay", "Reverb", "Limiter", "Exciter",
        "Phaser", "Flanger", "Echo", "Compressor",
        "Graphical EQ", "Cabinet", "Distortion",
        "Chorus", "Stereo Expander", "Ring Modulator", "Gate",
        "Reference Matcher", "Vocoder"
    };
    std::sort(fx_list.begin(), fx_list.end());
    for (const auto& fx : fx_list) m_avail_fx_browser->Append(fx);
    avail_sizer->Add(m_avail_fx_browser, 1, wxEXPAND | wxALL, 2);
    
    wxButton* add_btn = new wxButton(m_lower_pane, wxID_ANY, "Add Effect", wxDefaultPosition, wxSize(-1, 25));
    add_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_BUTTON, wxSize(16, 16)));
    add_btn->Bind(wxEVT_BUTTON, &MixerPanel::on_add_fx, this);
    avail_sizer->Add(add_btn, 0, wxEXPAND | wxALL, 2);
    lower_sizer->Add(avail_sizer, 0, wxEXPAND | wxALL, 2);

    // Effect Chain
    wxBoxSizer* chain_sizer = new wxBoxSizer(wxVERTICAL);
    chain_sizer->Add(new wxStaticText(m_lower_pane, wxID_ANY, "Effect Chain"), 0, wxALL, 2);
    m_fx_chain_group = new wxScrolledWindow(m_lower_pane, wxID_ANY, wxDefaultPosition, wxSize(250, -1), wxVSCROLL);
    m_fx_chain_group->SetScrollRate(0, 20);
    m_fx_chain_group->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_tracker_bg)); 
    chain_sizer->Add(m_fx_chain_group, 1, wxEXPAND | wxALL, 2);

    wxBoxSizer* chain_btns = new wxBoxSizer(wxHORIZONTAL);
    m_load_chain_btn = new wxButton(m_lower_pane, wxID_ANY, "Load", wxDefaultPosition, wxSize(-1, 25));
    m_load_chain_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_BUTTON, wxSize(16, 16)));
    m_save_chain_btn = new wxButton(m_lower_pane, wxID_ANY, "Save", wxDefaultPosition, wxSize(-1, 25));
    m_save_chain_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_BUTTON, wxSize(16, 16)));
    m_load_chain_btn->Bind(wxEVT_BUTTON, &MixerPanel::on_load_chain, this);
    m_save_chain_btn->Bind(wxEVT_BUTTON, &MixerPanel::on_save_chain, this);
    chain_btns->Add(m_load_chain_btn, 1, wxALL, 2);
    chain_btns->Add(m_save_chain_btn, 1, wxALL, 2);
    chain_sizer->Add(chain_btns, 0, wxEXPAND | wxALL, 2);
    lower_sizer->Add(chain_sizer, 0, wxEXPAND | wxALL, 2);

    // Effect Parameters
    wxBoxSizer* params_sizer = new wxBoxSizer(wxVERTICAL);
    params_sizer->Add(new wxStaticText(m_lower_pane, wxID_ANY, "Effect Controls"), 0, wxALL, 2);
    m_fx_params_group = new wxScrolledWindow(m_lower_pane, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_fx_params_group->SetScrollRate(0, 20);
    params_sizer->Add(m_fx_params_group, 1, wxEXPAND | wxALL, 2);
    lower_sizer->Add(params_sizer, 1, wxEXPAND | wxALL, 2);

    m_lower_pane->SetSizer(lower_sizer);

    m_splitter->SplitHorizontally(m_upper_pane, m_lower_pane);
    m_splitter->SetSashGravity(0.5);
    left_sizer->Add(m_splitter, 1, wxEXPAND);
    left_side->SetSizer(left_sizer);
    main_sizer->Add(left_side, 1, wxEXPAND | wxALL, 0);

    // --- Right Side: Master Pane ---
    m_master_group = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);
    wxBoxSizer* master_pane_sizer = new wxBoxSizer(wxVERTICAL);

    // Top row of Master Pane: Controls on left, Visuals on right
    wxBoxSizer* master_top_row = new wxBoxSizer(wxHORIZONTAL);

    // 1. Master Control Strip (Sized like a track channel)
    wxPanel* master_strip = new wxPanel(m_master_group, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);
    wxBoxSizer* master_strip_sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* master_label = new wxStaticText(master_strip, wxID_ANY, "Master");
    master_strip_sizer->Add(master_label, 0, wxALIGN_CENTER | wxALL, 2);

    wxBoxSizer* master_vol_meters = new wxBoxSizer(wxHORIZONTAL);
    m_master_gain = new wxSlider(master_strip, wxID_ANY, 100, 0, 200, wxDefaultPosition, wxSize(-1, 100), wxSL_VERTICAL | wxSL_INVERSE | wxSL_TICKS);
    m_master_gain->SetTickFreq(50);
    m_master_gain->SetValue((int)(m_engine.master_gain() * 100));
    m_master_gain->Bind(wxEVT_SLIDER, &MixerPanel::on_master_gain, this);
    master_vol_meters->Add(m_master_gain, 0, wxEXPAND | wxALL, 2);

    m_master_meter_l = new VUMeter(master_strip, wxID_ANY, m_engine, false);
    m_master_meter_r = new VUMeter(master_strip, wxID_ANY, m_engine, false);
    m_master_meter_l->SetMinSize(wxSize(10, 80));
    m_master_meter_r->SetMinSize(wxSize(10, 80));
    master_vol_meters->Add(m_master_meter_l, 1, wxEXPAND | wxALL, 1);
    master_vol_meters->Add(m_master_meter_r, 1, wxEXPAND | wxALL, 1);
    master_strip_sizer->Add(master_vol_meters, 1, wxEXPAND | wxALL, 2);

    // Master Pan (Parity with track strips)
    wxSlider* master_pan = new wxSlider(master_strip, wxID_ANY, 0, -100, 100, wxDefaultPosition, wxSize(60, -1), wxSL_HORIZONTAL | wxSL_TICKS);
    master_pan->SetTickFreq(50);
    master_pan->SetValue((int)(m_engine.m_master.pan() * 100));
    master_pan->Bind(wxEVT_SLIDER, [this](wxCommandEvent& ev) {
        float val = (float)ev.GetInt() / 100.0f;
        m_engine.m_master.set_pan(val);
    });
    master_strip_sizer->Add(master_pan, 0, wxALIGN_CENTER | wxALL, 2);

    // Mute
    m_master_mute = new wxCheckBox(master_strip, wxID_ANY, "M");
    m_master_mute->SetValue(m_engine.m_master.muted());
    m_master_mute->Bind(wxEVT_CHECKBOX, &MixerPanel::on_master_mute, this);
    master_strip_sizer->Add(m_master_mute, 0, wxALIGN_CENTER | wxALL, 2);

    // Select Button
    m_master_sel_btn = new wxButton(master_strip, wxID_ANY, "SEL", wxDefaultPosition, wxSize(60, 25));
    m_master_sel_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_BUTTON, wxSize(14, 14)));
    m_master_sel_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) {
        this->CallAfter([this]() {
            m_selected_track = kSelectedMaster;
            m_selected_fx_slot = -1;
            update_mixer_ui();
            update_effect_editor();
        });
    });
    master_strip_sizer->Add(m_master_sel_btn, 0, wxALIGN_CENTER | wxALL, 2);

    // Detach Button
    m_detach_btn = new wxButton(master_strip, wxID_ANY, "[]", wxDefaultPosition, wxSize(30, 25));
    m_detach_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FULL_SCREEN, wxART_BUTTON, wxSize(14, 14)));
    m_detach_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) {
        this->on_detach(ev);
    });
    master_strip_sizer->Add(m_detach_btn, 0, wxALIGN_CENTER | wxALL, 2);

    master_strip->SetSizer(master_strip_sizer);
    master_top_row->Add(master_strip, 0, wxEXPAND | wxALL, 2);

    // Group mute buttons (below the master strip, spanning mute-all for audio/notes).
    wxBoxSizer* group_mute_sizer = new wxBoxSizer(wxVERTICAL);
    group_mute_sizer->Add(new wxStaticText(master_strip, wxID_ANY, ""), 0, wxALL, 4); // spacer label
    m_mute_audio_btn = new wxButton(master_strip, wxID_ANY, "Mute Audio", wxDefaultPosition, wxSize(90, 26));
    m_mute_audio_btn->SetToolTip("Mute / unmute all audio tracks");
    m_mute_audio_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        bool new_state = !all_audio_tracks_muted();
        mute_all_audio_tracks(new_state);
        update_mute_group_buttons();
        update_mixer_ui();
    });
    master_strip_sizer->Add(m_mute_audio_btn, 0, wxALIGN_CENTER | wxALL, 2);

    m_mute_notes_btn = new wxButton(master_strip, wxID_ANY, "Mute Notes", wxDefaultPosition, wxSize(90, 26));
    m_mute_notes_btn->SetToolTip("Mute / unmute all notation tracks");
    m_mute_notes_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        bool new_state = !all_note_tracks_muted();
        mute_all_note_tracks(new_state);
        update_mute_group_buttons();
        update_mixer_ui();
    });
    master_strip_sizer->Add(m_mute_notes_btn, 0, wxALIGN_CENTER | wxALL, 2);

    // 2. Visual Meters (Taking remaining space)
    wxPanel* master_visuals = new wxPanel(m_master_group, wxID_ANY);
    wxBoxSizer* master_visuals_sizer = new wxBoxSizer(wxVERTICAL);

    // Upper part: analog style dB meters (resized)
    wxBoxSizer* analog_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_master_analog_l = new AnalogVUMeter(master_visuals, wxID_ANY, m_engine);
    m_master_analog_r = new AnalogVUMeter(master_visuals, wxID_ANY, m_engine);
    m_master_analog_l->SetMinSize(wxSize(140, 60)); 
    m_master_analog_r->SetMinSize(wxSize(140, 60));
    analog_sizer->Add(m_master_analog_l, 1, wxEXPAND | wxALL, 2);
    analog_sizer->Add(m_master_analog_r, 1, wxEXPAND | wxALL, 2);
    master_visuals_sizer->Add(analog_sizer, 0, wxEXPAND | wxALL, 2);

    // Down part: graphical spectrum analyzer
    m_master_spectral = new SpectralView(master_visuals, wxID_ANY, m_engine);
    m_master_spectral->SetMinSize(wxSize(-1, 80));
    master_visuals_sizer->Add(m_master_spectral, 0, wxEXPAND | wxALL, 2);

    // Mastering Controls
    m_mastering_ctrl_panel = new wxScrolledWindow(master_visuals, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_mastering_ctrl_panel->SetScrollRate(0, 20);
    m_mastering_ctrl_panel->SetMinSize(wxSize(350, -1));
    master_visuals_sizer->Add(m_mastering_ctrl_panel, 1, wxEXPAND | wxALL, 2);

    master_visuals->SetSizer(master_visuals_sizer);
    master_top_row->Add(master_visuals, 1, wxEXPAND | wxALL, 2);

    master_pane_sizer->Add(master_top_row, 1, wxEXPAND | wxALL, 5);

    m_master_group->SetSizer(master_pane_sizer);
    main_sizer->Add(m_master_group, 0, wxEXPAND | wxALL, 2); // Set proportion to 0

    SetSizer(main_sizer);

    update_mixer_ui();
    update_effect_editor();
    update_mastering_panel();
}

void MixerPanel::update_mastering_panel() {
    m_is_updating_ui = true;
    m_mastering_ctrl_panel->DestroyChildren();
    
    wxBoxSizer* params_sizer = new wxBoxSizer(wxVERTICAL);

    // --- Mastering UI ---
    wxStaticText* m_header = new wxStaticText(m_mastering_ctrl_panel, wxID_ANY, "Mastering Controls");
    wxFont h_font = m_header->GetFont(); h_font.SetWeight(wxFONTWEIGHT_BOLD); m_header->SetFont(h_font);
    params_sizer->Add(m_header, 0, wxALL, 5);

    auto& filter = m_engine.m_master.mastering_filter();
    auto& style = m_engine.m_master.mastering_styles();

    // Helper to add sliders
    auto add_m_slider = [&](const wxString& label, float min, float max, float val, std::function<void(float)> setter) {
        wxBoxSizer* s_row = new wxBoxSizer(wxHORIZONTAL);
        s_row->Add(new wxStaticText(m_mastering_ctrl_panel, wxID_ANY, label, wxDefaultPosition, wxSize(100, -1)), 0, wxALL, 5);
        wxSlider* sl = new wxSlider(m_mastering_ctrl_panel, wxID_ANY, (int)(((val - min) / (max - min)) * 1000), 0, 1000, wxDefaultPosition, wxSize(200, -1));
        sl->Bind(wxEVT_SLIDER, [this, min, max, setter](wxCommandEvent& ev) {
            if (m_is_updating_ui) return;
            setter(min + ((float)ev.GetInt() / 1000.0f) * (max - min));
        });
        s_row->Add(sl, 1, wxEXPAND | wxALL, 5);
        params_sizer->Add(s_row, 0, wxEXPAND | wxALL, 2);
    };

    // Style Choice
    wxBoxSizer* st_row = new wxBoxSizer(wxHORIZONTAL);
    st_row->Add(new wxStaticText(m_mastering_ctrl_panel, wxID_ANY, "Master Style:", wxDefaultPosition, wxSize(100, -1)), 0, wxALL, 5);
    wxChoice* st_choice = new wxChoice(m_mastering_ctrl_panel, wxID_ANY);
    for (const auto& s : style.get_presets()) st_choice->Append(s);
    st_choice->SetStringSelection(style.current_preset());
    st_choice->Bind(wxEVT_CHOICE, [this, &style](wxCommandEvent& ev) {
        if (m_is_updating_ui) return;
        style.load_preset(ev.GetString().ToStdString());
        CallAfter([this]() { update_mastering_panel(); });
    });
    st_row->Add(st_choice, 1, wxEXPAND | wxALL, 5);
    params_sizer->Add(st_row, 0, wxEXPAND | wxALL, 2);

    params_sizer->Add(new wxStaticLine(m_mastering_ctrl_panel), 0, wxEXPAND | wxALL, 5);
    params_sizer->Add(new wxStaticText(m_mastering_ctrl_panel, wxID_ANY, "Filters"), 0, wxALL, 5);

    wxCheckBox* hpf_en = new wxCheckBox(m_mastering_ctrl_panel, wxID_ANY, "High Pass Filter");
    hpf_en->SetValue(filter.hpf_enabled);
    hpf_en->Bind(wxEVT_CHECKBOX, [this, &filter](wxCommandEvent& ev) { filter.hpf_enabled = ev.IsChecked(); });
    params_sizer->Add(hpf_en, 0, wxALL, 5);
    add_m_slider("HPF Freq", 20.0f, 500.0f, filter.hpf_freq, [&filter](float v){ filter.hpf_freq = v; filter.update_filters(); });

    wxCheckBox* lpf_en = new wxCheckBox(m_mastering_ctrl_panel, wxID_ANY, "Low Pass Filter");
    lpf_en->SetValue(filter.lpf_enabled);
    lpf_en->Bind(wxEVT_CHECKBOX, [this, &filter](wxCommandEvent& ev) { filter.lpf_enabled = ev.IsChecked(); });
    params_sizer->Add(lpf_en, 0, wxALL, 5);
    add_m_slider("LPF Freq", 5000.0f, 20000.0f, filter.lpf_freq, [&filter](float v){ filter.lpf_freq = v; filter.update_filters(); });

    m_mastering_ctrl_panel->SetSizer(params_sizer);
    m_mastering_ctrl_panel->Layout();
    m_mastering_ctrl_panel->FitInside();
    m_is_updating_ui = false;
}

void MixerPanel::update_mixer_ui() {
    m_track_group->DestroyChildren();
    m_track_meters.clear();
    m_bus_meters.clear();

    size_t num_tracks = m_engine.track_count();
    size_t num_buses = m_engine.bus_count();
    
    wxBoxSizer* tracks_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Master Selection Button update
    if (m_selected_track == kSelectedMaster) {
        m_master_sel_btn->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_selection_color));
        m_master_sel_btn->SetForegroundColour(ThemeManager::contrastColor(m_engine.m_selection_color));
    } else {
        m_master_sel_btn->SetBackgroundColour(wxNullColour);
        m_master_sel_btn->SetForegroundColour(wxNullColour);
    }

    // --- Tracks ---
    for (size_t i = 0; i < num_tracks; ++i) {
        wxPanel* track_panel = new wxPanel(m_track_group, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);
        wxBoxSizer* track_sizer = new wxBoxSizer(wxVERTICAL);

        wxString track_name;
        track_name.Printf("Track %zu", i + 1);
        wxStaticText* label = new wxStaticText(track_panel, wxID_ANY, track_name);
        track_sizer->Add(label, 0, wxALIGN_CENTER | wxALL, 2);

        wxBoxSizer* track_controls = new wxBoxSizer(wxHORIZONTAL);

        // Volume Slider
        wxSlider* vol = new wxSlider(track_panel, wxID_ANY, 100, 0, 100, wxDefaultPosition, wxSize(-1, 100), wxSL_VERTICAL | wxSL_INVERSE | wxSL_TICKS);
        vol->SetTickFreq(25);
        vol->SetValue((int)(m_engine.track(i).volume() * 100));
        vol->Bind(wxEVT_SLIDER, [this, i](wxCommandEvent& ev) {
            float val = (float)ev.GetInt() / 100.0f;
            m_engine.track(i).set_volume(val);
            m_engine.mark_dirty();
        });
        track_controls->Add(vol, 0, wxEXPAND | wxALL, 2);

        // Meters
        VUMeter* meter_l = new VUMeter(track_panel, wxID_ANY, m_engine, false);
        VUMeter* meter_r = new VUMeter(track_panel, wxID_ANY, m_engine, false);
        meter_l->SetMinSize(wxSize(10, 80));
        meter_r->SetMinSize(wxSize(10, 80));
        track_controls->Add(meter_l, 1, wxEXPAND | wxALL, 1);
        track_controls->Add(meter_r, 1, wxEXPAND | wxALL, 1);
        m_track_meters.push_back({meter_l, meter_r});

        track_sizer->Add(track_controls, 1, wxEXPAND | wxALL, 2);

        // Pan Slider
        wxSlider* pan = new wxSlider(track_panel, wxID_ANY, 0, -100, 100, wxDefaultPosition, wxSize(60, -1), wxSL_HORIZONTAL | wxSL_TICKS);
        pan->SetTickFreq(50);
        pan->SetValue((int)(m_engine.track(i).get_pan() * 100));
        pan->Bind(wxEVT_SLIDER, [this, i](wxCommandEvent& ev) {
            float val = (float)ev.GetInt() / 100.0f;
            m_engine.track(i).set_pan(val);
            m_engine.mark_dirty();
        });
        track_sizer->Add(pan, 0, wxALIGN_CENTER | wxALL, 2);

        // Mute/Solo
        wxBoxSizer* ms_sizer = new wxBoxSizer(wxHORIZONTAL);
        wxCheckBox* mute = new wxCheckBox(track_panel, wxID_ANY, "M");
        mute->SetValue(m_engine.track(i).muted());
        mute->Bind(wxEVT_CHECKBOX, [this, i](wxCommandEvent& ev) {
            m_engine.track(i).set_mute(ev.IsChecked());
            m_engine.mark_dirty();
        });
        ms_sizer->Add(mute, 0, wxALL, 2);

        wxCheckBox* solo = new wxCheckBox(track_panel, wxID_ANY, "S");
        solo->SetValue(m_engine.track(i).solo());
        solo->Bind(wxEVT_CHECKBOX, [this, i](wxCommandEvent& ev) {
            m_engine.track(i).set_solo(ev.IsChecked());
            m_engine.mark_dirty();
        });
        ms_sizer->Add(solo, 0, wxALL, 2);
        track_sizer->Add(ms_sizer, 0, wxALIGN_CENTER | wxALL, 2);

        // Select Button
        wxButton* sel_btn = new wxButton(track_panel, wxID_ANY, "SEL", wxDefaultPosition, wxSize(60, 25));
        sel_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_BUTTON, wxSize(14, 14)));
        if (m_selected_track == kSelectedTrackBase + (int)i) {
            sel_btn->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_selection_color));
            sel_btn->SetForegroundColour(ThemeManager::contrastColor(m_engine.m_selection_color));
        } else {
            sel_btn->SetBackgroundColour(wxNullColour);
            sel_btn->SetForegroundColour(wxNullColour);
        }
        sel_btn->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent& ev) {
            this->CallAfter([this, i]() {
                m_selected_track = kSelectedTrackBase + (int)i;
                m_selected_fx_slot = -1;
                update_mixer_ui();
                update_effect_editor();
            });
        });
        track_sizer->Add(sel_btn, 0, wxALIGN_CENTER | wxALL, 2);

        track_panel->SetSizer(track_sizer);
        tracks_sizer->Add(track_panel, 0, wxEXPAND | wxALL, 2);
    }

    // --- Buses (skip master bus at index 0) ---
    for (size_t i = 1; i < num_buses; ++i) {
        wxPanel* bus_panel = new wxPanel(m_track_group, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);
        wxBoxSizer* bus_sizer = new wxBoxSizer(wxVERTICAL);

        wxString bus_name;
        bus_name.Printf("Bus %zu", i);
        wxStaticText* label = new wxStaticText(bus_panel, wxID_ANY, bus_name);
        bus_sizer->Add(label, 0, wxALIGN_CENTER | wxALL, 2);

        wxBoxSizer* bus_controls = new wxBoxSizer(wxHORIZONTAL);

        // Volume Slider
        wxSlider* vol = new wxSlider(bus_panel, wxID_ANY, 100, 0, 100, wxDefaultPosition, wxSize(-1, 100), wxSL_VERTICAL | wxSL_INVERSE | wxSL_TICKS);
        vol->SetTickFreq(25);
        vol->SetValue((int)(m_engine.bus(i).volume() * 100));
        vol->Bind(wxEVT_SLIDER, [this, i](wxCommandEvent& ev) {
            float val = (float)ev.GetInt() / 100.0f;
            m_engine.bus(i).set_volume(val);
        });
        bus_controls->Add(vol, 0, wxEXPAND | wxALL, 2);

        // Meters
        VUMeter* meter_l = new VUMeter(bus_panel, wxID_ANY, m_engine, false);
        VUMeter* meter_r = new VUMeter(bus_panel, wxID_ANY, m_engine, false);
        meter_l->SetMinSize(wxSize(10, 80));
        meter_r->SetMinSize(wxSize(10, 80));
        bus_controls->Add(meter_l, 1, wxEXPAND | wxALL, 1);
        bus_controls->Add(meter_r, 1, wxEXPAND | wxALL, 1);
        m_bus_meters.push_back({meter_l, meter_r});

        bus_sizer->Add(bus_controls, 1, wxEXPAND | wxALL, 2);

        // Pan Slider
        wxSlider* pan = new wxSlider(bus_panel, wxID_ANY, 0, -100, 100, wxDefaultPosition, wxSize(60, -1), wxSL_HORIZONTAL | wxSL_TICKS);
        pan->SetTickFreq(50);
        pan->SetValue((int)(m_engine.bus(i).pan() * 100));
        pan->Bind(wxEVT_SLIDER, [this, i](wxCommandEvent& ev) {
            float val = (float)ev.GetInt() / 100.0f;
            m_engine.bus(i).set_pan(val);
        });
        bus_sizer->Add(pan, 0, wxALIGN_CENTER | wxALL, 2);

        // Mute
        wxCheckBox* mute = new wxCheckBox(bus_panel, wxID_ANY, "M");
        mute->SetValue(m_engine.bus(i).muted());
        mute->Bind(wxEVT_CHECKBOX, [this, i](wxCommandEvent& ev) {
            m_engine.bus(i).set_mute(ev.IsChecked());
        });
        bus_sizer->Add(mute, 0, wxALIGN_CENTER | wxALL, 2);

        // Select Button
        wxButton* sel_btn = new wxButton(bus_panel, wxID_ANY, "SEL", wxDefaultPosition, wxSize(60, 25));
        sel_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_BUTTON, wxSize(14, 14)));
        if (m_selected_track == kSelectedBusBase + (int)i) {
            sel_btn->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_selection_color));
            sel_btn->SetForegroundColour(ThemeManager::contrastColor(m_engine.m_selection_color));
        } else {
            sel_btn->SetBackgroundColour(wxNullColour);
            sel_btn->SetForegroundColour(wxNullColour);
        }
        sel_btn->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent& ev) {
            this->CallAfter([this, i]() {
                m_selected_track = kSelectedBusBase + (int)i;
                m_selected_fx_slot = -1;
                update_mixer_ui();
                update_effect_editor();
            });
        });
        bus_sizer->Add(sel_btn, 0, wxALIGN_CENTER | wxALL, 2);

        bus_panel->SetSizer(bus_sizer);
        tracks_sizer->Add(bus_panel, 0, wxEXPAND | wxALL, 2);
    }

    m_track_group->SetSizer(tracks_sizer);
    m_track_group->FitInside(); // Update scrollbars
    update_mute_group_buttons();
}

void MixerPanel::update_meters() {
    if (m_master_meter_l) m_master_meter_l->level(m_engine.master_meter_l());
    if (m_master_meter_r) m_master_meter_r->level(m_engine.master_meter_r());
    if (m_master_analog_l) m_master_analog_l->level(m_engine.master_meter_l());
    if (m_master_analog_r) m_master_analog_r->level(m_engine.master_meter_r());
    if (m_master_spectral) m_master_spectral->update();

    for (size_t i = 0; i < m_engine.track_count() && i < m_track_meters.size(); ++i) {
        auto& meters = m_track_meters[i];
        if (meters.first) meters.first->level(m_engine.track(i).meter_level_l());
        if (meters.second) meters.second->level(m_engine.track(i).meter_level_r());
    }

    for (size_t i = 1; i < m_engine.bus_count() && (i - 1) < m_bus_meters.size(); ++i) {
        auto& meters = m_bus_meters[i - 1];
        if (meters.first) meters.first->level(m_engine.bus(i).meter_l());
        if (meters.second) meters.second->level(m_engine.bus(i).meter_r());
    }
}

void MixerPanel::update_effect_editor() {
    m_is_updating_ui = true;
    m_fx_chain_group->DestroyChildren();
    m_fx_params_group->DestroyChildren();

    bool is_master = (m_selected_track == kSelectedMaster);
    bool is_bus = (m_selected_track >= kSelectedBusBase);
    bool is_track = (m_selected_track >= kSelectedTrackBase && m_selected_track < kSelectedBusBase);

    int bus_idx = is_bus ? (m_selected_track - kSelectedBusBase) : -1;
    int track_idx = is_track ? (m_selected_track - kSelectedTrackBase) : -1;

    wxBoxSizer* params_sizer = new wxBoxSizer(wxVERTICAL);

    if (is_master) {
        params_sizer->Add(new wxStaticText(m_fx_params_group, wxID_ANY, "Styles and Filters are in the master pane on the right."), 0, wxALL, 5);
        params_sizer->Add(new wxStaticLine(m_fx_params_group), 0, wxEXPAND | wxALL, 5);

        // --- Reference Matcher Section ---
        wxStaticText* rm_header = new wxStaticText(m_fx_params_group, wxID_ANY, "Reference Matcher");
        wxFont rm_font = rm_header->GetFont(); rm_font.SetWeight(wxFONTWEIGHT_BOLD); rm_header->SetFont(rm_font);
        params_sizer->Add(rm_header, 0, wxALL, 5);

        auto& matcher = m_engine.m_master.reference_matcher();

        // Helper to add sliders
        auto add_rm_slider = [&](const wxString& label, float min, float max, float val, std::function<void(float)> setter) {
            wxBoxSizer* s_row = new wxBoxSizer(wxHORIZONTAL);
            s_row->Add(new wxStaticText(m_fx_params_group, wxID_ANY, label, wxDefaultPosition, wxSize(100, -1)), 0, wxALL, 5);
            wxSlider* sl = new wxSlider(m_fx_params_group, wxID_ANY, (int)(((val - min) / (max - min)) * 1000), 0, 1000, wxDefaultPosition, wxSize(200, -1));
            sl->Bind(wxEVT_SLIDER, [this, min, max, setter](wxCommandEvent& ev) {
                if (m_is_updating_ui) return;
                setter(min + ((float)ev.GetInt() / 1000.0f) * (max - min));
            });
            s_row->Add(sl, 1, wxEXPAND | wxALL, 5);
            params_sizer->Add(s_row, 0, wxEXPAND | wxALL, 2);
        };

        // Reference file info
        wxBoxSizer* ref_row = new wxBoxSizer(wxHORIZONTAL);
        ref_row->Add(new wxStaticText(m_fx_params_group, wxID_ANY, "Reference:", wxDefaultPosition, wxSize(100, -1)), 0, wxALL, 5);
        if (matcher.is_reference_loaded()) {
            wxString ref_name = matcher.get_reference_path();
            if (ref_name.Len() > 40) ref_name = "..." + ref_name.Right(40);
            ref_row->Add(new wxStaticText(m_fx_params_group, wxID_ANY, ref_name), 1, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        } else {
            ref_row->Add(new wxStaticText(m_fx_params_group, wxID_ANY, "(No reference loaded)"), 1, wxALL | wxALIGN_CENTER_VERTICAL, 5);
        }
        wxButton* load_ref_btn = new wxButton(m_fx_params_group, wxID_ANY, "Load");
        load_ref_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_BUTTON, wxSize(14, 14)));
        load_ref_btn->Bind(wxEVT_BUTTON, [this, &matcher](wxCommandEvent& ev) {
            wxFileDialog dlg(this, "Load Reference Track", "", "", "Audio files (*.wav;*.flac;*.ogg;*.mp3)|*.wav;*.flac;*.ogg;*.mp3", wxFD_OPEN);
            if (dlg.ShowModal() == wxID_OK) {
                if (matcher.load_reference(dlg.GetPath().ToStdString())) {
                    update_effect_editor();
                }
            }
        });
        ref_row->Add(load_ref_btn, 0, wxALL, 2);
        params_sizer->Add(ref_row, 0, wxEXPAND | wxALL, 2);

        // Presets
        wxBoxSizer* rm_preset_row = new wxBoxSizer(wxHORIZONTAL);
        rm_preset_row->Add(new wxStaticText(m_fx_params_group, wxID_ANY, "Preset:", wxDefaultPosition, wxSize(100, -1)), 0, wxALL, 5);
        wxChoice* rm_preset_choice = new wxChoice(m_fx_params_group, wxID_ANY);
        for (const auto& p : matcher.get_presets()) rm_preset_choice->Append(p);
        rm_preset_choice->SetStringSelection(matcher.current_preset());
        rm_preset_choice->Bind(wxEVT_CHOICE, [this, &matcher](wxCommandEvent& ev) {
            if (m_is_updating_ui) return;
            matcher.load_preset(ev.GetString().ToStdString());
            CallAfter([this]() { update_effect_editor(); });
        });
        rm_preset_row->Add(rm_preset_choice, 1, wxEXPAND | wxALL, 5);
        params_sizer->Add(rm_preset_row, 0, wxEXPAND | wxALL, 2);

        // Match controls
        wxBoxSizer* match_row = new wxBoxSizer(wxHORIZONTAL);
        wxCheckBox* match_rms = new wxCheckBox(m_fx_params_group, wxID_ANY, "Match RMS");
        match_rms->SetValue(matcher.get_match_rms());
        match_rms->Bind(wxEVT_CHECKBOX, [this, &matcher](wxCommandEvent& ev) { matcher.set_match_rms(ev.IsChecked()); });
        match_row->Add(match_rms, 0, wxALL, 5);

        wxCheckBox* match_eq = new wxCheckBox(m_fx_params_group, wxID_ANY, "Match EQ");
        match_eq->SetValue(matcher.get_match_eq());
        match_eq->Bind(wxEVT_CHECKBOX, [this, &matcher](wxCommandEvent& ev) { matcher.set_match_eq(ev.IsChecked()); });
        match_row->Add(match_eq, 0, wxALL, 5);

        wxCheckBox* match_width = new wxCheckBox(m_fx_params_group, wxID_ANY, "Match Width");
        match_width->SetValue(matcher.get_match_width());
        match_width->Bind(wxEVT_CHECKBOX, [this, &matcher](wxCommandEvent& ev) { matcher.set_match_width(ev.IsChecked()); });
        match_row->Add(match_width, 0, wxALL, 5);

        wxCheckBox* match_dyn = new wxCheckBox(m_fx_params_group, wxID_ANY, "Match Dynamics");
        match_dyn->SetValue(matcher.get_match_dynamics());
        match_dyn->Bind(wxEVT_CHECKBOX, [this, &matcher](wxCommandEvent& ev) { matcher.set_match_dynamics(ev.IsChecked()); });
        match_row->Add(match_dyn, 0, wxALL, 5);
        params_sizer->Add(match_row, 0, wxEXPAND | wxALL, 2);

        // Mix slider
        add_rm_slider("Mix", 0.0f, 1.0f, matcher.get_mix(), [&matcher](float v){ matcher.set_mix(v); });

        // Target RMS
        add_rm_slider("Target RMS (dB)", -24.0f, -6.0f, matcher.get_target_rms(), [&matcher](float v){ matcher.set_target_rms(v); });

        // Limiter controls
        wxBoxSizer* limit_row = new wxBoxSizer(wxHORIZONTAL);
        wxCheckBox* limit_en = new wxCheckBox(m_fx_params_group, wxID_ANY, "Limiter");
        limit_en->SetValue(matcher.get_limit());
        limit_en->Bind(wxEVT_CHECKBOX, [this, &matcher](wxCommandEvent& ev) { matcher.set_limit(ev.IsChecked()); });
        limit_row->Add(limit_en, 0, wxALL, 5);

        add_rm_slider("Ceiling", 0.8f, 1.0f, matcher.get_limit_threshold(), [&matcher](float v){ matcher.set_limit_threshold(v); });
        params_sizer->Add(limit_row, 0, wxEXPAND | wxALL, 2);
    } else {
        // --- Regular Track/Bus UI ---
        auto get_fx = [&](size_t idx) -> DSP* {
            if (is_bus && bus_idx >= 0 && bus_idx < (int)m_engine.bus_count())
                return m_engine.bus(bus_idx).get_effect(idx);
            if (is_track && track_idx >= 0 && track_idx < (int)m_engine.track_count())
                return m_engine.track(track_idx).get_effect(idx);
            return nullptr;
        };

        wxBoxSizer* chain_sizer = new wxBoxSizer(wxVERTICAL);
        for (size_t i = 0; i < MAX_INSERTS; ++i) {
            DSP* dsp = get_fx(i);
            if (dsp) {
                wxPanel* row = new wxPanel(m_fx_chain_group, wxID_ANY);
                row->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_tracker_lpb_highlight));
                wxBoxSizer* row_sizer = new wxBoxSizer(wxHORIZONTAL);

                wxCheckBox* bypass = new wxCheckBox(row, wxID_ANY, "");
                bypass->SetValue(!dsp->is_bypassed());
                bypass->Bind(wxEVT_CHECKBOX, [this, i, is_bus, is_track, track_idx, bus_idx](wxCommandEvent& ev) {
                    DSP* d = nullptr;
                    if (is_bus && bus_idx >= 0 && bus_idx < (int)m_engine.bus_count()) d = m_engine.bus(bus_idx).get_effect(i);
                    else if (is_track && track_idx >= 0 && track_idx < (int)m_engine.track_count()) d = m_engine.track(track_idx).get_effect(i);
                    if(d) d->set_bypass(!ev.IsChecked());
                });
                row_sizer->Add(bypass, 0, wxALL, 2);

                wxButton* sel_btn = new wxButton(row, wxID_ANY, dsp->name(), wxDefaultPosition, wxSize(100, 25));
                if ((int)i == m_selected_fx_slot) {
                    sel_btn->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_selection_color));
                    sel_btn->SetForegroundColour(ThemeManager::contrastColor(m_engine.m_selection_color));
                }
                sel_btn->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent& ev) {
                    m_selected_fx_slot = (int)i;
                    update_effect_editor();
                });
                row_sizer->Add(sel_btn, 1, wxALL, 2);

                wxButton* up = new wxButton(row, wxID_ANY, "^", wxDefaultPosition, wxSize(25, 25));
                up->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_UP, wxART_BUTTON, wxSize(14, 14)));
                up->Bind(wxEVT_BUTTON, [this, i, is_bus, is_track, track_idx, bus_idx](wxCommandEvent& ev) {
                    if (is_bus && bus_idx >= 0 && bus_idx < (int)m_engine.bus_count()) m_engine.bus(bus_idx).move_effect_up(i);
                    else if (is_track && track_idx >= 0 && track_idx < (int)m_engine.track_count()) m_engine.track(track_idx).move_effect_up(i);
                    update_effect_editor();
                });
                row_sizer->Add(up, 0, wxALL, 1);

                wxButton* down = new wxButton(row, wxID_ANY, "v", wxDefaultPosition, wxSize(25, 25));
                down->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_DOWN, wxART_BUTTON, wxSize(14, 14)));
                down->Bind(wxEVT_BUTTON, [this, i, is_bus, is_track, track_idx, bus_idx](wxCommandEvent& ev) {
                    if (is_bus && bus_idx >= 0 && bus_idx < (int)m_engine.bus_count()) m_engine.bus(bus_idx).move_effect_down(i);
                    else if (is_track && track_idx >= 0 && track_idx < (int)m_engine.track_count()) m_engine.track(track_idx).move_effect_down(i);
                    update_effect_editor();
                });
                row_sizer->Add(down, 0, wxALL, 1);

                wxButton* rem = new wxButton(row, wxID_ANY, "X", wxDefaultPosition, wxSize(25, 25));
                rem->SetBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_BUTTON, wxSize(14, 14)));
                rem->SetForegroundColour(ThemeManager::toWxColour(m_engine.m_warning_color));
                rem->Bind(wxEVT_BUTTON, [this, i, is_bus, is_track, track_idx, bus_idx](wxCommandEvent& ev) {
                    if (is_bus && bus_idx >= 0 && bus_idx < (int)m_engine.bus_count()) m_engine.bus(bus_idx).remove_effect(i);
                    else if (is_track && track_idx >= 0 && track_idx < (int)m_engine.track_count()) m_engine.track(track_idx).remove_effect(i);
                    m_selected_fx_slot = -1;
                    update_effect_editor();
                });
                row_sizer->Add(rem, 0, wxALL, 1);

                row->SetSizer(row_sizer);
                chain_sizer->Add(row, 0, wxEXPAND | wxALL, 1);
            }
        }
        m_fx_chain_group->SetSizer(chain_sizer);
        m_fx_chain_group->FitInside();

        DSP* dsp = get_fx(m_selected_fx_slot);
        if (dsp) {
            wxStaticText* header = new wxStaticText(m_fx_params_group, wxID_ANY, "Editing: " + dsp->name());
            wxFont font = header->GetFont(); font.SetWeight(wxFONTWEIGHT_BOLD); header->SetFont(font);
            params_sizer->Add(header, 0, wxALL, 5);

            // Presets Dropdown
            auto presets = dsp->get_presets();
            wxChoice* p_choice = nullptr;
            if (!presets.empty()) {
                wxBoxSizer* p_row = new wxBoxSizer(wxHORIZONTAL);
                p_row->Add(new wxStaticText(m_fx_params_group, wxID_ANY, "Preset:", wxDefaultPosition, wxSize(80, -1)), 0, wxALL, 5);
                p_choice = new wxChoice(m_fx_params_group, wxID_ANY, wxDefaultPosition, wxSize(200, -1));
                for (const auto& p : presets) p_choice->Append(p);
                p_choice->Append("Manual");
                if (!p_choice->SetStringSelection(dsp->current_preset())) p_choice->SetStringSelection("Manual");
                p_choice->Bind(wxEVT_CHOICE, [this, dsp](wxCommandEvent& ev) {
                    if (m_is_updating_ui) return;
                    dsp->load_preset(ev.GetString().ToStdString());
                    CallAfter([this]() { update_effect_editor(); });
                });
                p_row->Add(p_choice, 1, wxEXPAND | wxALL, 5);
                params_sizer->Add(p_row, 0, wxEXPAND | wxALL, 2);
            }

            auto add_slider = [&](const wxString& label, float min, float max, float val, std::function<void(float)> setter) {
                wxBoxSizer* s_row = new wxBoxSizer(wxHORIZONTAL);
                s_row->Add(new wxStaticText(m_fx_params_group, wxID_ANY, label, wxDefaultPosition, wxSize(80, -1)), 0, wxALL, 5);
                int steps = 1000;
                wxSlider* sl = new wxSlider(m_fx_params_group, wxID_ANY, (int)(((val - min) / (max - min)) * steps), 0, steps, wxDefaultPosition, wxSize(200, -1));
                sl->Bind(wxEVT_SLIDER, [this, dsp, p_choice, min, max, steps, setter](wxCommandEvent& ev) {
                    if (m_is_updating_ui) return;
                    setter(min + ((float)ev.GetInt() / (float)steps) * (max - min));
                    dsp->set_current_preset("Manual");
                    if (p_choice) p_choice->SetStringSelection("Manual");
                });
                s_row->Add(sl, 1, wxEXPAND | wxALL, 5);
                params_sizer->Add(s_row, 0, wxEXPAND | wxALL, 2);
            };

            if (auto* g = dynamic_cast<tpanar_ns::GainDSP*>(dsp)) {
                add_slider("Gain", 0.0f, 2.0f, g->gain, [g](float v){ g->gain = v; });
            } else if (auto* d = dynamic_cast<tpanar_ns::DelayDSP*>(dsp)) {
                add_slider("Feedback", 0.0f, 0.99f, d->feedback, [d](float v){ d->feedback = v; });
                add_slider("Mix", 0.0f, 1.0f, d->mix, [d](float v){ d->mix = v; });
            } else if (auto* rev = dynamic_cast<tpanar_ns::ReverbDSP*>(dsp)) {
                add_slider("Room Size", 0.0f, 1.0f, rev->room_size, [rev](float v){ rev->room_size = v; });
                add_slider("Damp", 0.0f, 1.0f, rev->damp, [rev](float v){ rev->damp = v; });
                add_slider("Mix", 0.0f, 1.0f, rev->mix, [rev](float v){ rev->mix = v; });
            } else if (auto* lim = dynamic_cast<tpanar_ns::LimiterDSP*>(dsp)) {
                add_slider("Ceiling", 0.0f, 1.0f, lim->ceiling, [lim](float v){ lim->ceiling = v; });
                add_slider("Threshold", 0.0f, 1.0f, lim->threshold, [lim](float v){ lim->threshold = v; });
            } else if (auto* exc = dynamic_cast<tpanar_ns::ExciterDSP*>(dsp)) {
                add_slider("Amount", 0.0f, 1.0f, exc->amount, [exc](float v){ exc->amount = v; });
                add_slider("Freq", 0.0f, 1.0f, exc->freq, [exc](float v){ exc->freq = v; });
            } else if (auto* pha = dynamic_cast<tpanar_ns::PhaserDSP*>(dsp)) {
                add_slider("Rate", 0.0f, 1.0f, pha->rate, [pha](float v){ pha->rate = v; });
                add_slider("Depth", 0.0f, 1.0f, pha->depth, [pha](float v){ pha->depth = v; });
                add_slider("Feedback", 0.0f, 1.0f, pha->feedback, [pha](float v){ pha->feedback = v; });
                add_slider("Mix", 0.0f, 1.0f, pha->mix, [pha](float v){ pha->mix = v; });
            } else if (auto* fla = dynamic_cast<tpanar_ns::FlangerDSP*>(dsp)) {
                add_slider("Rate", 0.0f, 1.0f, fla->rate, [fla](float v){ fla->rate = v; });
                add_slider("Depth", 0.0f, 1.0f, fla->depth, [fla](float v){ fla->depth = v; });
                add_slider("Feedback", 0.0f, 1.0f, fla->feedback, [fla](float v){ fla->feedback = v; });
                add_slider("Mix", 0.0f, 1.0f, fla->mix, [fla](float v){ fla->mix = v; });
            } else if (auto* ech = dynamic_cast<tpanar_ns::EchoDSP*>(dsp)) {
                add_slider("Time", 0.0f, 1.0f, ech->time, [ech](float v){ ech->time = v; });
                add_slider("Feedback", 0.0f, 1.0f, ech->feedback, [ech](float v){ ech->feedback = v; });
                add_slider("Damp", 0.0f, 1.0f, ech->damp, [ech](float v){ ech->damp = v; });
                add_slider("Mix", 0.0f, 1.0f, ech->mix, [ech](float v){ ech->mix = v; });
            } else if (auto* cmp = dynamic_cast<tpanar_ns::CompressorDSP*>(dsp)) {
                add_slider("Threshold", 0.0f, 1.0f, cmp->threshold, [cmp](float v){ cmp->threshold = v; });
                add_slider("Ratio", 1.0f, 20.0f, cmp->ratio, [cmp](float v){ cmp->ratio = v; });
                add_slider("Attack", 0.001f, 0.5f, cmp->attack, [cmp](float v){ cmp->attack = v; });
                add_slider("Release", 0.01f, 2.0f, cmp->release, [cmp](float v){ cmp->release = v; });
                add_slider("Makeup", 0.0f, 2.0f, cmp->makeup, [cmp](float v){ cmp->makeup = v; });
            } else if (auto* geq = dynamic_cast<tpanar_ns::GraphicalEQDSP*>(dsp)) {
                wxBoxSizer* eq_sizer = new wxBoxSizer(wxHORIZONTAL);
                for (int b = 0; b < 12; ++b) {
                    wxBoxSizer* band_sizer = new wxBoxSizer(wxVERTICAL);
                    wxSlider* s = new wxSlider(m_fx_params_group, wxID_ANY, (int)(geq->get_band_gain(b) * 10.0f), -120, 120, wxDefaultPosition, wxSize(-1, 100), wxSL_VERTICAL | wxSL_INVERSE | wxSL_TICKS);
                    s->SetTickFreq(60);
                    s->Bind(wxEVT_SLIDER, [this, dsp, geq, b, p_choice](wxCommandEvent& ev){
                        if (m_is_updating_ui) return;
                        geq->set_band_gain(b, (float)ev.GetInt() / 10.0f);
                        dsp->set_current_preset("Manual");
                        if (p_choice) p_choice->SetStringSelection("Manual");
                    });
                    band_sizer->Add(s, 1, wxEXPAND | wxALL, 2);
                    wxString lbl; float f = geq->get_band_freq(b);
                    if (f >= 1000) lbl.Printf("%dk", (int)(f/1000)); else lbl.Printf("%d", (int)f);
                    wxStaticText* t = new wxStaticText(m_fx_params_group, wxID_ANY, lbl);
                    wxFont font = t->GetFont(); font.SetPointSize(8); t->SetFont(font);
                    band_sizer->Add(t, 0, wxALIGN_CENTER | wxALL, 1);
                    eq_sizer->Add(band_sizer, 1, wxEXPAND | wxALL, 2);
                }
                params_sizer->Add(eq_sizer, 0, wxEXPAND | wxALL, 2);
            } else if (auto* cab = dynamic_cast<tpanar_ns::CabinetDSP*>(dsp)) {
                add_slider("Low Cut", 20.0f, 500.0f, cab->low_cut, [cab](float v){ cab->low_cut = v; });
                add_slider("High Cut", 1000.0f, 15000.0f, cab->high_cut, [cab](float v){ cab->high_cut = v; });
                add_slider("Peak Freq", 500.0f, 8000.0f, cab->peak_freq, [cab](float v){ cab->peak_freq = v; });
                add_slider("Peak Gain", -12.0f, 12.0f, cab->peak_gain, [cab](float v){ cab->peak_gain = v; });
            } else if (auto* dis = dynamic_cast<tpanar_ns::DistortionDSP*>(dsp)) {
                add_slider("Drive", 0.0f, 1.0f, dis->drive, [dis](float v){ dis->drive = v; });
                add_slider("Mix", 0.0f, 1.0f, dis->mix, [dis](float v){ dis->mix = v; });
            } else if (auto* cho = dynamic_cast<tpanar_ns::ChorusDSP*>(dsp)) {
                add_slider("Rate", 0.0f, 1.0f, cho->rate, [cho](float v){ cho->rate = v; });
                add_slider("Depth", 0.0f, 1.0f, cho->depth, [cho](float v){ cho->depth = v; });
                add_slider("Feedback", 0.0f, 1.0f, cho->feedback, [cho](float v){ cho->feedback = v; });
                add_slider("Mix", 0.0f, 1.0f, cho->mix, [cho](float v){ cho->mix = v; });
            } else if (auto* exp = dynamic_cast<tpanar_ns::StereoExpanderDSP*>(dsp)) {
                add_slider("Width", 0.0f, 2.0f, exp->width, [exp](float v){ exp->width = v; });
            } else if (auto* rmo = dynamic_cast<tpanar_ns::RingModulatorDSP*>(dsp)) {
                add_slider("Freq", 0.0f, 1.0f, rmo->freq, [rmo](float v){ rmo->freq = v; });
                add_slider("Mix", 0.0f, 1.0f, rmo->mix, [rmo](float v){ rmo->mix = v; });
            } else if (auto* gate = dynamic_cast<tpanar_ns::GateDSP*>(dsp)) {
                add_slider("Threshold", 0.0f, 1.0f, gate->threshold, [gate](float v){ gate->threshold = v; });
                add_slider("Range", 0.0f, 1.0f, gate->range, [gate](float v){ gate->range = v; });
                add_slider("Attack", 0.001f, 0.5f, gate->attack, [gate](float v){ gate->attack = v; });
                add_slider("Release", 0.01f, 2.0f, gate->release, [gate](float v){ gate->release = v; });
            } else if (auto* rm = dynamic_cast<tpanar_ns::ReferenceMatcherDSP*>(dsp)) {
                // Reference file info
                wxBoxSizer* ref_row = new wxBoxSizer(wxHORIZONTAL);
                ref_row->Add(new wxStaticText(m_fx_params_group, wxID_ANY, "Reference:", wxDefaultPosition, wxSize(80, -1)), 0, wxALL, 5);
                if (rm->is_reference_loaded()) {
                    wxString ref_name = rm->get_reference_path();
                    if (ref_name.Len() > 30) ref_name = "..." + ref_name.Right(30);
                    ref_row->Add(new wxStaticText(m_fx_params_group, wxID_ANY, ref_name), 1, wxALL | wxALIGN_CENTER_VERTICAL, 5);
                } else {
                    ref_row->Add(new wxStaticText(m_fx_params_group, wxID_ANY, "(None)"), 1, wxALL | wxALIGN_CENTER_VERTICAL, 5);
                }
                wxButton* load_ref_btn = new wxButton(m_fx_params_group, wxID_ANY, "Load");
                load_ref_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_BUTTON, wxSize(14, 14)));
                load_ref_btn->Bind(wxEVT_BUTTON, [this, rm](wxCommandEvent& ev) {
                    wxFileDialog dlg(this, "Load Reference Track", "", "", "Audio files (*.wav;*.flac;*.ogg;*.mp3)|*.wav;*.flac;*.ogg;*.mp3", wxFD_OPEN);
                    if (dlg.ShowModal() == wxID_OK) {
                        rm->load_reference(dlg.GetPath().ToStdString());
                        update_effect_editor();
                    }
                });
                ref_row->Add(load_ref_btn, 0, wxALL, 2);
                params_sizer->Add(ref_row, 0, wxEXPAND | wxALL, 2);

                add_slider("Mix", 0.0f, 1.0f, rm->get_mix(), [rm](float v){ rm->set_mix(v); });
                add_slider("Target RMS (dB)", -24.0f, -6.0f, rm->get_target_rms(), [rm](float v){ rm->set_target_rms(v); });
            } else if (auto* voc = dynamic_cast<tpanar_ns::VocoderDSP*>(dsp)) {
                add_slider("Attack", 0.001f, 0.2f, voc->attack, [voc](float v){ voc->attack = v; });
                add_slider("Release", 0.001f, 0.5f, voc->release, [voc](float v){ voc->release = v; });
                add_slider("Bandwidth", 0.01f, 0.5f, voc->bandwidth, [voc](float v){ voc->bandwidth = v; voc->update_bands(); });
                add_slider("Shift", -1.0f, 1.0f, voc->shift, [voc](float v){ voc->shift = v; voc->update_bands(); });
                
                wxBoxSizer* c_row = new wxBoxSizer(wxHORIZONTAL);
                c_row->Add(new wxStaticText(m_fx_params_group, wxID_ANY, "Carrier:", wxDefaultPosition, wxSize(80, -1)), 0, wxALL, 5);
                wxChoice* c_choice = new wxChoice(m_fx_params_group, wxID_ANY);
                c_choice->Append("Saw"); c_choice->Append("Noise"); c_choice->Append("External");
                c_choice->SetSelection((int)voc->carrier_type);
                c_choice->Bind(wxEVT_CHOICE, [this, voc, p_choice](wxCommandEvent& ev) {
                    if (m_is_updating_ui) return;
                    voc->carrier_type = (float)ev.GetSelection();
                    voc->set_current_preset("Manual");
                    if (p_choice) p_choice->SetStringSelection("Manual");
                });
                c_row->Add(c_choice, 1, wxEXPAND | wxALL, 5);
                params_sizer->Add(c_row, 0, wxEXPAND | wxALL, 2);
                
                add_slider("Mix", 0.0f, 1.0f, voc->mix, [voc](float v){ voc->mix = v; });
            }
        } else {
            params_sizer->Add(new wxStaticText(m_fx_params_group, wxID_ANY, "No effect selected"), 0, wxALL, 5);
        }
    }

    m_fx_params_group->SetSizer(params_sizer);
    m_fx_params_group->Layout();
    m_fx_params_group->FitInside();
    
    update_mastering_panel();
    
    m_is_updating_ui = false;
}

void MixerPanel::on_master_gain(wxCommandEvent& event) {
    float gain = event.GetInt() / 100.0f;
    m_engine.set_master_gain(gain);
}

void MixerPanel::on_master_mute(wxCommandEvent& event) {
    m_engine.m_master.set_mute(event.IsChecked());
}

void MixerPanel::on_track_volume(wxCommandEvent& event) {}
void MixerPanel::on_track_pan(wxCommandEvent& event) {}
void MixerPanel::on_track_mute(wxCommandEvent& event) {}
void MixerPanel::on_track_solo(wxCommandEvent& event) {}
void MixerPanel::on_track_select(wxCommandEvent& event) {
    // This is a generic handler, but we use specific Bind() calls for tracks/buses.
    // If we get here, it might be an unhandled event.
    event.Skip();
}

void MixerPanel::on_bus_volume(wxCommandEvent& event) {}
void MixerPanel::on_bus_pan(wxCommandEvent& event) {}
void MixerPanel::on_bus_mute(wxCommandEvent& event) {}
void MixerPanel::on_bus_select(wxCommandEvent& event) {}

bool MixerPanel::all_audio_tracks_muted() const
{
    for (size_t i = 0; i < m_engine.track_count(); ++i) {
        if (m_engine.track(i).kind() == TrackKind::Audio && !m_engine.track(i).muted())
            return false;
    }
    return m_engine.track_count() > 0;
}

bool MixerPanel::all_note_tracks_muted() const
{
    for (size_t i = 0; i < m_engine.track_count(); ++i) {
        if (m_engine.track(i).kind() == TrackKind::Note && !m_engine.track(i).muted())
            return false;
    }
    return m_engine.track_count() > 0;
}

void MixerPanel::mute_all_audio_tracks(bool mute)
{
    for (size_t i = 0; i < m_engine.track_count(); ++i) {
        if (m_engine.track(i).kind() == TrackKind::Audio)
            m_engine.track(i).set_mute(mute);
    }
}

void MixerPanel::mute_all_note_tracks(bool mute)
{
    for (size_t i = 0; i < m_engine.track_count(); ++i) {
        if (m_engine.track(i).kind() == TrackKind::Note)
            m_engine.track(i).set_mute(mute);
    }
}

void MixerPanel::update_mute_group_buttons()
{
    if (!m_mute_audio_btn || !m_mute_notes_btn) return;
    bool audio_muted = all_audio_tracks_muted();
    bool notes_muted = all_note_tracks_muted();
    m_mute_audio_btn->SetLabel(audio_muted ? "Unmute Audio" : "Mute Audio");
    m_mute_notes_btn->SetLabel(notes_muted ? "Unmute Notes" : "Mute Notes");
}

void MixerPanel::on_add_fx(wxCommandEvent& event) {
    bool is_master = (m_selected_track == kSelectedMaster);
    if (is_master) return; // Prevent adding effects to master bus

    int sel = m_avail_fx_browser->GetSelection();
    if (sel == wxNOT_FOUND) return;
    wxString fx_name = m_avail_fx_browser->GetString(sel);

    bool is_bus = (m_selected_track >= kSelectedBusBase);
    bool is_track = (m_selected_track >= kSelectedTrackBase && m_selected_track < kSelectedBusBase);
    
    int bus_idx = is_bus ? (m_selected_track - kSelectedBusBase) : -1;
    int track_idx = is_track ? (m_selected_track - kSelectedTrackBase) : -1;

    auto get_fx_at = [&](size_t idx) -> DSP* {
        if (is_bus && bus_idx >= 0 && bus_idx < (int)m_engine.bus_count()) return m_engine.bus(bus_idx).get_effect(idx);
        if (is_track && track_idx >= 0 && track_idx < (int)m_engine.track_count()) return m_engine.track(track_idx).get_effect(idx);
        return nullptr;
    };
    auto set_fx_at = [&](size_t idx, std::unique_ptr<DSP> dsp) {
        if (is_bus && bus_idx >= 0 && bus_idx < (int)m_engine.bus_count()) {
             m_engine.bus(bus_idx).set_effect(idx, std::move(dsp)); 
             m_engine.bus(bus_idx).enable_effect(idx, true);
        } else if (is_track && track_idx >= 0 && track_idx < (int)m_engine.track_count()) {
             m_engine.track(track_idx).set_effect(idx, std::move(dsp)); 
             m_engine.track(track_idx).enable_effect(idx, true);
        }
    };

    int slot = -1;
    for (size_t i = 0; i < MAX_INSERTS; ++i) {
        if (!get_fx_at(i)) {
            slot = (int)i;
            break;
        }
    }
    if (slot == -1) return;

    std::unique_ptr<DSP> dsp;
    if (fx_name == "Gain") dsp = std::make_unique<tpanar_ns::GainDSP>();
    else if (fx_name == "Delay") dsp = std::make_unique<tpanar_ns::DelayDSP>();
    else if (fx_name == "Reverb") dsp = std::make_unique<tpanar_ns::ReverbDSP>();
    else if (fx_name == "Limiter") dsp = std::make_unique<tpanar_ns::LimiterDSP>();
    else if (fx_name == "Exciter") dsp = std::make_unique<tpanar_ns::ExciterDSP>();
    else if (fx_name == "Phaser") dsp = std::make_unique<tpanar_ns::PhaserDSP>();
    else if (fx_name == "Flanger") dsp = std::make_unique<tpanar_ns::FlangerDSP>();
    else if (fx_name == "Echo") dsp = std::make_unique<tpanar_ns::EchoDSP>();
    else if (fx_name == "Compressor") dsp = std::make_unique<tpanar_ns::CompressorDSP>();
    else if (fx_name == "Graphical EQ") dsp = std::make_unique<tpanar_ns::GraphicalEQDSP>();
    else if (fx_name == "Cabinet") dsp = std::make_unique<tpanar_ns::CabinetDSP>();
    else if (fx_name == "Distortion") dsp = std::make_unique<tpanar_ns::DistortionDSP>();
    else if (fx_name == "Chorus") dsp = std::make_unique<tpanar_ns::ChorusDSP>();
    else if (fx_name == "Stereo Expander") dsp = std::make_unique<tpanar_ns::StereoExpanderDSP>();
    else if (fx_name == "Ring Modulator") dsp = std::make_unique<tpanar_ns::RingModulatorDSP>();
    else if (fx_name == "Gate") dsp = std::make_unique<tpanar_ns::GateDSP>();
    else if (fx_name == "Reference Matcher") dsp = std::make_unique<tpanar_ns::ReferenceMatcherDSP>();
    else if (fx_name == "Vocoder") dsp = std::make_unique<tpanar_ns::VocoderDSP>();

    if (dsp) {
        set_fx_at(slot, std::move(dsp));
        m_selected_fx_slot = slot;
        update_effect_editor();
    }
}

void MixerPanel::on_fx_up(wxCommandEvent& event) {}
void MixerPanel::on_fx_down(wxCommandEvent& event) {}
void MixerPanel::on_fx_remove(wxCommandEvent& event) {}
void MixerPanel::on_fx_bypass(wxCommandEvent& event) {}

void MixerPanel::on_save_chain(wxCommandEvent& event) {
    bool is_master = (m_selected_track == kSelectedMaster);
    if (is_master) return;

    wxFileDialog dlg(this, "Save Chain", "", "", "Chain files (*.chain)|*.chain", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_OK) {
        bool is_bus = (m_selected_track >= kSelectedBusBase);
        bool is_track = (m_selected_track >= kSelectedTrackBase && m_selected_track < kSelectedBusBase);

        if (is_bus) {
            int idx = m_selected_track - kSelectedBusBase;
            if (idx >= 0 && idx < (int)m_engine.bus_count())
                m_engine.bus(idx).save_effect_chain(dlg.GetPath().ToStdString());
        }
        else if (is_track) {
            int idx = m_selected_track - kSelectedTrackBase;
            if (idx >= 0 && idx < (int)m_engine.track_count())
                m_engine.track(idx).save_effect_chain(dlg.GetPath().ToStdString());
        }
    }
}

void MixerPanel::on_load_chain(wxCommandEvent& event) {
    bool is_master = (m_selected_track == kSelectedMaster);
    if (is_master) return;

    wxFileDialog dlg(this, "Load Chain", "", "", "Chain files (*.chain)|*.chain", wxFD_OPEN);
    if (dlg.ShowModal() == wxID_OK) {
        bool is_bus = (m_selected_track >= kSelectedBusBase);
        bool is_track = (m_selected_track >= kSelectedTrackBase && m_selected_track < kSelectedBusBase);

        if (is_bus) {
            int idx = m_selected_track - kSelectedBusBase;
            if (idx >= 0 && idx < (int)m_engine.bus_count())
                m_engine.bus(idx).load_effect_chain(dlg.GetPath().ToStdString());
        }
        else if (is_track) {
            int idx = m_selected_track - kSelectedTrackBase;
            if (idx >= 0 && idx < (int)m_engine.track_count())
                m_engine.track(idx).load_effect_chain(dlg.GetPath().ToStdString());
        }
        m_selected_fx_slot = -1;
        update_effect_editor();
    }
}

void MixerPanel::on_detach(wxCommandEvent& event) {
    if (m_detached_frame) {
        return;
    }
    Hide();
    m_detached_frame = new DetachedFrame(this, "Mixer", GetParent(), m_tab_index);
    m_detached_frame->set_on_detach_callback([this]() { m_detached_frame = nullptr; });
}

} // namespace tpanar_ns
