#include "wx_settings_panel.h"
#include "theme.h"
#include "wx_main_window.h"
#include "../core/engine.h"
#include "../core/config_manager.h"

#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/colordlg.h>
#include <wx/artprov.h>
#include <wx/dialog.h>

namespace tpanar_ns {

namespace {

int backend_choice_index(AudioBackendType type)
{
    return type == AudioBackendType::Oss ? 1 : 0;
}

AudioBackendType backend_choice_type(int idx)
{
    return idx == 1 ? AudioBackendType::Oss : AudioBackendType::Jack;
}

int bars_choice_index(int bars)
{
    if (bars >= 2) return 2;
    if (bars == 1) return 1;
    return 0;
}

int bars_choice_value(int selection)
{
    if (selection >= 2) return 2;
    if (selection == 1) return 1;
    return 0;
}

} // namespace

// Modal dialog that waits for the user to press a single key combination.
class KeyCaptureDialog : public wxDialog {
public:
    KeyCaptureDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, "Assign Key",
                   wxDefaultPosition, wxSize(300, 120),
                   wxDEFAULT_DIALOG_STYLE & ~wxCLOSE_BOX)
    {
        m_key = -1;
        m_mods = 0;

        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        m_label = new wxStaticText(this, wxID_ANY, "Press a key combination...",
                                   wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
        sizer->Add(m_label, 1, wxALL | wxEXPAND, 12);

        wxButton* cancel = new wxButton(this, wxID_CANCEL, "Cancel");
        sizer->Add(cancel, 0, wxALIGN_CENTER | wxBOTTOM, 8);
        SetSizer(sizer);

        // Capture key events on the dialog itself
        Bind(wxEVT_KEY_DOWN, &KeyCaptureDialog::on_key, this);
        m_label->Bind(wxEVT_KEY_DOWN, &KeyCaptureDialog::on_key, this);
        SetFocus();
    }

    int captured_key()  const { return m_key; }
    int captured_mods() const { return m_mods; }

private:
    wxStaticText* m_label;
    int m_key;
    int m_mods;

    void on_key(wxKeyEvent& event) {
        int key = event.GetKeyCode();
        // Ignore bare modifier presses
        if (key == WXK_SHIFT || key == WXK_CONTROL || key == WXK_ALT) {
            event.Skip();
            return;
        }
        m_key  = key;
        m_mods = 0;
        if (event.ShiftDown())   m_mods |= 1;
        if (event.ControlDown()) m_mods |= 2;
        if (event.AltDown())     m_mods |= 4;

        // Show what was captured before closing
        wxString label;
        if (m_mods & 2) label += "Ctrl+";
        if (m_mods & 1) label += "Shift+";
        if (m_mods & 4) label += "Alt+";
        if (key >= 32 && key < 127)
            label += wxChar(key);
        else
            label += wxString::Format("<%d>", key);
        m_label->SetLabel(label);

        EndModal(wxID_OK);
    }
};

wxBEGIN_EVENT_TABLE(SettingsPanel, wxPanel)
wxEND_EVENT_TABLE()

SettingsPanel::SettingsPanel(wxWindow* parent, Engine& engine)
    : wxPanel(parent, wxID_ANY), m_engine(engine)
{
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    m_sub_tabs = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);

    init_audio_grp(0, 0, 400, 300);
    init_midi_grp(0, 0, 400, 300);
    m_mixer_grp = nullptr;
    init_gui_grp(0, 0, 400, 300);
    init_kbd_grp(0, 0, 400, 300);
    init_misc_grp(0, 0, 400, 300);

    main_sizer->Add(m_sub_tabs, 1, wxEXPAND | wxALL, 0);
    SetSizer(main_sizer);
    sync_audio_controls();
}

