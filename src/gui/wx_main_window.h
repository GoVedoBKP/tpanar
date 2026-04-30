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
#include <wx/frame.h>
#include <wx/notebook.h>
#include <wx/timer.h>
#include <wx/menu.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/statusbr.h>

namespace tpanar_ns {

class Engine;
class TransportBar;
class TrackerPanel;
class TracksPanel;
class InstrumentPanel;
class MixerPanel;
class SettingsPanel;
class ProjectPanel;
class HelpPanel;
class Track;

class WxMainWindow : public wxFrame {
public:
    WxMainWindow(int w, int h, const wxString& title, Engine& engine);
    ~WxMainWindow();

    void update_all_uis();
    void request_update();

    Track& track(size_t index);
    int get_cursor_row() const;

    void OnTimer(wxTimerEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnCharHook(wxKeyEvent& event);

private:
    void init_menu();
    void init_panels();

    Engine& m_engine;

    wxTimer* m_timer;
    wxNotebook* m_tabs;
    wxStatusBar* m_status = nullptr;

    TransportBar* m_transport;
    ProjectPanel* m_project_panel;
    TrackerPanel* m_tracker_panel;
    TracksPanel* m_tracks_panel;
    InstrumentPanel* m_instrument_panel;
    MixerPanel* m_mixer_panel;
    SettingsPanel* m_settings_panel;
    HelpPanel* m_help_panel;

    int m_selected_tab;

    DECLARE_EVENT_TABLE()
};

} // namespace tpanar_ns
