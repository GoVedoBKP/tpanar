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

#include "wx_main_window.h"
#include "wx_transportbar.h"
#include "wx_tracker_panel.h"
#include "wx_tracker_view.h"
#include "wx_tracks_panel.h"
#include "wx_mixer_panel.h"
#include "wx_instrument_panel.h"
#include "wx_settings_panel.h"
#include "wx_project_panel.h"
#include "wx_help_panel.h"
#include "../core/engine.h"
#include <wx/combobox.h>
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/statbmp.h>
#include <wx/stdpaths.h>
#include <filesystem>
#include <iostream>
#include <optional>

namespace tpanar_ns {

namespace {

std::optional<wxImage> load_logo_image() {
    namespace fs = std::filesystem;

    const fs::path exe_path(wxStandardPaths::Get().GetExecutablePath().ToStdString());
    const fs::path exe_dir = exe_path.parent_path();
    const fs::path resources_dir(wxStandardPaths::Get().GetResourcesDir().ToStdString());
    const fs::path cwd = fs::current_path();
    const fs::path logo_name = fs::path("imgs") / "tpanar.png";

    const fs::path candidates[] = {
        cwd / logo_name,
        exe_dir / logo_name,
        exe_dir / ".." / logo_name,
        exe_dir / ".." / "share" / "tpanar" / logo_name,
        resources_dir / logo_name,
        fs::path("/usr/local/share/tpanar") / logo_name,
        fs::path("/usr/share/tpanar") / logo_name,
    };

    for (const fs::path& candidate : candidates) {
        const fs::path normalized = candidate.lexically_normal();
        if (!fs::exists(normalized)) {
            continue;
        }

        wxImage image;
        if (image.LoadFile(wxString::FromUTF8(normalized.string()), wxBITMAP_TYPE_PNG)) {
            return image;
        }
    }

    return std::nullopt;
}

wxBitmap scaled_logo_bitmap(const wxImage& image, int width, int height) {
    wxImage scaled = image.Copy();
    scaled.Rescale(width, height, wxIMAGE_QUALITY_HIGH);
    return wxBitmap(scaled);
}

constexpr int kUiTimerIntervalMs = 16;

} // namespace

BEGIN_EVENT_TABLE(WxMainWindow, wxFrame)
    EVT_TIMER(wxID_ANY, WxMainWindow::OnTimer)
    EVT_CLOSE(WxMainWindow::OnClose)
    EVT_CHAR_HOOK(WxMainWindow::OnCharHook)
    EVT_KEY_DOWN(WxMainWindow::OnKeyDown)
END_EVENT_TABLE()

WxMainWindow::WxMainWindow(int w, int h, const wxString& title, Engine& engine)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(w, h)),
      m_engine(engine),
      m_timer(nullptr),
      m_tabs(nullptr),
      m_transport(nullptr),
      m_project_panel(nullptr),
      m_tracker_panel(nullptr),
      m_tracks_panel(nullptr),
      m_instrument_panel(nullptr),
      m_mixer_panel(nullptr),
      m_settings_panel(nullptr),
      m_help_panel(nullptr),
      m_selected_tab(0)
{
    SetBackgroundStyle(wxBG_STYLE_SYSTEM);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* header_sizer = new wxBoxSizer(wxHORIZONTAL);

    if (std::optional<wxImage> logo_image = load_logo_image(); logo_image && logo_image->IsOk()) {
        wxStaticBitmap* logo = new wxStaticBitmap(this, wxID_ANY, scaled_logo_bitmap(*logo_image, 44, 44));
        header_sizer->Add(logo, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 8);

        wxIcon logo_icon;
        logo_icon.CopyFromBitmap(wxBitmap(*logo_image));
        if (logo_icon.IsOk()) {
            SetIcon(logo_icon);
        }
    }

    m_transport = new TransportBar(this, wxID_ANY, m_engine);
    header_sizer->Add(m_transport, 1, wxEXPAND | wxRIGHT, 0);
    main_sizer->Add(header_sizer, 0, wxEXPAND | wxALL, 0);

    m_tabs = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);
    main_sizer->Add(m_tabs, 1, wxEXPAND | wxALL, 0);

    init_panels();

    SetSizer(main_sizer);

    m_timer = new wxTimer(this, wxID_ANY);
    m_timer->Start(kUiTimerIntervalMs);

    Centre();
}