void SettingsPanel::init_audio_grp(int x, int y, int w, int h) {
    m_audio_grp = new wxPanel(m_sub_tabs, wxID_ANY);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_audio_grp, wxID_ANY, "Backend:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    m_audio_backend = new wxChoice(m_audio_grp, wxID_ANY);
    m_audio_backend->Append("JACK");
    m_audio_backend->Append("OSS (/dev/dsp)");
    m_audio_backend->SetSelection(backend_choice_index(m_engine.configured_audio_backend_type()));
    row->Add(m_audio_backend, 0, wxALL, 2);
    sizer->Add(row, 0, wxALL, 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_audio_grp, wxID_ANY, "Input Channels:"), 0, wxALL, 2);
    m_audio_ins = new wxSpinCtrl(m_audio_grp, wxID_ANY, "2", wxDefaultPosition, wxSize(60, 25));
    m_audio_ins->SetRange(0, 32);
    m_audio_ins->SetValue(m_engine.m_num_ins);
    row->Add(m_audio_ins, 0, wxALL, 2);
    sizer->Add(row, 0, wxALL, 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_audio_grp, wxID_ANY, "Output Channels:"), 0, wxALL, 2);
    m_audio_outs = new wxSpinCtrl(m_audio_grp, wxID_ANY, "2", wxDefaultPosition, wxSize(60, 25));
    m_audio_outs->SetRange(0, 32);
    m_audio_outs->SetValue(m_engine.m_num_outs);
    row->Add(m_audio_outs, 0, wxALL, 2);
    sizer->Add(row, 0, wxALL, 2);

    m_reinit_audio_btn = new wxButton(m_audio_grp, wxID_ANY, "Reinitialize Audio", wxDefaultPosition, wxSize(150, 30));
    m_reinit_audio_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_REFRESH, wxART_BUTTON, wxSize(16, 16)));
    m_reinit_audio_btn->Bind(wxEVT_BUTTON, &SettingsPanel::on_reinit_audio, this);
    sizer->Add(m_reinit_audio_btn, 0, wxALL, 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_audio_grp, wxID_ANY, "Worker Threads:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    m_worker_threads = new wxChoice(m_audio_grp, wxID_ANY);
    m_worker_threads->Append("Off (single-threaded)");
    m_worker_threads->Append("1");
    m_worker_threads->Append("2");
    m_worker_threads->Append("3");
    m_worker_threads->Append("4");
    m_worker_threads->Append("6");
    m_worker_threads->Append("8");
    m_worker_threads->Append("Auto");
    {
        uint32_t n = ConfigManager::instance().config().num_worker_threads;
        int sel = 0;
        if (n == 255) sel = 7;
        else if (n == 8) sel = 6;
        else if (n == 6) sel = 5;
        else if (n == 4) sel = 4;
        else if (n == 3) sel = 3;
        else if (n == 2) sel = 2;
        else if (n == 1) sel = 1;
        m_worker_threads->SetSelection(sel);
    }
    m_worker_threads->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        static const uint32_t thread_vals[] = {0, 1, 2, 3, 4, 6, 8, 255};
        int sel = m_worker_threads->GetSelection();
        if (sel >= 0 && sel < 8) {
            m_engine.set_worker_threads(thread_vals[sel]);
            ConfigManager::instance().save();
        }
    });
    row->Add(m_worker_threads, 0, wxALL, 2);
    sizer->Add(row, 0, wxALL, 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_audio_grp, wxID_ANY, "Record Count-in:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    m_record_preroll = new wxChoice(m_audio_grp, wxID_ANY);
    m_record_preroll->Append("Off");
    m_record_preroll->Append("1 bar");
    m_record_preroll->Append("2 bars");
    m_record_preroll->SetSelection(bars_choice_index(m_engine.record_preroll_bars()));
    m_record_preroll->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        m_engine.set_record_preroll_bars(bars_choice_value(m_record_preroll->GetSelection()));
        m_engine.save_config();
    });
    row->Add(m_record_preroll, 0, wxALL, 2);
    sizer->Add(row, 0, wxALL, 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_audio_grp, wxID_ANY, "Export Lead-in:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    m_export_lead_in = new wxChoice(m_audio_grp, wxID_ANY);
    m_export_lead_in->Append("Off");
    m_export_lead_in->Append("1 bar");
    m_export_lead_in->Append("2 bars");
    m_export_lead_in->SetSelection(bars_choice_index(m_engine.export_lead_in_bars()));
    m_export_lead_in->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        m_engine.set_export_lead_in_bars(bars_choice_value(m_export_lead_in->GetSelection()));
        m_engine.save_config();
    });
    row->Add(m_export_lead_in, 0, wxALL, 2);
    sizer->Add(row, 0, wxALL, 2);

    sizer->Add(new wxStaticText(m_audio_grp, wxID_ANY,
        "Apply while stopped for best results."), 0, wxLEFT, 6);
    sizer->Add(new wxStaticText(m_audio_grp, wxID_ANY,
        "JACK exposes audio+MIDI ports; OSS uses /dev/dsp playback."), 0, wxLEFT | wxTOP, 6);

    m_audio_status = new wxStaticText(m_audio_grp, wxID_ANY, "Audio Status: Ready");
    sizer->Add(m_audio_status, 0, wxALL, 2);

    m_audio_grp->SetSizer(sizer);
    m_sub_tabs->AddPage(m_audio_grp, "Audio");
}

