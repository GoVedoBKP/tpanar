#include "wx_project_panel.h"
#include "wx_main_window.h"
#include "../core/engine.h"
#include "../instrument/sample_instrument.h"

#include <wx/sizer.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/dir.h>
#include <wx/artprov.h>
#include <wx/spinctrl.h>
#include <wx/timer.h>
#include <wx/choicdlg.h>
#include <wx/filename.h>
#include <thread>
#include <atomic>

namespace tpanar_ns {

namespace {

bool instrument_compatible_with_track(TrackKind kind, InstrumentType type)
{
    switch (kind) {
        case TrackKind::Audio:
            return type == InstrumentType::Sampler;
        case TrackKind::Note:
            return type == InstrumentType::SoundFont || type == InstrumentType::SFZ;
        case TrackKind::Pilot:
            return type == InstrumentType::Sampler;
    }
    return false;
}

wxString drum_type_label(DrumType type)
{
    const auto& info = drum_type_info(type);
    if (info.min_hz <= 0.0f || info.max_hz <= 0.0f) {
        return info.name;
    }
    return wxString::Format("%s [%.0f-%.0f Hz]", info.name, info.min_hz, info.max_hz);
}

wxString ensure_extension(const wxString& path, const wxString& extension)
{
    wxFileName filename(path);
    if (!filename.HasExt()) {
        filename.SetExt(extension);
    }
    return filename.GetFullPath();
}

} // namespace

wxBEGIN_EVENT_TABLE(ProjectPanel, wxPanel)
    EVT_BUTTON(wxID_NEW, ProjectPanel::on_new)
    EVT_BUTTON(wxID_OPEN, ProjectPanel::on_load)
    EVT_BUTTON(wxID_FILE1, ProjectPanel::on_import)
    EVT_BUTTON(wxID_SAVE, ProjectPanel::on_save)
    EVT_BUTTON(wxID_FILE2, ProjectPanel::on_export)
wxEND_EVENT_TABLE()

ProjectPanel::ProjectPanel(wxWindow* parent, WxMainWindow* main_window, Engine& engine)
    : wxPanel(parent, wxID_ANY), m_main_window(main_window), m_engine(engine)
{
    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_left_panel = new wxPanel(this, wxID_ANY);
    wxBoxSizer* left_sizer = new wxBoxSizer(wxVERTICAL);

    // File Operations Group
    wxStaticBoxSizer* file_group = new wxStaticBoxSizer(wxVERTICAL, m_left_panel, "File Operations");
    wxFlexGridSizer* file_btn_grid = new wxFlexGridSizer(2, 3, 5, 5);
    file_btn_grid->AddGrowableCol(0);
    file_btn_grid->AddGrowableCol(1);
    file_btn_grid->AddGrowableCol(2);

    auto create_btn = [&](wxWindowID id, const wxString& label) {
        return new wxButton(m_left_panel, id, label, wxDefaultPosition, wxSize(-1, 30));
    };

    m_new_btn = create_btn(wxID_NEW, "New");
    m_load_btn = create_btn(wxID_OPEN, "Load");
    m_import_btn = create_btn(wxID_FILE1, "Import");
    m_save_btn = create_btn(wxID_SAVE, "Save");
    m_export_btn = create_btn(wxID_FILE2, "Export");

    file_btn_grid->Add(m_new_btn, 1, wxEXPAND);
    file_btn_grid->Add(m_load_btn, 1, wxEXPAND);
    file_btn_grid->Add(m_import_btn, 1, wxEXPAND);
    file_btn_grid->Add(m_save_btn, 1, wxEXPAND);
    file_btn_grid->Add(m_export_btn, 1, wxEXPAND);
    
    file_group->Add(file_btn_grid, 0, wxEXPAND | wxALL, 5);
    left_sizer->Add(file_group, 0, wxEXPAND | wxALL, 5);

    // Browser Group
    wxStaticBoxSizer* browser_group = new wxStaticBoxSizer(wxVERTICAL, m_left_panel, "Project Browser");
    m_dir_picker = new wxDirPickerCtrl(m_left_panel, wxID_ANY, ".", "Choose Directory", wxDefaultPosition, wxDefaultSize, wxDIRP_DEFAULT_STYLE | wxDIRP_SMALL);
    m_dir_picker->Bind(wxEVT_DIRPICKER_CHANGED, &ProjectPanel::on_dir_changed, this);
    browser_group->Add(m_dir_picker, 0, wxEXPAND | wxALL, 2);

    m_file_list = new wxListBox(m_left_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_file_list->Bind(wxEVT_LISTBOX_DCLICK, &ProjectPanel::on_file_dclick, this);
    browser_group->Add(m_file_list, 1, wxEXPAND | wxALL, 2);
    left_sizer->Add(browser_group, 1, wxEXPAND | wxALL, 5);

    update_file_list(".");

    // Project Metadata Fields
    wxStaticBoxSizer* meta_sizer = new wxStaticBoxSizer(wxVERTICAL, m_left_panel, "Project Metadata");
    wxFlexGridSizer* meta_grid = new wxFlexGridSizer(4, 2, 5, 5);
    meta_grid->AddGrowableCol(1);
    
    auto add_meta_field = [&](const wxString& label, wxTextCtrl** ctrl, const std::string& initial_val, auto setter) {
        meta_grid->Add(new wxStaticText(m_left_panel, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        *ctrl = new wxTextCtrl(m_left_panel, wxID_ANY, initial_val);
        (*ctrl)->Bind(wxEVT_TEXT, [this, setter, ctrl](wxCommandEvent&) {
            (m_engine.*setter)((*ctrl)->GetValue().ToStdString());
        });
        meta_grid->Add(*ctrl, 1, wxEXPAND);
    };

    add_meta_field("Title:", &m_title_in, m_engine.project_title(), &Engine::set_project_title);
    add_meta_field("Artist:", &m_artist_in, m_engine.project_artist(), &Engine::set_project_artist);
    add_meta_field("Album:", &m_album_in, m_engine.project_album(), &Engine::set_project_album);
    add_meta_field("Year:", &m_year_in, m_engine.project_year(), &Engine::set_project_year);

    meta_sizer->Add(meta_grid, 1, wxEXPAND | wxALL, 5);
    left_sizer->Add(meta_sizer, 0, wxEXPAND | wxALL, 5);

    // Export Options Group
    wxStaticBoxSizer* export_group = new wxStaticBoxSizer(wxVERTICAL, m_left_panel, "Export Settings");
    
    wxBoxSizer* sr_row = new wxBoxSizer(wxHORIZONTAL);
    sr_row->Add(new wxStaticText(m_left_panel, wxID_ANY, "Sample Rate:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_sample_rate_ch = new wxChoice(m_left_panel, wxID_ANY);
    m_sample_rate_ch->Append("44100");
    m_sample_rate_ch->Append("48000");
    m_sample_rate_ch->Append("96000");
    m_sample_rate_ch->Select(1);
    sr_row->Add(m_sample_rate_ch, 1, wxEXPAND);
    export_group->Add(sr_row, 0, wxEXPAND | wxALL, 5);

    wxBoxSizer* check_row = new wxBoxSizer(wxHORIZONTAL);
    m_separate_tracks_btn = new wxCheckBox(m_left_panel, wxID_ANY, "Separate Tracks");
    m_export_click_btn = new wxCheckBox(m_left_panel, wxID_ANY, "Export Click Track");
    m_realtime_btn = new wxCheckBox(m_left_panel, wxID_ANY, "Realtime Render");
    check_row->Add(m_separate_tracks_btn, 1, wxEXPAND);
    check_row->Add(m_export_click_btn, 1, wxEXPAND);
    check_row->Add(m_realtime_btn, 1, wxEXPAND);
    export_group->Add(check_row, 0, wxEXPAND | wxALL, 5);

    m_export_progress_bar = new wxGauge(m_left_panel, wxID_ANY, 100);
    export_group->Add(m_export_progress_bar, 0, wxEXPAND | wxALL, 5);
    
    left_sizer->Add(export_group, 0, wxEXPAND | wxALL, 5);

    m_left_panel->SetSizer(left_sizer);
    main_sizer->Add(m_left_panel, 3, wxEXPAND | wxALL, 2);

    m_right_panel = new wxPanel(this, wxID_ANY);
    wxBoxSizer* right_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* track_btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_add_track_btn = new wxButton(m_right_panel, wxID_ANY, "Add Track", wxDefaultPosition, wxSize(-1, 30));
    m_add_track_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_BUTTON, wxSize(16, 16)));
    m_add_bus_btn = new wxButton(m_right_panel, wxID_ANY, "Add Bus", wxDefaultPosition, wxSize(-1, 30));
    m_add_bus_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_BUTTON, wxSize(16, 16)));
    m_add_track_btn->Bind(wxEVT_BUTTON, &ProjectPanel::on_add_track, this);
    m_add_bus_btn->Bind(wxEVT_BUTTON, &ProjectPanel::on_add_bus, this);
    track_btn_sizer->Add(m_add_track_btn, 1, wxEXPAND | wxALL, 2);
    track_btn_sizer->Add(m_add_bus_btn, 1, wxEXPAND | wxALL, 2);
    right_sizer->Add(track_btn_sizer, 0, wxEXPAND | wxALL, 5);

    m_track_scroll = new wxScrolledWindow(m_right_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_track_scroll->SetScrollRate(0, 5);
    m_track_container = new wxPanel(m_track_scroll, wxID_ANY);
    right_sizer->Add(m_track_scroll, 1, wxEXPAND | wxALL, 2);

    m_right_panel->SetSizer(right_sizer);
    main_sizer->Add(m_right_panel, 7, wxEXPAND | wxALL, 2);

    SetSizer(main_sizer);

    update_track_list();
}

void ProjectPanel::update_metadata() {
    if (m_title_in) m_title_in->ChangeValue(m_engine.project_title());
    if (m_artist_in) m_artist_in->ChangeValue(m_engine.project_artist());
    if (m_album_in) m_album_in->ChangeValue(m_engine.project_album());
    if (m_year_in) m_year_in->ChangeValue(m_engine.project_year());
}

void ProjectPanel::update_track_list() {
    m_track_container->DestroyChildren();

    wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(0, 14, 5, 5);
    grid_sizer->AddGrowableCol(1); // Track name should grow

    size_t num_tracks = m_engine.track_count();
    size_t num_insts = m_engine.instrument_count();
    size_t num_buses = m_engine.bus_count();

    // Header row
    auto make_hdr = [&](const wxString& text, const wxString& tip = wxEmptyString) {
        wxStaticText* lbl = new wxStaticText(m_track_container, wxID_ANY, text);
        wxFont f = lbl->GetFont();
        f.SetWeight(wxFONTWEIGHT_BOLD);
        lbl->SetFont(f);
        if (!tip.IsEmpty()) lbl->SetToolTip(tip);
        return lbl;
    };
    grid_sizer->Add(make_hdr(""),          0, wxALIGN_CENTER_VERTICAL | wxALL, 2); // index
    grid_sizer->Add(make_hdr("Name"),      0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    grid_sizer->Add(make_hdr("Type"),      0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    grid_sizer->Add(make_hdr("Linked"),    0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    grid_sizer->Add(make_hdr("Instrument"),0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    grid_sizer->Add(make_hdr("Take"),      0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    grid_sizer->Add(make_hdr("Drum",       "Audio-track drum role for later analysis, using GM drum names and typical frequency ranges"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    grid_sizer->Add(make_hdr("Action"),    0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    grid_sizer->Add(make_hdr("Output"),    0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    grid_sizer->Add(make_hdr("V\u00b1",   "Velocity humanization: \u00b1 spread in units (0 = off, SoundFont/SFZ only)"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    grid_sizer->Add(make_hdr("T ms",       "Timing humanization: max random onset delay in ms (0 = off, SoundFont/SFZ only)"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    grid_sizer->AddSpacer(1); // up
    grid_sizer->AddSpacer(1); // down
    grid_sizer->AddSpacer(1); // remove

    // Display tracks
    for (size_t i = 0; i < num_tracks; ++i) {
        auto& track_obj = m_engine.track(i);
        if (track_obj.instrument() &&
            !instrument_compatible_with_track(track_obj.kind(), track_obj.instrument()->type())) {
            track_obj.set_instrument(nullptr);
        }
        const bool is_tempo_track = m_engine.is_tempo_track(i);

        // Column 0: Index Label
        wxString idx_str;
        idx_str.Printf("TRK %zu:", i);
        grid_sizer->Add(new wxStaticText(m_track_container, wxID_ANY, idx_str), 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);

        // Column 1: Name Input
        wxTextCtrl* name_in = new wxTextCtrl(m_track_container, wxID_ANY, track_obj.name(), wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        name_in->Bind(wxEVT_TEXT, [this, i, name_in](wxCommandEvent&) {
            m_engine.track(i).set_name(name_in->GetValue().ToStdString());
        });
        grid_sizer->Add(name_in, 1, wxEXPAND | wxALL, 2);

        // Column 2: Track type
        wxChoice* type_ch = new wxChoice(m_track_container, wxID_ANY);
        type_ch->Append("Note");
        type_ch->Append("Audio");
        type_ch->Append("Pilot");
        type_ch->Select((int)track_obj.kind());
        type_ch->Enable(!is_tempo_track);
        type_ch->Bind(wxEVT_CHOICE, [this, i, type_ch](wxCommandEvent&) {
            TrackKind kind = (TrackKind)type_ch->GetSelection();
            Track& track = m_engine.track(i);
            track.set_kind(kind);
            if (kind != TrackKind::Audio) {
                track.set_drum_type(DrumType::None);
                track.set_record_armed(false);
            } else {
                m_engine.set_audio_track_armed(i, track.record_armed());
            }
            if (!track.instrument() || !instrument_compatible_with_track(kind, track.instrument()->type())) {
                track.set_instrument(nullptr);
            }
            const int linked = track.linked_track();
            if (linked < 0 || (size_t)linked >= m_engine.track_count() || linked == (int)i) {
                track.set_linked_track(-1);
            } else {
                const TrackKind linked_kind = m_engine.track(linked).kind();
                const bool compatible =
                    (kind == TrackKind::Note  && linked_kind == TrackKind::Audio) ||
                    (kind == TrackKind::Audio && linked_kind == TrackKind::Note);
                if (!compatible) {
                    track.set_linked_track(-1);
                }
            }
            m_engine.refresh_timeline_audio_tracks();
            m_engine.mark_dirty();
            CallAfter([this]() { update_track_list(); });
        });
        grid_sizer->Add(type_ch, 0, wxEXPAND | wxALL, 2);

        // Column 3: Attached/linked track
        wxChoice* link_ch = new wxChoice(m_track_container, wxID_ANY);
        std::vector<int> linked_tracks;
        link_ch->Append("None");
        linked_tracks.push_back(-1);
        for (size_t j = 0; j < num_tracks; ++j) {
            if (j == i) continue;
            const TrackKind other_kind = m_engine.track(j).kind();
            const bool compatible =
                (track_obj.kind() == TrackKind::Note  && other_kind == TrackKind::Audio) ||
                (track_obj.kind() == TrackKind::Audio && other_kind == TrackKind::Note);
            if (!compatible) continue;
            link_ch->Append(wxString::Format("TRK %zu: %s", j, m_engine.track(j).name()));
            linked_tracks.push_back((int)j);
        }
        int link_sel = 0;
        for (size_t j = 0; j < linked_tracks.size(); ++j) {
            if (linked_tracks[j] == track_obj.linked_track()) {
                link_sel = (int)j;
                break;
            }
        }
        link_ch->Select(link_sel);
        link_ch->Enable(!is_tempo_track && track_obj.kind() != TrackKind::Pilot);
        link_ch->Bind(wxEVT_CHOICE, [this, i, link_ch, linked_tracks](wxCommandEvent&) {
            const int sel = link_ch->GetSelection();
            if (sel != wxNOT_FOUND && (size_t)sel < linked_tracks.size()) {
                m_engine.track(i).set_linked_track(linked_tracks[sel]);
                m_engine.refresh_timeline_audio_tracks();
                m_engine.mark_dirty();
            }
        });
        grid_sizer->Add(link_ch, 0, wxEXPAND | wxALL, 2);

        // Column 4: Instrument Choice
        wxChoice* inst_ch = new wxChoice(m_track_container, wxID_ANY);
        std::vector<int> instrument_values;
        inst_ch->Append("None");
        instrument_values.push_back(-1);
        for (size_t j = 0; j < num_insts; ++j) {
            if (!instrument_compatible_with_track(track_obj.kind(), m_engine.instrument(j).type())) {
                continue;
            }
            inst_ch->Append(m_engine.instrument(j).name());
            instrument_values.push_back((int)j);
        }
        int inst_sel = 0;
        const int inst_idx = m_engine.get_instrument_index(track_obj.instrument());
        for (size_t j = 0; j < instrument_values.size(); ++j) {
            if (instrument_values[j] == inst_idx) {
                inst_sel = (int)j;
                break;
            }
        }
        inst_ch->Select(inst_sel);
        inst_ch->Enable(!is_tempo_track);
        inst_ch->Bind(wxEVT_CHOICE, [this, i, inst_ch, instrument_values](wxCommandEvent&) {
            int sel = inst_ch->GetSelection();
            if (sel == wxNOT_FOUND || (size_t)sel >= instrument_values.size() || instrument_values[sel] < 0) {
                m_engine.track(i).set_instrument(nullptr);
            } else {
                m_engine.track(i).set_instrument(&m_engine.instrument((size_t)instrument_values[sel]));
            }
            m_engine.refresh_timeline_audio_tracks();
            m_engine.mark_dirty();
            CallAfter([this]() { update_track_list(); });
        });
        grid_sizer->Add(inst_ch, 0, wxEXPAND | wxALL, 2);

        // Column 5: Audio take choice
        wxChoice* take_ch = new wxChoice(m_track_container, wxID_ANY);
        std::vector<int> take_values;
        take_ch->Append("01");
        take_values.push_back(1);
        bool take_enabled = false;
        if (track_obj.kind() == TrackKind::Audio || track_obj.kind() == TrackKind::Pilot) {
            Instrument* inst = track_obj.instrument();
            if (inst && inst->type() == InstrumentType::Sampler) {
                auto* sampler = static_cast<SampleInstrument*>(inst);
                if (sampler->sample_count() > 0) {
                    take_ch->Clear();
                    take_values.clear();
                    for (size_t j = 0; j < sampler->sample_count(); ++j) {
                        take_ch->Append(wxString::Format("%02zu: %s", j + 1, sampler->get_sample(j).name.c_str()));
                        take_values.push_back((int)j + 1);
                    }
                    take_enabled = true;
                }
            }
        }
        int take_sel = 0;
        for (size_t j = 0; j < take_values.size(); ++j) {
            if (take_values[j] == track_obj.audio_sample_index()) {
                take_sel = (int)j;
                break;
            }
        }
        take_ch->Select(take_sel);
        take_ch->Enable(!is_tempo_track && take_enabled);
        take_ch->Bind(wxEVT_CHOICE, [this, i, take_ch, take_values](wxCommandEvent&) {
            const int sel = take_ch->GetSelection();
            if (sel != wxNOT_FOUND && (size_t)sel < take_values.size()) {
                m_engine.track(i).set_audio_sample_index((uint8_t)take_values[sel]);
                m_engine.refresh_timeline_audio_tracks();
                m_engine.mark_dirty();
            }
        });
        grid_sizer->Add(take_ch, 0, wxEXPAND | wxALL, 2);

        // Column 6: Drum type
        wxChoice* drum_ch = new wxChoice(m_track_container, wxID_ANY);
        std::vector<DrumType> drum_values;
        for (const auto& info : drum_type_infos()) {
            drum_ch->Append(drum_type_label(info.type));
            drum_values.push_back(info.type);
        }
        int drum_sel = 0;
        for (size_t j = 0; j < drum_values.size(); ++j) {
            if (drum_values[j] == track_obj.drum_type()) {
                drum_sel = (int)j;
                break;
            }
        }
        drum_ch->Select(drum_sel);
        drum_ch->Enable(!is_tempo_track && track_obj.kind() == TrackKind::Audio);
        drum_ch->Bind(wxEVT_CHOICE, [this, i, drum_ch, drum_values](wxCommandEvent&) {
            const int sel = drum_ch->GetSelection();
            if (sel != wxNOT_FOUND && (size_t)sel < drum_values.size()) {
                m_engine.track(i).set_drum_type(drum_values[sel]);
                m_engine.mark_dirty();
            }
        });
        grid_sizer->Add(drum_ch, 0, wxEXPAND | wxALL, 2);

        // Column 7: Track action
        const bool is_note_track = !is_tempo_track && track_obj.kind() == TrackKind::Note;
        wxButton* analyze_btn = new wxButton(m_track_container, wxID_ANY,
                                             is_note_track ? "Quantize" : "Analyze",
                                             wxDefaultPosition, wxSize(80, -1));
        const int linked_track = track_obj.linked_track();
        const bool analyze_enabled =
            is_note_track ||
            (!is_tempo_track &&
             track_obj.kind() == TrackKind::Audio &&
             linked_track >= 0 &&
             (size_t)linked_track < m_engine.track_count() &&
             m_engine.track((size_t)linked_track).kind() == TrackKind::Note);
        analyze_btn->Enable(analyze_enabled);
        analyze_btn->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
            if (m_engine.track(i).kind() == TrackKind::Note) {
                wxArrayString choices;
                choices.Add("Tracker rows");
                choices.Add("Metronome clicks");
                wxSingleChoiceDialog dlg(this, "Snap note events to the selected grid.", "Quantize Note Track", choices);
                dlg.SetSelection(0);
                if (dlg.ShowModal() != wxID_OK) return;
                const bool align_to_click = dlg.GetSelection() == 1;
                if (m_engine.quantize_note_track(i, align_to_click)) {
                    if (m_main_window) m_main_window->update_all_uis();
                } else {
                    wxMessageBox("Quantization failed. Ensure the note track contains note events.",
                                 "Quantize", wxOK | wxICON_WARNING);
                }
            } else {
                if (m_engine.analyze_audio_track(i)) {
                    if (m_main_window) m_main_window->update_all_uis();
                } else {
                    wxMessageBox("Analysis failed. Ensure the audio track has a linked note track, a valid take, and detectable drum hits.",
                                 "Analysis", wxOK | wxICON_WARNING);
                }
            }
        });
        grid_sizer->Add(analyze_btn, 0, wxEXPAND | wxALL, 2);

        // Column 8: Output Bus Choice
        wxChoice* out_ch = new wxChoice(m_track_container, wxID_ANY);
        out_ch->Append("Master");
        for (size_t j = 1; j < num_buses; ++j) {
            wxString b_name = m_engine.bus(j).name();
            out_ch->Append(b_name);
        }
        
        int current_out = track_obj.output_bus();
        if (current_out == MixerBus::ROUTE_MASTER) {
            out_ch->Select(0);
        } else if (current_out >= 0 && (size_t)current_out < num_buses) {
            out_ch->Select(current_out);
        }
        
        out_ch->Bind(wxEVT_CHOICE, [this, i, out_ch](wxCommandEvent&) {
            int sel = out_ch->GetSelection();
            if (sel == 0) {
                m_engine.track(i).set_output_bus(MixerBus::ROUTE_MASTER);
            } else {
                m_engine.track(i).set_output_bus(sel);
            }
            m_engine.mark_dirty();
        });
        out_ch->Enable(!is_tempo_track);
        grid_sizer->Add(out_ch, 0, wxEXPAND | wxALL, 2);

        Instrument* inst_ptr = track_obj.instrument();

        // Columns 9, 10: Velocity & timing humanization - active only for note tracks with SoundFont/SFZ
        bool hum_enabled = !is_tempo_track && track_obj.kind() == TrackKind::Note && inst_ptr &&
                           (inst_ptr->type() == InstrumentType::SoundFont ||
                            inst_ptr->type() == InstrumentType::SFZ);

        wxSpinCtrl* vel_spin = new wxSpinCtrl(m_track_container, wxID_ANY, wxEmptyString,
                                               wxDefaultPosition, wxSize(55, -1),
                                               wxSP_ARROW_KEYS, 0, 64, track_obj.humanize_vel());
        vel_spin->SetToolTip("Velocity humanization: +/- spread in velocity units (0 = off)");
        vel_spin->Enable(hum_enabled);
        vel_spin->Bind(wxEVT_SPINCTRL, [this, i](wxSpinEvent& ev) {
            m_engine.track(i).set_humanize_vel((uint8_t)ev.GetValue());
            m_engine.mark_dirty();
        });
        grid_sizer->Add(vel_spin, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);

        wxSpinCtrl* tim_spin = new wxSpinCtrl(m_track_container, wxID_ANY, wxEmptyString,
                                               wxDefaultPosition, wxSize(55, -1),
                                               wxSP_ARROW_KEYS, 0, 100, track_obj.humanize_timing());
        tim_spin->SetToolTip("Timing humanization: max random onset delay in ms (0 = off)");
        tim_spin->Enable(hum_enabled);
        tim_spin->Bind(wxEVT_SPINCTRL, [this, i](wxSpinEvent& ev) {
            m_engine.track(i).set_humanize_timing((uint8_t)ev.GetValue());
            m_engine.mark_dirty();
        });
        grid_sizer->Add(tim_spin, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);

        // Columns 11, 12, 13: Control Buttons
        auto create_small_btn = [&](const wxArtID& art, const wxString& backup_label, auto func) {
            wxButton* btn = new wxButton(m_track_container, wxID_ANY, backup_label, wxDefaultPosition, wxSize(30, 25));
            btn->SetBitmap(wxArtProvider::GetBitmap(art, wxART_BUTTON, wxSize(14, 14)));
            btn->Bind(wxEVT_BUTTON, func);
            return btn;
        };

        wxButton* up_btn = create_small_btn(wxART_GO_UP, "^", [this, i](wxCommandEvent&) {
            if (i > 0) { m_engine.move_track(i, i - 1); CallAfter([this]() { update_track_list(); }); }
        });
        up_btn->Enable(!is_tempo_track);
        grid_sizer->Add(up_btn, 0, wxALL, 1);

        wxButton* down_btn = create_small_btn(wxART_GO_DOWN, "v", [this, i](wxCommandEvent&) {
            if (i < m_engine.track_count() - 1) { m_engine.move_track(i, i + 1); CallAfter([this]() { update_track_list(); }); }
        });
        down_btn->Enable(!is_tempo_track);
        grid_sizer->Add(down_btn, 0, wxALL, 1);

        wxButton* rem_btn = create_small_btn(wxART_DELETE, "X", [this, i](wxCommandEvent&) {
            m_engine.remove_track(i); CallAfter([this]() { update_track_list(); });
        });
        rem_btn->SetForegroundColour(*wxRED);
        rem_btn->Enable(!is_tempo_track);
        grid_sizer->Add(rem_btn, 0, wxALL, 1);
    }

    // Display buses (skip master bus at index 0)
    for (size_t i = 1; i < num_buses; ++i) {
        auto& bus_obj = m_engine.bus(i);

        // Column 0: Index Label
        wxString idx_str;
        idx_str.Printf("BUS %zu:", i);
        grid_sizer->Add(new wxStaticText(m_track_container, wxID_ANY, idx_str), 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);

        // Column 1: Name Input
        wxTextCtrl* name_in = new wxTextCtrl(m_track_container, wxID_ANY, bus_obj.name(), wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        name_in->Bind(wxEVT_TEXT, [this, i, name_in](wxCommandEvent&) {
            m_engine.bus(i).set_name(name_in->GetValue().ToStdString());
        });
        grid_sizer->Add(name_in, 1, wxEXPAND | wxALL, 2);

        // Columns 2, 3, 4, 5, 6, 7: Spacers (type/link/instrument/take/drum/analyze - N/A for buses)
        grid_sizer->AddSpacer(10);
        grid_sizer->AddSpacer(10);
        grid_sizer->AddSpacer(10);
        grid_sizer->AddSpacer(10);
        grid_sizer->AddSpacer(10);
        grid_sizer->AddSpacer(10);

        // Column 8: Output Choice (Hierarchical Buses + Hardware)
        wxChoice* out_ch = new wxChoice(m_track_container, wxID_ANY);
        std::vector<int> bus_outputs;
        
        // 1. Master Bus
        out_ch->Append("Master Bus");
        bus_outputs.push_back(MixerBus::ROUTE_MASTER);
        
        // 2. Hierarchical Buses (only those with index < current bus to avoid loops)
        for (size_t j = 1; j < i; ++j) {
            out_ch->Append("Bus: " + m_engine.bus(j).name());
            bus_outputs.push_back((int)j);
        }
        
        // 3. Hardware Stereo Pairs
        uint32_t num_outs = m_engine.m_num_outs;
        for (uint32_t j = 0; j < num_outs; j += 2) {
            if (j + 1 < num_outs) {
                out_ch->Append(wxString::Format("Hardware: Out %u/%u", j+1, j+2));
                bus_outputs.push_back(MixerBus::ROUTE_HW_STEREO_BASE - (int)(j/2));
            } else {
                out_ch->Append(wxString::Format("Hardware: Out %u", j+1));
                bus_outputs.push_back(MixerBus::ROUTE_HW_MONO_BASE - (int)j);
            }
        }
        
        // 4. Hardware Mono Outputs
        for (uint32_t j = 0; j < num_outs; ++j) {
            out_ch->Append(wxString::Format("Hardware: Mono Out %u", j+1));
            bus_outputs.push_back(MixerBus::ROUTE_HW_MONO_BASE - (int)j);
        }

        // Set current selection
        int current_out = bus_obj.output_bus();
        for (size_t k = 0; k < bus_outputs.size(); ++k) {
            if (bus_outputs[k] == current_out) {
                out_ch->Select((int)k);
                break;
            }
        }
        
        out_ch->Bind(wxEVT_CHOICE, [this, i, out_ch, bus_outputs](wxCommandEvent&) {
            int sel = out_ch->GetSelection();
            if (sel >= 0 && sel < (int)bus_outputs.size()) {
                m_engine.bus(i).set_output_bus(bus_outputs[sel]);
                m_engine.mark_dirty();
            }
        });
        grid_sizer->Add(out_ch, 0, wxEXPAND | wxALL, 2);
        // Columns 9, 10: Spacers (humanization - N/A for buses)
        grid_sizer->AddSpacer(10);
        grid_sizer->AddSpacer(10);

        // Columns 11, 12, 13: Control Buttons
        auto create_small_btn = [&](const wxArtID& art, const wxString& backup_label, auto func) {
            wxButton* btn = new wxButton(m_track_container, wxID_ANY, backup_label, wxDefaultPosition, wxSize(30, 25));
            btn->SetBitmap(wxArtProvider::GetBitmap(art, wxART_BUTTON, wxSize(14, 14)));
            btn->Bind(wxEVT_BUTTON, func);
            return btn;
        };

        grid_sizer->Add(create_small_btn(wxART_GO_UP, "^", [this, i](wxCommandEvent&) {
            if (i > 1) { m_engine.move_bus(i, i - 1); update_track_list(); }
        }), 0, wxALL, 1);

        grid_sizer->Add(create_small_btn(wxART_GO_DOWN, "v", [this, i](wxCommandEvent&) {
            if (i < m_engine.bus_count() - 1) { m_engine.move_bus(i, i + 1); update_track_list(); }
        }), 0, wxALL, 1);

        wxButton* rem_btn = create_small_btn(wxART_DELETE, "X", [this, i](wxCommandEvent&) {
            m_engine.remove_bus(i); update_track_list();
        });
        rem_btn->SetForegroundColour(*wxRED);
        grid_sizer->Add(rem_btn, 0, wxALL, 1);
    }

    m_track_container->SetSizer(grid_sizer);
    grid_sizer->SetSizeHints(m_track_container);
    m_track_container->Layout();
    
    // Update scrolled window virtual size
    wxSize best_size = grid_sizer->GetMinSize();
    m_track_scroll->SetVirtualSize(best_size);
}

void ProjectPanel::on_new(wxCommandEvent& event) {
    m_engine.new_project();
    update_track_list();
    if (m_main_window) m_main_window->update_all_uis();
    wxMessageBox("New project created", "Info", wxOK | wxICON_INFORMATION);
}

void ProjectPanel::on_load(wxCommandEvent& event) {
    wxFileDialog dlg(this, "Load Project", "", "",
                     "TPanar Projects (*.tp;*.TP;*.dg;*.DG)|*.tp;*.TP;*.dg;*.DG",
                     wxFD_OPEN);
    if (dlg.ShowModal() == wxID_OK) {
        m_engine.load_project(dlg.GetPath().ToStdString());
        if (m_main_window) m_main_window->update_all_uis();
    }
}

void ProjectPanel::on_import(wxCommandEvent& event) {
    wxFileDialog dlg(this, "Import Audio", "", "",
                     "Audio and SoundFont Files (*.wav;*.flac;*.ogg;*.mp3;*.aiff;*.sf2;*.sf3)|*.wav;*.flac;*.ogg;*.mp3;*.aiff;*.sf2;*.sf3;*.WAV;*.FLAC;*.OGG;*.MP3;*.AIFF;*.SF2;*.SF3",
                     wxFD_OPEN);
    if (dlg.ShowModal() == wxID_OK) {
        if (!m_engine.import_audio(dlg.GetPath().ToStdString())) {
            wxMessageBox("Import failed. The selected file could not be loaded as audio or SoundFont data.",
                         "Import Failed", wxOK | wxICON_ERROR);
            return;
        }
        if (m_main_window) m_main_window->update_all_uis();
    }
}

void ProjectPanel::on_save(wxCommandEvent& event) {
    wxFileDialog dlg(this, "Save Project", "", "", "TPanar Projects (*.tp)|*.tp",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_OK) {
        m_engine.save_project(ensure_extension(dlg.GetPath(), "tp").ToStdString());
    }
}

void ProjectPanel::on_export(wxCommandEvent& event) {
    wxFileDialog dlg(this, "Export to WAV", "", "", "WAV Files|*.wav", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;

    Engine::ExportOptions opts;
    wxString sr = m_sample_rate_ch->GetStringSelection();
    if (!sr.empty()) opts.sample_rate = wxAtoi(sr);
    opts.separate_tracks = m_separate_tracks_btn->GetValue();
    opts.export_click = m_export_click_btn->GetValue();
    opts.realtime = m_realtime_btn->GetValue();
    opts.lead_in_bars = m_engine.export_lead_in_bars();

    std::string path = dlg.GetPath().ToStdString();
    m_export_btn->Disable();
    m_export_progress_bar->SetValue(0);
    m_export_result.store(-1);

    std::thread([this, path, opts]() {
        bool ok = m_engine.render_to_wav(path, opts);
        m_export_result.store(ok ? 1 : 0);
    }).detach();

    if (!m_export_timer) {
        m_export_timer = new wxTimer(this, wxID_ANY);
        Bind(wxEVT_TIMER, &ProjectPanel::on_export_timer, this, m_export_timer->GetId());
    }
    m_export_timer->Start(200);
}

void ProjectPanel::on_export_timer(wxTimerEvent& /*event*/) {
    float prog = m_engine.m_export_progress.load();
    m_export_progress_bar->SetValue((int)(prog * 100.0f));

    int result = m_export_result.load();
    if (result >= 0) {
        m_export_timer->Stop();
        m_export_btn->Enable();
        m_export_progress_bar->SetValue(result > 0 ? 100 : 0);
        if (result > 0)
            wxMessageBox("Export completed successfully.\nAdditional stem and click files are written next to the main WAV when those options are enabled.",
                         "Success", wxOK | wxICON_INFORMATION);
        else
            wxMessageBox("Export failed", "Error", wxOK | wxICON_ERROR);
    }
}

void ProjectPanel::on_file_select(wxCommandEvent& event) {}

void ProjectPanel::on_dir_changed(wxFileDirPickerEvent& event) {
    update_file_list(event.GetPath());
}

void ProjectPanel::on_file_dclick(wxCommandEvent& event) {
    int sel = m_file_list->GetSelection();
    if (sel != wxNOT_FOUND) {
        wxString filename = m_file_list->GetString(sel);
        wxString dir = m_dir_picker->GetPath();
        wxString full_path = dir + "/" + filename;
        
        if (wxMessageBox("Load project " + filename + "? All unsaved changes will be lost.", 
                        "Confirm", wxYES_NO | wxICON_QUESTION) == wxYES) {
            m_engine.load_project(full_path.ToStdString());
            if (m_main_window) m_main_window->update_all_uis();
        }
    }
}

void ProjectPanel::update_file_list(const wxString& dir) {
    m_file_list->Clear();
    wxDir directory(dir);
    if (directory.IsOpened()) {
        wxString filename;
        const wxString patterns[] = {"*.tp", "*.TP", "*.dg", "*.DG"};
        for (const auto& pattern : patterns) {
            bool cont = directory.GetFirst(&filename, pattern, wxDIR_FILES);
            while (cont) {
                if (m_file_list->FindString(filename) == wxNOT_FOUND) {
                    m_file_list->Append(filename);
                }
                cont = directory.GetNext(&filename);
            }
        }
    }
}

void ProjectPanel::on_add_track(wxCommandEvent& event) {
    const size_t pair_index = m_engine.track_count() > 2 ? (m_engine.track_count() - 2) / 2 + 1 : 1;

    m_engine.add_instrument();
    const size_t audio_inst_index = m_engine.instrument_count() - 1;
    m_engine.set_instrument_type(audio_inst_index, InstrumentType::Sampler);
    m_engine.instrument(audio_inst_index).set_name(wxString::Format("Track %zu Sampler", pair_index).ToStdString());

    m_engine.add_track();
    const size_t audio_track_index = m_engine.track_count() - 1;
    m_engine.track(audio_track_index).set_name(wxString::Format("Audio %zu", pair_index).ToStdString());
    m_engine.track(audio_track_index).set_kind(TrackKind::Audio);
    m_engine.track(audio_track_index).set_instrument(&m_engine.instrument(audio_inst_index));
    m_engine.track(audio_track_index).set_audio_sample_index(1);
    m_engine.track(audio_track_index).set_drum_type(DrumType::None);
    m_engine.track(audio_track_index).set_audio_input((int)(pair_index - 1), -1);

    m_engine.add_instrument();
    const size_t note_inst_index = m_engine.instrument_count() - 1;
    m_engine.set_instrument_type(note_inst_index, InstrumentType::SoundFont);
    m_engine.instrument(note_inst_index).set_name(wxString::Format("Track %zu SF2", pair_index).ToStdString());

    m_engine.add_track();
    const size_t note_track_index = m_engine.track_count() - 1;
    m_engine.track(note_track_index).set_name(wxString::Format("Note %zu", pair_index).ToStdString());
    m_engine.track(note_track_index).set_kind(TrackKind::Note);
    m_engine.track(note_track_index).set_instrument(&m_engine.instrument(note_inst_index));

    m_engine.track(audio_track_index).set_linked_track((int)note_track_index);
    m_engine.track(note_track_index).set_linked_track((int)audio_track_index);

    m_engine.refresh_timeline_audio_tracks();
    m_engine.set_audio_track_armed(audio_track_index, true);
    m_engine.set_record_track(note_track_index);
    m_engine.mark_dirty();
    update_track_list();
    if (m_main_window) m_main_window->update_all_uis();
}

void ProjectPanel::on_remove_track(wxCommandEvent& event) {}

void ProjectPanel::on_track_name(wxCommandEvent& event) {}

void ProjectPanel::on_track_inst(wxCommandEvent& event) {}

void ProjectPanel::on_track_output(wxCommandEvent& event) {}

void ProjectPanel::on_move_track_up(wxCommandEvent& event) {}

void ProjectPanel::on_move_track_down(wxCommandEvent& event) {}

void ProjectPanel::on_add_bus(wxCommandEvent& event) {
    m_engine.add_bus();
    update_track_list();
    if (m_main_window) m_main_window->update_all_uis();
}

void ProjectPanel::on_remove_bus(wxCommandEvent& event) {}

void ProjectPanel::on_bus_name(wxCommandEvent& event) {}

void ProjectPanel::on_bus_output(wxCommandEvent& event) {}

} // namespace tpanar_ns
