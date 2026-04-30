#pragma once

#include <wx/panel.h>
#include <wx/notebook.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/listctrl.h>

namespace tpanar_ns {

class Engine;

class SettingsPanel : public wxPanel {
public:
    SettingsPanel(wxWindow* parent, Engine& engine);

private:
    Engine& m_engine;
    wxNotebook* m_sub_tabs;

    wxPanel* m_audio_grp;
    wxPanel* m_midi_grp;
    wxPanel* m_mixer_grp;
    wxPanel* m_gui_grp;
    wxPanel* m_kbd_grp;
    wxPanel* m_misc_grp;

    wxChoice*   m_audio_backend;
    wxSpinCtrl* m_audio_ins;
    wxSpinCtrl* m_audio_outs;
    wxChoice*   m_worker_threads;
    wxChoice*   m_record_preroll;
    wxChoice*   m_export_lead_in;
    wxButton* m_reinit_audio_btn;
    wxStaticText* m_audio_status;

    wxSpinCtrl* m_midi_ins;
    wxSpinCtrl* m_midi_outs;
    wxButton* m_reinit_midi_btn;

    wxChoice* m_gui_theme;
    wxChoice* m_gui_btn_h;
    wxChoice* m_gui_font_size;
    wxButton* m_waveform_color_btn;
    wxButton* m_bg_color_btn;
    wxButton* m_fg_color_btn;
    wxChoice* m_boxtype_choice;
    wxChoice* m_btn_boxtype_choice;

    wxChoice* m_kbd_layout;
    wxListCtrl* m_shortcut_list;   // scrollable shortcut reference list
    wxButton*   m_assign_btn;

    void init_audio_grp(int x, int y, int w, int h);
    void init_midi_grp(int x, int y, int w, int h);
    void init_mixer_grp(int x, int y, int w, int h);
    void init_gui_grp(int x, int y, int w, int h);
    void init_kbd_grp(int x, int y, int w, int h);
    void init_misc_grp(int x, int y, int w, int h);
    void populate_shortcut_list();
    void refresh_audio_status();
    void sync_audio_controls();

    void on_reinit_audio(wxCommandEvent& event);
    void on_reinit_midi(wxCommandEvent& event);
    void on_gui_theme(wxCommandEvent& event);
    void on_gui_btn_h(wxCommandEvent& event);
    void on_gui_font_size(wxCommandEvent& event);
    void on_waveform_color(wxCommandEvent& event);
    void on_bg_color(wxCommandEvent& event);
    void on_fg_color(wxCommandEvent& event);
    void on_boxtype(wxCommandEvent& event);
    void on_btn_boxtype(wxCommandEvent& event);
    void on_kbd_layout(wxCommandEvent& event);
    void on_assign_key(wxCommandEvent& event);
    void on_reset_keys(wxCommandEvent& event);
    void on_save_settings(wxCommandEvent& event);
    void on_load_settings(wxCommandEvent& event);
    void on_export_settings(wxCommandEvent& event);
    void on_import_settings(wxCommandEvent& event);

    wxDECLARE_EVENT_TABLE();
};

} // namespace tpanar_ns