void SettingsPanel::init_midi_grp(int x, int y, int w, int h) {
    m_midi_grp = new wxPanel(m_sub_tabs, wxID_ANY);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_midi_grp, wxID_ANY, "Input Devices:"), 0, wxALL, 2);
    m_midi_ins = new wxSpinCtrl(m_midi_grp, wxID_ANY, "0", wxDefaultPosition, wxSize(60, 25));
    m_midi_ins->SetRange(0, 16);
    row->Add(m_midi_ins, 0, wxALL, 2);
    sizer->Add(row, 0, wxALL, 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_midi_grp, wxID_ANY, "Output Devices:"), 0, wxALL, 2);
    m_midi_outs = new wxSpinCtrl(m_midi_grp, wxID_ANY, "0", wxDefaultPosition, wxSize(60, 25));
    m_midi_outs->SetRange(0, 16);
    row->Add(m_midi_outs, 0, wxALL, 2);
    sizer->Add(row, 0, wxALL, 2);

    m_reinit_midi_btn = new wxButton(m_midi_grp, wxID_ANY, "Reinitialize MIDI", wxDefaultPosition, wxSize(150, 30));
    m_reinit_midi_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_REFRESH, wxART_BUTTON, wxSize(16, 16)));
    m_reinit_midi_btn->Bind(wxEVT_BUTTON, &SettingsPanel::on_reinit_midi, this);
    sizer->Add(m_reinit_midi_btn, 0, wxALL, 2);

    m_midi_grp->SetSizer(sizer);
    m_sub_tabs->AddPage(m_midi_grp, "MIDI");
}

void SettingsPanel::refresh_audio_status()
{
    const AudioBackendType active = m_engine.active_audio_backend_type();
    if (active == AudioBackendType::Null) {
        m_audio_status->SetLabel("Audio Status: fallback active (no hardware backend)");
        m_audio_status->SetForegroundColour(*wxRED);
        return;
    }

    m_audio_status->SetLabel(wxString::Format("Audio Status: %s active",
        audio_backend_type_label(active)));
    m_audio_status->SetForegroundColour(wxColour(0, 160, 0));
}

void SettingsPanel::sync_audio_controls()
{
    m_audio_backend->SetSelection(backend_choice_index(m_engine.configured_audio_backend_type()));
    m_audio_ins->SetValue(static_cast<int>(m_engine.m_num_ins));
    m_audio_outs->SetValue(static_cast<int>(m_engine.m_num_outs));
    m_midi_ins->SetValue(static_cast<int>(m_engine.m_num_midi_ins));
    m_midi_outs->SetValue(static_cast<int>(m_engine.m_num_midi_outs));
    m_record_preroll->SetSelection(bars_choice_index(m_engine.record_preroll_bars()));
    m_export_lead_in->SetSelection(bars_choice_index(m_engine.export_lead_in_bars()));
    refresh_audio_status();
}

