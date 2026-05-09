#pragma once

#include <wx/panel.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/gauge.h>
#include <wx/stattext.h>
#include <wx/filepicker.h>
#include <wx/listbox.h>
#include <wx/timer.h>
#include <atomic>

namespace tpanar_ns {

class Engine;
class WxMainWindow;

class ProjectPanel : public wxPanel {
public:
    ProjectPanel(wxWindow* parent, WxMainWindow* main_window, Engine& engine);

    void update_track_list();
    void update_metadata();

private:
    WxMainWindow* m_main_window;
    Engine& m_engine;

    wxPanel* m_left_panel;
    wxPanel* m_right_panel;

    wxButton* m_new_btn;
    wxButton* m_load_btn;
    wxButton* m_import_btn;
    wxButton* m_save_btn;
    wxButton* m_export_btn;
    wxButton* m_export_midi_btn;
    wxDirPickerCtrl* m_dir_picker;
    wxListBox* m_file_list;

    wxTextCtrl* m_title_in;
    wxTextCtrl* m_artist_in;
    wxTextCtrl* m_album_in;
    wxTextCtrl* m_year_in;

    wxChoice* m_sample_rate_ch;
    wxCheckBox* m_separate_tracks_btn;
    wxCheckBox* m_export_click_btn;
    wxCheckBox* m_realtime_btn;
    wxGauge* m_export_progress_bar;
    wxTimer* m_export_timer{nullptr};
    std::atomic<int> m_export_result{-1};

    wxScrolledWindow* m_track_scroll;
    wxPanel* m_track_container;
    wxButton* m_add_track_btn;
    wxButton* m_add_bus_btn;

    void on_new(wxCommandEvent& event);
    void on_load(wxCommandEvent& event);
    void on_import(wxCommandEvent& event);
    void on_save(wxCommandEvent& event);
    void on_export(wxCommandEvent& event);
    void on_export_midi(wxCommandEvent& event);
    void on_export_timer(wxTimerEvent& event);
    void on_file_select(wxCommandEvent& event);
    void on_dir_changed(wxFileDirPickerEvent& event);
    void on_file_dclick(wxCommandEvent& event);
    void update_file_list(const wxString& dir);
    void on_add_track(wxCommandEvent& event);
    void on_remove_track(wxCommandEvent& event);
    void on_track_name(wxCommandEvent& event);
    void on_track_inst(wxCommandEvent& event);
    void on_track_output(wxCommandEvent& event);
    void on_move_track_up(wxCommandEvent& event);
    void on_move_track_down(wxCommandEvent& event);

    void on_add_bus(wxCommandEvent& event);
    void on_remove_bus(wxCommandEvent& event);
    void on_bus_name(wxCommandEvent& event);
    void on_bus_output(wxCommandEvent& event);

    wxDECLARE_EVENT_TABLE();
};

} // namespace tpanar_ns
