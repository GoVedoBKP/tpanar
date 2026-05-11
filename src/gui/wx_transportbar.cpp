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

#include "wx_transportbar.h"
#include "theme.h"
#include "wx_vu_meter.h"
#include "wx_main_window.h"

#include <wx/artprov.h>
#include "../core/engine.h"

#include <wx/sizer.h>
#include <wx/statline.h>
#include <algorithm>
#include <cstdio>

namespace tpanar_ns {

enum {
    ID_PLAY = wxID_HIGHEST + 1,
    ID_STOP,
    ID_RECORD,
    ID_AUDIO_RECORD,
    ID_METRONOME,
    ID_LOOP,
    ID_TEMPO,
    ID_LPB,
    ID_OCTAVE,
    ID_STEP
};

wxBEGIN_EVENT_TABLE(TransportBar, wxPanel)
    EVT_BUTTON(ID_PLAY, TransportBar::on_play)
    EVT_BUTTON(ID_STOP, TransportBar::on_stop)
    EVT_TOGGLEBUTTON(ID_RECORD, TransportBar::on_record)
    EVT_TOGGLEBUTTON(ID_AUDIO_RECORD, TransportBar::on_audio_record)
    EVT_TOGGLEBUTTON(ID_METRONOME, TransportBar::on_metronome)
    EVT_TOGGLEBUTTON(ID_LOOP, TransportBar::on_loop)
    EVT_SPINCTRL(ID_TEMPO, TransportBar::on_tempo)
    EVT_SPINCTRL(ID_LPB, TransportBar::on_lpb)
    EVT_SPINCTRL(ID_OCTAVE, TransportBar::on_octave)
    EVT_SPINCTRL(ID_STEP, TransportBar::on_step)
wxEND_EVENT_TABLE()

TransportBar::TransportBar(wxWindow* parent, wxWindowID id, Engine& engine)
    : wxPanel(parent, id), m_engine(engine)
{
    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);
    // Guarantee enough height for GTK spin buttons (both arrows visible)
    SetMinSize(wxSize(-1, 50));

    const int PAD   = 4;   // standard item padding
    const int GPAD  = 6;   // gap around group separators
    const int BTN_W = 62;
    const int BTN_H = -1;  // natural height
    const int SPN_H = 30;  // explicit spin height — ensures both arrows render

    auto add_sep = [&]() {
        auto* sep = new wxStaticLine(this, wxID_ANY, wxDefaultPosition,
                                     wxDefaultSize, wxLI_VERTICAL);
        main_sizer->Add(sep, 0, wxEXPAND | wxLEFT | wxRIGHT, GPAD);
    };