void SettingsPanel::init_mixer_grp(int x, int y, int w, int h) {
    // No mixer settings currently — tab is hidden
}

void SettingsPanel::init_gui_grp(int x, int y, int w, int h) {
    m_gui_grp = new wxPanel(m_sub_tabs, wxID_ANY);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_gui_grp, wxID_ANY, "Theme:"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    m_gui_theme = new wxChoice(m_gui_grp, wxID_ANY);
    
    const auto& themes = ThemeManager::get_available_themes();
    for (const auto& t : themes) {
        m_gui_theme->Append(t.name);
    }
    m_gui_theme->SetSelection((int)m_engine.m_gui_theme);
    m_gui_theme->Bind(wxEVT_CHOICE, &SettingsPanel::on_gui_theme, this);

    row->Add(m_gui_theme, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    sizer->Add(row, 0, wxEXPAND | wxALL, 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_gui_grp, wxID_ANY, "Button Height:"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    m_gui_btn_h = new wxChoice(m_gui_grp, wxID_ANY);
    m_gui_btn_h->Append("20");
    m_gui_btn_h->Append("25");
    m_gui_btn_h->Append("30");
    m_gui_btn_h->SetSelection(m_engine.m_gui_button_height == 20 ? 0 : (m_engine.m_gui_button_height == 30 ? 2 : 1));
    m_gui_btn_h->Bind(wxEVT_CHOICE, &SettingsPanel::on_gui_btn_h, this);
    row->Add(m_gui_btn_h, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    sizer->Add(row, 0, wxEXPAND | wxALL, 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_gui_grp, wxID_ANY, "Font Size:"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    m_gui_font_size = new wxChoice(m_gui_grp, wxID_ANY);
    m_gui_font_size->Append("10");
    m_gui_font_size->Append("12");
    m_gui_font_size->Append("14");
    m_gui_font_size->SetSelection(m_engine.m_gui_font_size == 10 ? 0 : (m_engine.m_gui_font_size == 14 ? 2 : 1));
    m_gui_font_size->Bind(wxEVT_CHOICE, &SettingsPanel::on_gui_font_size, this);
    row->Add(m_gui_font_size, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    sizer->Add(row, 0, wxEXPAND | wxALL, 2);

    row = new wxBoxSizer(wxHORIZONTAL);
    m_bg_color_btn = new wxButton(m_gui_grp, wxID_ANY, "Background Color", wxDefaultPosition, wxSize(-1, 25));
    m_bg_color_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FIND, wxART_BUTTON, wxSize(16, 16)));
    m_bg_color_btn->Bind(wxEVT_BUTTON, &SettingsPanel::on_bg_color, this);
    
    m_fg_color_btn = new wxButton(m_gui_grp, wxID_ANY, "Foreground Color", wxDefaultPosition, wxSize(-1, 25));
    m_fg_color_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FIND, wxART_BUTTON, wxSize(16, 16)));
    m_fg_color_btn->Bind(wxEVT_BUTTON, &SettingsPanel::on_fg_color, this);
    
    m_waveform_color_btn = new wxButton(m_gui_grp, wxID_ANY, "Waveform Color", wxDefaultPosition, wxSize(-1, 25));
    m_waveform_color_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FIND, wxART_BUTTON, wxSize(16, 16)));
    m_waveform_color_btn->Bind(wxEVT_BUTTON, &SettingsPanel::on_waveform_color, this);
    
    row->Add(m_bg_color_btn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    row->Add(m_fg_color_btn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    row->Add(m_waveform_color_btn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    sizer->Add(row, 0, wxEXPAND | wxALL, 2);

    m_gui_grp->SetSizer(sizer);
    m_sub_tabs->AddPage(m_gui_grp, "GUI");
}

void SettingsPanel::init_kbd_grp(int x, int y, int w, int h) {
    m_kbd_grp = new wxPanel(m_sub_tabs, wxID_ANY);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    // Layout selector row
    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(m_kbd_grp, wxID_ANY, "Keyboard Layout:"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    m_kbd_layout = new wxChoice(m_kbd_grp, wxID_ANY);
    m_kbd_layout->Append("QWERTY");
    m_kbd_layout->Append("AZERTY");
    m_kbd_layout->Append("QWERTZ");

    KeyboardLayout layout = m_engine.m_key_bindings.get_layout();
    if (layout == KeyboardLayout::QWERTZ) m_kbd_layout->SetSelection(2);
    else m_kbd_layout->SetSelection(0);

    m_kbd_layout->Bind(wxEVT_CHOICE, &SettingsPanel::on_kbd_layout, this);
    row->Add(m_kbd_layout, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    sizer->Add(row, 0, wxEXPAND | wxALL, 2);

    // Shortcut reference list
    sizer->Add(new wxStaticText(m_kbd_grp, wxID_ANY, "Keyboard Shortcuts:"), 0, wxLEFT | wxTOP, 4);

    m_shortcut_list = new wxListCtrl(m_kbd_grp, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SIMPLE);
    m_shortcut_list->InsertColumn(0, "Action",  wxLIST_FORMAT_LEFT, 220);
    m_shortcut_list->InsertColumn(1, "Key",     wxLIST_FORMAT_LEFT, 140);
    sizer->Add(m_shortcut_list, 1, wxEXPAND | wxALL, 4);

    // Assign Key button row
    wxBoxSizer* btn_row = new wxBoxSizer(wxHORIZONTAL);
    m_assign_btn = new wxButton(m_kbd_grp, wxID_ANY, "Assign Key",
                                wxDefaultPosition, wxSize(-1, 28));
    m_assign_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_BUTTON, wxSize(16, 16)));
    m_assign_btn->SetToolTip("Select a row above, then click to assign a new key");
    m_assign_btn->Bind(wxEVT_BUTTON, &SettingsPanel::on_assign_key, this);
    btn_row->Add(m_assign_btn, 0, wxALL, 4);

    wxButton* reset_btn = new wxButton(m_kbd_grp, wxID_ANY, "Reset to Defaults",
                                       wxDefaultPosition, wxSize(-1, 28));
    reset_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_UNDO, wxART_BUTTON, wxSize(16, 16)));
    reset_btn->SetToolTip("Restore all key bindings to their defaults");
    reset_btn->Bind(wxEVT_BUTTON, &SettingsPanel::on_reset_keys, this);
    btn_row->Add(reset_btn, 0, wxALL, 4);

    sizer->Add(btn_row, 0, wxEXPAND);

    m_kbd_grp->SetSizer(sizer);
    m_sub_tabs->AddPage(m_kbd_grp, "Keyboard");

    populate_shortcut_list();
}