WxMainWindow::~WxMainWindow() {
    if (m_timer) {
        m_timer->Stop();
        delete m_timer;
    }
}

void WxMainWindow::init_panels() {
    m_project_panel = new ProjectPanel(m_tabs, this, m_engine);
    m_tabs->AddPage(m_project_panel, "Project");

    m_tracker_panel = new TrackerPanel(m_tabs, m_engine);
    m_tabs->AddPage(m_tracker_panel, "Tracker");
    m_tracker_panel->set_tab_index(1);

    m_tracks_panel = new TracksPanel(m_tabs, m_engine);
    m_tabs->AddPage(m_tracks_panel, "Tracks");
    m_tracks_panel->set_tab_index(2);


    m_instrument_panel = new InstrumentPanel(m_tabs, m_engine);
    m_tabs->AddPage(m_instrument_panel, "Instrument");
    m_instrument_panel->set_tab_index(3);

    m_mixer_panel = new MixerPanel(m_tabs, m_engine);
    m_tabs->AddPage(m_mixer_panel, "Mixer");
    m_mixer_panel->set_tab_index(4);

    m_settings_panel = new SettingsPanel(m_tabs, m_engine);
    m_tabs->AddPage(m_settings_panel, "Settings");

    m_help_panel = new HelpPanel(m_tabs, m_engine);
    m_tabs->AddPage(m_help_panel, "Help");

    m_tabs->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& event) {
        m_selected_tab = event.GetSelection();
        update_all_uis();
        if (m_selected_tab == 1 && m_tracker_panel) {
            m_tracker_panel->grab_focus();
        }
    }, wxID_ANY);
}

void WxMainWindow::update_all_uis() {
    if (m_mixer_panel) m_mixer_panel->update_mixer_ui();
    if (m_tracker_panel) m_tracker_panel->update_pattern_list();
    if (m_project_panel) {
        m_project_panel->update_track_list();
        m_project_panel->update_metadata();
    }
    if (m_instrument_panel) {
        m_instrument_panel->update_instrument_list();
        m_instrument_panel->update_editor();
    }
}

void WxMainWindow::request_update() {
    wxQueueEvent(this, new wxCommandEvent(wxEVT_COMMAND_BUTTON_CLICKED));
}

Track& WxMainWindow::track(size_t index) {
    return m_engine.track(index);
}

int WxMainWindow::get_cursor_row() const {
    if (m_tracker_panel) return m_tracker_panel->get_cursor_row();
    return 0;
}

void WxMainWindow::OnTimer(wxTimerEvent& event) {
    if (m_transport) m_transport->update();
    if (m_mixer_panel) m_mixer_panel->update_meters();
    if (m_tracker_panel && m_engine.transport_state() != TransportState::Stopped) {
        m_tracker_panel->update();
    }

    int current_tab = m_tabs->GetSelection();
    if (m_tracks_panel && (m_engine.transport_state() != TransportState::Stopped || current_tab == 2)) {
        m_tracks_panel->update();
    }

}