    // --- Transport buttons ---
    m_play = new wxButton(this, ID_PLAY, "Play", wxDefaultPosition, wxSize(BTN_W, BTN_H));
    m_play->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON, wxSize(16, 16)));
    main_sizer->Add(m_play, 0, wxALIGN_CENTER_VERTICAL | wxALL, PAD);

    m_stop = new wxButton(this, ID_STOP, "Stop", wxDefaultPosition, wxSize(BTN_W, BTN_H));
    m_stop->SetBitmap(wxArtProvider::GetBitmap(wxART_STOP, wxART_BUTTON, wxSize(16, 16)));
    main_sizer->Add(m_stop, 0, wxALIGN_CENTER_VERTICAL | wxALL, PAD);

    add_sep();

    // --- Mode toggles ---
    m_record = new wxToggleButton(this, ID_RECORD, "Edit", wxDefaultPosition, wxSize(BTN_W, BTN_H));
    main_sizer->Add(m_record, 0, wxALIGN_CENTER_VERTICAL | wxALL, PAD);

    m_audio_record = new wxToggleButton(this, ID_AUDIO_RECORD, "Rec", wxDefaultPosition, wxSize(BTN_W, BTN_H));
    main_sizer->Add(m_audio_record, 0, wxALIGN_CENTER_VERTICAL | wxALL, PAD);

    m_loop = new wxToggleButton(this, ID_LOOP, "Loop", wxDefaultPosition, wxSize(BTN_W, BTN_H));
    m_loop->SetValue(false);
    main_sizer->Add(m_loop, 0, wxALIGN_CENTER_VERTICAL | wxALL, PAD);

    add_sep();

    // --- Metronome ---
    m_metronome = new wxToggleButton(this, ID_METRONOME, wxString::FromUTF8("♩"),
                                     wxDefaultPosition, wxSize(BTN_W, BTN_H));
    m_metronome->SetValue(false);
    main_sizer->Add(m_metronome, 0, wxALIGN_CENTER_VERTICAL | wxALL, PAD);

    m_metro_visual = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(14, 14), wxBORDER_SIMPLE);
    m_metro_visual->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_tracker_bg));
    main_sizer->Add(m_metro_visual, 0, wxALIGN_CENTER_VERTICAL | wxALL, PAD);

    m_metro_vol = new wxSlider(this, wxID_ANY, 40, 0, 100, wxDefaultPosition, wxSize(50, -1));
    m_metro_vol->Bind(wxEVT_SLIDER, &TransportBar::on_metro_vol, this);
    main_sizer->Add(m_metro_vol, 0, wxALIGN_CENTER_VERTICAL | wxALL, PAD);

    add_sep();

    // --- Numeric controls ---
    auto add_spin = [&](const char* label, wxSpinCtrl*& spin, wxWindowID id,
                        int lo, int hi, int val, int w) {
        main_sizer->Add(new wxStaticText(this, wxID_ANY, label),
                        0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 3);
        spin = new wxSpinCtrl(this, id, wxEmptyString, wxDefaultPosition, wxSize(w, SPN_H));
        spin->SetRange(lo, hi);
        spin->SetValue(val);
        main_sizer->Add(spin, 0, wxALIGN_CENTER_VERTICAL | wxALL, PAD);
    };

    add_spin("BPM",  m_tempo_spin,  ID_TEMPO,   30, 300, (int)m_engine.tempo(),      120);
    add_spin("LPB",  m_lpb_spin,    ID_LPB,      1, 128, (int)m_engine.lpb(),        120);
    add_spin("Oct",  m_octave_spin, ID_OCTAVE,   0,   9, (int)m_engine.base_octave(),120);
    add_spin("Step", m_step_spin,   ID_STEP,     1,  64, 1,                           120);

    add_sep();

    // --- Clock ---
    m_clock = new wxStaticText(this, wxID_ANY, "00:00.000", wxDefaultPosition, wxSize(82, -1));
    wxFont clock_font = m_clock->GetFont();
    clock_font.SetFamily(wxFONTFAMILY_TELETYPE);
    clock_font.SetWeight(wxFONTWEIGHT_BOLD);
    clock_font.SetPointSize(12);
    m_clock->SetFont(clock_font);
    m_clock->SetForegroundColour(ThemeManager::toWxColour(m_engine.m_fg_color));
    main_sizer->Add(m_clock, 0, wxALIGN_CENTER_VERTICAL | wxALL, PAD);

    add_sep();

    // --- VU meters ---
    wxBoxSizer* meter_stack = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* l_row = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* l_lbl = new wxStaticText(this, wxID_ANY, "L");
    wxFont mini_font = l_lbl->GetFont(); mini_font.SetPointSize(8); l_lbl->SetFont(mini_font);
    l_row->Add(l_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
    m_meter_l = new VUMeter(this, wxID_ANY, m_engine, true);
    m_meter_l->SetMinSize(wxSize(20, 8));
    l_row->Add(m_meter_l, 1, wxEXPAND);
    meter_stack->Add(l_row, 1, wxEXPAND | wxBOTTOM, 2);

    wxBoxSizer* r_row = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* r_lbl = new wxStaticText(this, wxID_ANY, "R");
    r_lbl->SetFont(mini_font);
    r_row->Add(r_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
    m_meter_r = new VUMeter(this, wxID_ANY, m_engine, true);
    m_meter_r->SetMinSize(wxSize(20, 8));
    r_row->Add(m_meter_r, 1, wxEXPAND);
    meter_stack->Add(r_row, 1, wxEXPAND);

    main_sizer->Add(meter_stack, 1, wxEXPAND | wxALL, PAD);

    m_clock_str = "00:00.000";

    SetSizer(main_sizer);
}

void TransportBar::on_play(wxCommandEvent& event) {
    WxMainWindow* main_win = dynamic_cast<WxMainWindow*>(GetParent());
    if (main_win && main_win->start_playback_from_selected_position()) {
        return;
    }

    if (m_engine.transport_state() != TransportState::Stopped) {
        m_engine.play();
    } else {
        m_engine.auto_seek();
        m_engine.start();
    }
}

void TransportBar::on_stop(wxCommandEvent& event) {
    m_engine.stop();
}

void TransportBar::on_record(wxCommandEvent& event) {
    m_engine.enable_record(m_record->GetValue());
}

void TransportBar::on_audio_record(wxCommandEvent& event) {
    m_engine.set_audio_record_enabled(m_audio_record->GetValue());
}

void TransportBar::on_metronome(wxCommandEvent& event) {
    m_engine.set_metronome_enabled(m_metronome->GetValue());
}

void TransportBar::on_loop(wxCommandEvent& event) {
    m_engine.set_loop(m_loop->GetValue());
}

void TransportBar::on_tempo(wxSpinEvent& event) {
    m_engine.set_tempo(m_tempo_spin->GetValue());
}

void TransportBar::on_lpb(wxSpinEvent& event) {
    m_engine.set_lpb(m_lpb_spin->GetValue());
}

void TransportBar::on_octave(wxSpinEvent& event) {
    m_engine.set_base_octave(m_octave_spin->GetValue());
}

void TransportBar::on_step(wxSpinEvent& event) {
    m_engine.set_step_size(m_step_spin->GetValue());
}

void TransportBar::on_metro_vol(wxCommandEvent& event) {
    m_engine.set_metronome_volume(m_metro_vol->GetValue() / 100.0f);
}

void TransportBar::update() {
    if (!m_tempo_spin->HasFocus()) m_tempo_spin->SetValue((int)m_engine.tempo());
    if (!m_lpb_spin->HasFocus()) m_lpb_spin->SetValue((int)m_engine.lpb());
    if (!m_octave_spin->HasFocus()) m_octave_spin->SetValue(m_engine.base_octave());
    if (!m_step_spin->HasFocus()) m_step_spin->SetValue((int)m_engine.step_size());
    m_record->SetValue(m_engine.m_record_enabled);
    m_audio_record->SetValue(m_engine.audio_record_enabled());

    auto state = m_engine.transport_state();
    double total_seconds = 0;
    if (state == TransportState::Stopped) {
        WxMainWindow* main_win = dynamic_cast<WxMainWindow*>(GetParent());
        if (main_win) {
            const int row = main_win->get_selected_song_row();
            if (row >= 0) {
                total_seconds = m_engine.get_time_at_row((size_t)row);
            } else {
                total_seconds = m_engine.get_current_time_seconds();
            }
        } else {
            total_seconds = m_engine.get_current_time_seconds();
        }
    } else {
        total_seconds = m_engine.get_current_time_seconds();
    }

    int minutes = (int)(total_seconds / 60);
    double seconds = total_seconds - (minutes * 60);

    m_clock_str.Printf("%02d:%06.3f", minutes, seconds);
    m_clock->SetLabel(m_clock_str);

    if (m_meter_l) m_meter_l->level(m_engine.master_meter_l());
    if (m_meter_r) m_meter_r->level(m_engine.master_meter_r());

    if (m_engine.is_playing() && m_engine.m_samples_until_next_beat < 1000) {
        m_metro_visual->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_tracker_cursor));
    } else {
        m_metro_visual->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_tracker_bg));
    }

    Refresh();
}

void TransportBar::resize(int w, int h) {
    SetSize(w, h);
}

} // namespace tpanar_ns