void SettingsPanel::populate_shortcut_list() {
    m_shortcut_list->DeleteAllItems();

    // Category separators: pairs of (first action in group, label)
    struct Category {
        Action first;
        const char* label;
    };
    const Category cats[] = {
        { Action::Play,       "--- Transport ---" },
        { Action::Undo,       "--- Edit ---" },
        { Action::MoveUp,     "--- Navigation ---" },
        { Action::InsertRow,  "--- Pattern ---" },
        { Action::MuteTrack,  "--- Track ---" },
        { Action::OctaveUp,   "--- Octave ---" },
        { Action::NoteC,      "--- Notes (lower octave) ---" },
        { Action::NoteC2,     "--- Notes (upper octave) ---" },
    };
    const int n_cats = static_cast<int>(sizeof(cats) / sizeof(cats[0]));

    long row = 0;
    for (Action action : KeyBindings::all_actions()) {
        // Insert category header if this action starts a new group
        for (int c = 0; c < n_cats; ++c) {
            if (cats[c].first == action) {
                long idx = m_shortcut_list->InsertItem(row, wxString(cats[c].label));
                m_shortcut_list->SetItem(idx, 1, wxEmptyString);
                m_shortcut_list->SetItemData(idx, -1); // marks header row
                ++row;
                break;
            }
        }
        std::string name = m_engine.m_key_bindings.get_action_name(action);
        std::string key  = m_engine.m_key_bindings.get_key_name(action);
        long idx = m_shortcut_list->InsertItem(row, wxString::FromUTF8(("  " + name).c_str()));
        m_shortcut_list->SetItem(idx, 1, wxString::FromUTF8(key.c_str()));
        m_shortcut_list->SetItemData(idx, static_cast<long>(action));
        ++row;
    }
}