void WxMainWindow::OnClose(wxCloseEvent& event) {
    if (event.CanVeto() && m_engine.is_dirty()) {
        int answer = wxMessageBox(
            "The project has unsaved changes.\nDo you want to save before closing?",
            "Unsaved Changes",
            wxYES_NO | wxCANCEL | wxICON_QUESTION, this);

        if (answer == wxCANCEL) {
            event.Veto();
            return;
        }

        if (answer == wxYES) {
            wxFileDialog dlg(this, "Save Project", "", "",
                             "TPanar Projects (*.tp)|*.tp",
                             wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (dlg.ShowModal() != wxID_OK) {
                event.Veto();
                return;
            }
            wxFileName filename(dlg.GetPath());
            if (!filename.HasExt()) {
                filename.SetExt("tp");
            }
            m_engine.save_project(filename.GetFullPath().ToStdString());
        }
    }

    m_engine.shutdown();
    Destroy();
}

void WxMainWindow::OnCharHook(wxKeyEvent& event) {
    // Prevent global key handling if a text control has focus
    wxWindow* focus = wxWindow::FindFocus();
    if (focus && (dynamic_cast<wxTextCtrl*>(focus) || dynamic_cast<wxComboBox*>(focus))) {
        event.Skip();
        return;
    }

    int key = event.GetKeyCode();
    int wx_mods = event.GetModifiers();
    int modifiers = 0;
    if (wx_mods & wxMOD_CONTROL) modifiers |= 0x1000;
    if (wx_mods & wxMOD_ALT)     modifiers |= 0x2000;
    if (wx_mods & wxMOD_SHIFT)   modifiers |= 0x4000;

    Action action = m_engine.m_key_bindings.get_action(key, modifiers);

    auto is_note_action = [](Action a) -> bool {
        return ((int)a >= (int)Action::NoteC && (int)a <= (int)Action::NoteB) ||
               ((int)a >= (int)Action::NoteC2 && (int)a <= (int)Action::NoteB2) ||
               (a == Action::NoteC3) || (a == Action::NoteOff);
    };

    if (is_note_action(action)) {
        event.Skip();
        return;
    }

    switch (action) {
        case Action::Play: m_engine.play(); break;
        case Action::PlaySong: m_engine.play_song(); break;
        case Action::PlayPattern: m_engine.play_pattern(); break;
        case Action::PlayFromPosition: m_engine.play_from_position(m_engine.current_row()); break;
        case Action::Stop: m_engine.stop(); break;
        case Action::Undo: 
            if (m_selected_tab == 1 && m_tracker_panel) m_tracker_panel->get_tracker_view()->handle_action(Action::Undo);
            else m_engine.undo_stack().undo(); 
            break;
        case Action::Redo: 
            if (m_selected_tab == 1 && m_tracker_panel) m_tracker_panel->get_tracker_view()->handle_action(Action::Redo);
            else m_engine.undo_stack().redo(); 
            break;
        case Action::Record:
            m_engine.enable_record(!m_engine.m_record_enabled);
            if (m_transport) m_transport->update();
            break;
        case Action::OctaveUp:
            m_engine.set_base_octave(m_engine.base_octave() + 1);
            if (m_transport) m_transport->update();
            break;
        case Action::OctaveDown:
            m_engine.set_base_octave(m_engine.base_octave() - 1);
            if (m_transport) m_transport->update();
            break;
        case Action::MuteTrack:
            if (m_selected_tab == 1 && m_tracker_panel) {
                int track_idx = m_tracker_panel->get_tracker_view()->get_cursor_track();
                m_engine.track(track_idx).set_mute(!m_engine.track(track_idx).muted());
            }
            break;
        case Action::SoloTrack:
            if (m_selected_tab == 1 && m_tracker_panel) {
                int track_idx = m_tracker_panel->get_tracker_view()->get_cursor_track();
                m_engine.track(track_idx).set_solo(!m_engine.track(track_idx).solo());
            }
            break;
        case Action::NextOrderPos: {
            size_t pos = m_engine.m_edit_order_pos.load();
            if (pos < m_engine.m_order.size() - 1) {
                m_engine.m_edit_order_pos.store(pos + 1);
                m_engine.set_active_pattern(m_engine.m_order[pos + 1]);
                if (m_tracker_panel) m_tracker_panel->update_pattern_list();
            }
            break;
        }
        case Action::PrevOrderPos: {
            size_t pos = m_engine.m_edit_order_pos.load();
            if (pos > 0) {
                m_engine.m_edit_order_pos.store(pos - 1);
                m_engine.set_active_pattern(m_engine.m_order[pos - 1]);
                if (m_tracker_panel) m_tracker_panel->update_pattern_list();
            }
            break;
        }
        case Action::ToggleMetronome: m_engine.toggle_metronome(); break;
        case Action::Cut: 
            if (m_selected_tab == 1 && m_tracker_panel) m_tracker_panel->get_tracker_view()->handle_action(Action::Cut);
            else if (m_selected_tab == 4 && m_instrument_panel) m_instrument_panel->cut(); 
            break;
        case Action::Copy: 
            if (m_selected_tab == 1 && m_tracker_panel) m_tracker_panel->get_tracker_view()->handle_action(Action::Copy);
            else if (m_selected_tab == 4 && m_instrument_panel) m_instrument_panel->copy(); 
            break;
        case Action::Paste: 
            if (m_selected_tab == 1 && m_tracker_panel) m_tracker_panel->get_tracker_view()->handle_action(Action::Paste);
            else if (m_selected_tab == 4 && m_instrument_panel) m_instrument_panel->paste(); 
            break;
        default:
            event.Skip();
            break;
    }
}

void WxMainWindow::OnKeyDown(wxKeyEvent& event) {
    event.Skip();
}

} // namespace tpanar_ns
