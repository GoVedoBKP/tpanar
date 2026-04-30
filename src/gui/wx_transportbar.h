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
#include <wx/slider.h>
#include <wx/button.h>
#include <wx/tglbtn.h>
#include <wx/stattext.h>
#include <wx/spinbutt.h>
#include <wx/spinctrl.h>
#include <wx/sizer.h>

#include "../core/transport.h"

namespace tpanar_ns {

class Engine;

class TransportBar : public wxPanel {
public:
    TransportBar(wxWindow* parent, wxWindowID id, Engine& engine);

    void update();
    void resize(int w, int h);

private:
    void on_play(wxCommandEvent& event);
    void on_stop(wxCommandEvent& event);
    void on_record(wxCommandEvent& event);
    void on_audio_record(wxCommandEvent& event);
    void on_metronome(wxCommandEvent& event);
    void on_loop(wxCommandEvent& event);
    void on_tempo(wxSpinEvent& event);
    void on_lpb(wxSpinEvent& event);
    void on_octave(wxSpinEvent& event);
    void on_step(wxSpinEvent& event);
    void on_metro_vol(wxCommandEvent& event);

    Engine& m_engine;

    wxButton* m_play;
    wxButton* m_stop;
    wxToggleButton* m_record;
    wxToggleButton* m_audio_record;
    wxToggleButton* m_metronome;
    wxPanel* m_metro_visual;
    wxSlider* m_metro_vol;
    wxToggleButton* m_loop;
    wxSpinCtrl* m_tempo_spin;
    wxSpinCtrl* m_lpb_spin;
    wxSpinCtrl* m_octave_spin;
    wxSpinCtrl* m_step_spin;
    wxStaticText* m_clock;
    class VUMeter* m_meter_l;
    class VUMeter* m_meter_r;

    wxString m_clock_str;

    wxDECLARE_EVENT_TABLE();
};

} // namespace tpanar_ns