void SettingsPanel::init_misc_grp(int x, int y, int w, int h) {
    m_misc_grp = new wxPanel(m_sub_tabs, wxID_ANY);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    // --- Default config file path ---
    sizer->Add(new wxStaticText(m_misc_grp, wxID_ANY, "Default settings file:"), 0, wxALL, 4);

    std::string cfg_path = ConfigManager::instance().config_path();
    wxStaticText* path_lbl = new wxStaticText(m_misc_grp, wxID_ANY,
        wxString::FromUTF8(cfg_path.c_str()),
        wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_START);
    path_lbl->SetFont(wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    sizer->Add(path_lbl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

    sizer->Add(new wxStaticLine(m_misc_grp), 0, wxEXPAND | wxALL, 4);

    // --- Save / Load defaults ---
    wxBoxSizer* row1 = new wxBoxSizer(wxHORIZONTAL);

    wxButton* save_btn = new wxButton(m_misc_grp, wxID_ANY, "Save Settings", wxDefaultPosition, wxSize(-1, 28));
    save_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_BUTTON, wxSize(16, 16)));
    save_btn->SetToolTip("Save current settings to the default file (auto-saved on exit)");
    save_btn->Bind(wxEVT_BUTTON, &SettingsPanel::on_save_settings, this);
    row1->Add(save_btn, 0, wxALL, 4);

    wxButton* load_btn = new wxButton(m_misc_grp, wxID_ANY, "Load Settings", wxDefaultPosition, wxSize(-1, 28));
    load_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_BUTTON, wxSize(16, 16)));
    load_btn->SetToolTip("Reload settings from the default file and apply them");
    load_btn->Bind(wxEVT_BUTTON, &SettingsPanel::on_load_settings, this);
    row1->Add(load_btn, 0, wxALL, 4);

    sizer->Add(row1, 0, wxALL, 0);

    sizer->Add(new wxStaticLine(m_misc_grp), 0, wxEXPAND | wxALL, 4);

    // --- Export / Import to custom file ---
    sizer->Add(new wxStaticText(m_misc_grp, wxID_ANY, "Settings file portability:"), 0, wxALL, 4);

    wxBoxSizer* row2 = new wxBoxSizer(wxHORIZONTAL);

    wxButton* export_btn = new wxButton(m_misc_grp, wxID_ANY, "Export Settings...", wxDefaultPosition, wxSize(-1, 28));
    export_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE_AS, wxART_BUTTON, wxSize(16, 16)));
    export_btn->SetToolTip("Save settings to a custom file");
    export_btn->Bind(wxEVT_BUTTON, &SettingsPanel::on_export_settings, this);
    row2->Add(export_btn, 0, wxALL, 4);

    wxButton* import_btn = new wxButton(m_misc_grp, wxID_ANY, "Import Settings...", wxDefaultPosition, wxSize(-1, 28));
    import_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_BUTTON, wxSize(16, 16)));
    import_btn->SetToolTip("Load settings from a custom file");
    import_btn->Bind(wxEVT_BUTTON, &SettingsPanel::on_import_settings, this);
    row2->Add(import_btn, 0, wxALL, 4);

    sizer->Add(row2, 0, wxALL, 0);

    sizer->AddStretchSpacer(1);

    sizer->Add(new wxStaticText(m_misc_grp, wxID_ANY,
        "Note: Audio/MIDI changes take effect after restarting audio."),
        0, wxALL, 6);

    m_misc_grp->SetSizer(sizer);
    m_sub_tabs->AddPage(m_misc_grp, "Misc");
}

void SettingsPanel::on_save_settings(wxCommandEvent&) {
    m_engine.save_config();
    wxMessageBox("Settings saved to:\n" +
        wxString::FromUTF8(ConfigManager::instance().config_path().c_str()),
        "Settings Saved", wxOK | wxICON_INFORMATION);
}

void SettingsPanel::on_load_settings(wxCommandEvent&) {
    ConfigManager::instance().load();
    m_engine.load_config();
    ThemeManager::apply_theme_and_settings(m_engine);
    sync_audio_controls();
    wxWindow* top = wxGetTopLevelParent(this);
    if (top) { top->Refresh(); top->Update(); }
    wxMessageBox("Settings loaded and applied.", "Settings Loaded", wxOK | wxICON_INFORMATION);
}

void SettingsPanel::on_export_settings(wxCommandEvent&) {
    wxFileDialog dlg(this, "Export Settings", "", "tpanar_settings.json",
        "JSON files (*.json)|*.json|All files (*.*)|*.*",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_OK) {
        m_engine.save_config(); // Update config from engine first
        ConfigManager::instance().save_to(dlg.GetPath().ToStdString());
        wxMessageBox("Settings exported to:\n" + dlg.GetPath(),
            "Export Complete", wxOK | wxICON_INFORMATION);
    }
}

void SettingsPanel::on_import_settings(wxCommandEvent&) {
    wxFileDialog dlg(this, "Import Settings", "", "",
        "JSON files (*.json)|*.json|All files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        ConfigManager::instance().load_from(dlg.GetPath().ToStdString());
        m_engine.load_config();
        ThemeManager::apply_theme_and_settings(m_engine);
        sync_audio_controls();
        wxWindow* top = wxGetTopLevelParent(this);
        if (top) { top->Refresh(); top->Update(); }
        wxMessageBox("Settings imported from:\n" + dlg.GetPath() +
            "\n\nAudio/MIDI/backend changes will take effect after restarting audio.",
            "Import Complete", wxOK | wxICON_INFORMATION);
    }
}

void SettingsPanel::on_reinit_audio(wxCommandEvent& event) {
    m_engine.set_audio_backend_type(backend_choice_type(m_audio_backend->GetSelection()));
    m_engine.reinitialize_audio(m_audio_ins->GetValue(), m_audio_outs->GetValue(), m_midi_ins->GetValue(), m_midi_outs->GetValue());
    m_engine.save_config();
    refresh_audio_status();
    wxMessageBox("Audio settings updated. JACK may require reconnecting ports; OSS uses /dev/dsp playback only.", "Info", wxOK | wxICON_INFORMATION);
}

void SettingsPanel::on_reinit_midi(wxCommandEvent& event) {
    on_reinit_audio(event);
}

void SettingsPanel::on_gui_theme(wxCommandEvent& event) {
    int idx = event.GetSelection();
    if (idx != wxNOT_FOUND) {
        m_engine.m_gui_theme = (ThemeType)idx;
        ThemeManager::apply_theme_and_settings(m_engine);
        m_engine.save_config();
        
        // Refresh entire UI
        wxWindow* top = wxGetTopLevelParent(this);
        if (top) {
            top->Refresh();
            // Force update of children if needed
            top->Update();
        }
    }
}

void SettingsPanel::on_gui_btn_h(wxCommandEvent& event) {
    int idx = event.GetSelection();
    if (idx != wxNOT_FOUND) {
        wxString s = m_gui_btn_h->GetString(idx);
        long val;
        if (s.ToLong(&val)) {
            m_engine.m_gui_button_height = (int)val;
            m_engine.save_config();
        }
    }
}

void SettingsPanel::on_gui_font_size(wxCommandEvent& event) {
    int idx = event.GetSelection();
    if (idx != wxNOT_FOUND) {
        wxString s = m_gui_font_size->GetString(idx);
        long val;
        if (s.ToLong(&val)) {
            m_engine.m_gui_font_size = (int)val;
            m_engine.save_config();
        }
    }
}

void SettingsPanel::on_waveform_color(wxCommandEvent& event) {
    wxColourData data;
    data.SetColour(ThemeManager::toWxColour(m_engine.m_waveform_color));
    wxColourDialog dlg(this, &data);
    if (dlg.ShowModal() == wxID_OK) {
        wxColour c = dlg.GetColourData().GetColour();
        m_engine.m_waveform_color = (c.Red() << 24) | (c.Green() << 16) | (c.Blue() << 8) | 255;
        m_engine.save_config();
        Refresh();
    }
}

void SettingsPanel::on_bg_color(wxCommandEvent& event) {
    wxColourData data;
    data.SetColour(ThemeManager::toWxColour(m_engine.m_bg_color));
    wxColourDialog dlg(this, &data);
    if (dlg.ShowModal() == wxID_OK) {
        wxColour c = dlg.GetColourData().GetColour();
        m_engine.m_bg_color = (c.Red() << 24) | (c.Green() << 16) | (c.Blue() << 8) | 255;
        m_engine.m_gui_theme = ThemeType::Custom;
        m_gui_theme->SetSelection((int)ThemeType::Custom);
        m_engine.save_config();
        
        wxWindow* top = wxGetTopLevelParent(this);
        if (top) top->Refresh();
    }
}

void SettingsPanel::on_fg_color(wxCommandEvent& event) {
    wxColourData data;
    data.SetColour(ThemeManager::toWxColour(m_engine.m_fg_color));
    wxColourDialog dlg(this, &data);
    if (dlg.ShowModal() == wxID_OK) {
        wxColour c = dlg.GetColourData().GetColour();
        m_engine.m_fg_color = (c.Red() << 24) | (c.Green() << 16) | (c.Blue() << 8) | 255;
        m_engine.m_gui_theme = ThemeType::Custom;
        m_gui_theme->SetSelection((int)ThemeType::Custom);
        m_engine.save_config();
        
        wxWindow* top = wxGetTopLevelParent(this);
        if (top) top->Refresh();
    }
}

void SettingsPanel::on_kbd_layout(wxCommandEvent& event) {
    int idx = event.GetSelection();
    if (idx == 0) m_engine.m_key_bindings.set_layout(KeyboardLayout::QWERTY);
    else if (idx == 2) m_engine.m_key_bindings.set_layout(KeyboardLayout::QWERTZ);

    populate_shortcut_list();
    m_engine.save_config();
}

void SettingsPanel::on_assign_key(wxCommandEvent& event) {
    long sel = m_shortcut_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel == wxNOT_FOUND) {
        wxMessageBox("Select a shortcut row first.", "Assign Key", wxOK | wxICON_INFORMATION, this);
        return;
    }
    long data = m_shortcut_list->GetItemData(sel);
    if (data == -1) {
        wxMessageBox("Select an action row (not a category header).", "Assign Key", wxOK | wxICON_INFORMATION, this);
        return;
    }
    Action action = static_cast<Action>(data);

    KeyCaptureDialog dlg(this);
    if (dlg.ShowModal() != wxID_OK) return;

    int key  = dlg.captured_key();
    int mods = dlg.captured_mods();
    m_engine.m_key_bindings.assign(action, key, mods);
    populate_shortcut_list();
    // Re-select the same row (row index may have shifted; find by data)
    for (long i = 0; i < m_shortcut_list->GetItemCount(); ++i) {
        if (m_shortcut_list->GetItemData(i) == static_cast<long>(action)) {
            m_shortcut_list->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            m_shortcut_list->EnsureVisible(i);
            break;
        }
    }
    m_engine.save_config();
}

void SettingsPanel::on_reset_keys(wxCommandEvent& event) {
    if (wxMessageBox("Reset all key bindings to defaults?", "Reset Keys",
                     wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
    m_engine.m_key_bindings.set_defaults();
    populate_shortcut_list();
    m_engine.save_config();
}

} // namespace tpanar_ns
