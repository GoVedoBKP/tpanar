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

#include "wx_instrument_panel.h"
#include "wx_waveform_view.h"
#include "wx_main_window.h"
#include "wx_detached_frame.h"
#include "wx_vu_meter.h"
#include "../core/engine.h"
#include "../instrument/sample_instrument.h"
#include "../instrument/soundfont_instrument.h"
#include "../instrument/sfz_instrument.h"
#include "../instrument/dssi_instrument.h"
#include "../instrument/midi_instrument.h"
#include "../instrument/voice_instrument.h"
#include "../instrument/voice_synthesis_worker.h"
#include "../audio/resampler.h"
#include "../audio/timestretch.h"
#include "../io/audio_file.h"
#include "../io/project_archive.h"

#include <wx/sizer.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/textdlg.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/numdlg.h>
#include <wx/artprov.h>
#include <wx/progdlg.h>
#include <dlfcn.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr const char* kInstrumentArchiveOpenFilter = "TPanar Instruments (*.tpi;*.TPI;*.dgi;*.DGI)|*.tpi;*.TPI;*.dgi;*.DGI";
constexpr const char* kInstrumentArchiveSaveFilter = "TPanar Instruments (*.tpi)|*.tpi";
constexpr const char* kInstrumentManifestFile = "instrument.json";

struct LoadedSampleInstrument {
    std::string name;
    std::vector<tpanar_ns::SampleEntry> samples;
    size_t selected_sample = 0;
};

fs::path make_instrument_temp_dir(const char* prefix) {
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    fs::path dir = fs::temp_directory_path() / (std::string(prefix) + std::to_string(now));
    if (fs::exists(dir)) {
        fs::remove_all(dir);
    }
    fs::create_directories(dir);
    return dir;
}

bool save_sample_instrument_archive(const tpanar_ns::SampleInstrument& sampler,
                                    const std::string& archive_path) {
    fs::path tmp_dir;
    try {
        tmp_dir = make_instrument_temp_dir("tpanar_inst_save_");
        fs::path samples_dir = tmp_dir / "samples";
        fs::create_directories(samples_dir);

        json instrument_json;
        instrument_json["format_version"] = 1;
        instrument_json["type"] = "sampler";
        instrument_json["name"] = sampler.name();
        instrument_json["selected_sample"] = sampler.sample_count() == 0
            ? 0
            : std::min(sampler.selected_sample(), sampler.sample_count() - 1);
        instrument_json["samples"] = json::array();

        for (size_t i = 0; i < sampler.sample_count(); ++i) {
            const auto& sample = sampler.get_sample(i);
            json sample_json;
            sample_json["name"] = sample.name;

            if (sample.data && (!sample.data->left.empty() || !sample.data->right.empty())) {
                const std::string filename = "sample_" + std::to_string(i) + ".wav";
                fs::path wav_path = samples_dir / filename;
                if (!tpanar_ns::AudioFile::save_wav(wav_path.string(),
                                                      sample.data->left,
                                                      sample.data->right,
                                                      (uint32_t)sample.data->sample_rate)) {
                    fs::remove_all(tmp_dir);
                    return false;
                }
                sample_json["file"] = (fs::path("samples") / filename).string();
            }

            instrument_json["samples"].push_back(sample_json);
        }

        std::ofstream manifest(tmp_dir / kInstrumentManifestFile);
        if (!manifest) {
            fs::remove_all(tmp_dir);
            return false;
        }
        manifest << instrument_json.dump(2);
        manifest.close();

        const bool ok = tpanar_ns::ProjectArchive::save(archive_path, tmp_dir.string());
        fs::remove_all(tmp_dir);
        return ok;
    } catch (const std::exception&) {
        if (!tmp_dir.empty() && fs::exists(tmp_dir)) {
            fs::remove_all(tmp_dir);
        }
        return false;
    }
}

bool load_sample_instrument_archive(const std::string& archive_path,
                                    LoadedSampleInstrument& loaded) {
    fs::path tmp_dir;
    try {
        tmp_dir = make_instrument_temp_dir("tpanar_inst_load_");
        if (!tpanar_ns::ProjectArchive::extract(archive_path, tmp_dir.string())) {
            fs::remove_all(tmp_dir);
            return false;
        }

        std::ifstream manifest(tmp_dir / kInstrumentManifestFile);
        if (!manifest) {
            fs::remove_all(tmp_dir);
            return false;
        }

        json instrument_json;
        manifest >> instrument_json;

        if (instrument_json.value("type", std::string()) != "sampler") {
            fs::remove_all(tmp_dir);
            return false;
        }

        loaded = {};
        loaded.name = instrument_json.value("name", std::string("Loaded Sampler"));

        if (instrument_json.contains("samples")) {
            for (const auto& sample_json : instrument_json["samples"]) {
                tpanar_ns::SampleEntry entry;
                entry.name = sample_json.value("name", std::string("Sample"));

                const std::string sample_file = sample_json.value("file", std::string());
                if (!sample_file.empty()) {
                    auto data = std::make_shared<tpanar_ns::SampleData>();
                    uint32_t rate = 0;
                    fs::path sample_path = tmp_dir / sample_file;
                    if (!tpanar_ns::AudioFile::load_audio(sample_path.string(),
                                                            data->left,
                                                            data->right,
                                                            rate)) {
                        fs::remove_all(tmp_dir);
                        return false;
                    }
                    data->sample_rate = (int)rate;
                    entry.data = data;
                } else {
                    entry.data = std::make_shared<tpanar_ns::SampleData>();
                }

                loaded.samples.push_back(std::move(entry));
            }
        }

        if (!loaded.samples.empty()) {
            loaded.selected_sample = std::min(
                instrument_json.value("selected_sample", size_t{0}),
                loaded.samples.size() - 1
            );
        }

        fs::remove_all(tmp_dir);
        return true;
    } catch (const std::exception&) {
        if (!tmp_dir.empty() && fs::exists(tmp_dir)) {
            fs::remove_all(tmp_dir);
        }
        return false;
    }
}

} // namespace

namespace tpanar_ns {

namespace {
constexpr int kInputMeterIntervalMs = 16;
}

InstrumentPanel::InstrumentPanel(wxWindow* parent, Engine& engine)
    : wxPanel(parent, wxID_ANY), m_engine(engine)
{
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    m_main_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
    m_main_splitter->SetMinimumPaneSize(200);

    // --- Left Side: Instrument List and Browser ---
    m_left_panel = new wxPanel(m_main_splitter, wxID_ANY);
    wxBoxSizer* left_sizer = new wxBoxSizer(wxVERTICAL);

    m_left_splitter = new wxSplitterWindow(m_left_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3D);
    m_left_splitter->SetMinimumPaneSize(100);

    // 1. Instrument List Pane
    m_inst_list_pane = new wxPanel(m_left_splitter, wxID_ANY);
    wxBoxSizer* inst_list_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* top_btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_detach_btn = new wxButton(m_inst_list_pane, wxID_ANY, "", wxDefaultPosition, wxSize(28, 28));
    m_detach_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FULL_SCREEN, wxART_BUTTON, wxSize(16, 16)));
    m_detach_btn->SetToolTip("Detach");
    m_detach_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_detach, this);
    top_btn_sizer->Add(new wxStaticText(m_inst_list_pane, wxID_ANY, "Instruments"), 1, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    top_btn_sizer->Add(m_detach_btn, 0, wxALL, 2);
    inst_list_sizer->Add(top_btn_sizer, 0, wxEXPAND | wxALL, 2);

    wxBoxSizer* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_new_btn = new wxButton(m_inst_list_pane, wxID_ANY, "New", wxDefaultPosition, wxSize(-1, 25));
    m_new_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_NEW, wxART_BUTTON, wxSize(16, 16)));
    m_new_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_new, this);
    
    m_load_btn = new wxButton(m_inst_list_pane, wxID_ANY, "Load", wxDefaultPosition, wxSize(-1, 25));
    m_load_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_BUTTON, wxSize(16, 16)));
    m_load_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_load, this);
    
    m_save_btn = new wxButton(m_inst_list_pane, wxID_ANY, "Save", wxDefaultPosition, wxSize(-1, 25));
    m_save_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_BUTTON, wxSize(16, 16)));
    m_save_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_save, this);
    
    m_delete_btn = new wxButton(m_inst_list_pane, wxID_ANY, "Del", wxDefaultPosition, wxSize(-1, 25));
    m_delete_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_BUTTON, wxSize(16, 16)));
    m_delete_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_delete, this);
    
    btn_sizer->Add(m_new_btn, 1, wxALL, 1);
    btn_sizer->Add(m_load_btn, 1, wxALL, 1);
    btn_sizer->Add(m_save_btn, 1, wxALL, 1);
    btn_sizer->Add(m_delete_btn, 1, wxALL, 1);
    inst_list_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, 2);

    m_inst_scroll = new wxScrolledWindow(m_inst_list_pane, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_inst_scroll->SetScrollRate(0, 20);
    m_inst_scroll->SetSizer(new wxBoxSizer(wxVERTICAL));
    inst_list_sizer->Add(m_inst_scroll, 1, wxEXPAND | wxALL, 2);
    m_inst_list_pane->SetSizer(inst_list_sizer);

    // 2. File Browser Pane
    m_file_browser_pane = new wxPanel(m_left_splitter, wxID_ANY);
    wxBoxSizer* file_browser_sizer = new wxBoxSizer(wxVERTICAL);
    file_browser_sizer->Add(new wxStaticText(m_file_browser_pane, wxID_ANY, "File Browser"), 0, wxALL, 2);
    m_file_browser = new wxFileCtrl(m_file_browser_pane, wxID_ANY, wxEmptyString, wxEmptyString, wxEmptyString, wxFC_DEFAULT_STYLE);
    file_browser_sizer->Add(m_file_browser, 1, wxEXPAND | wxALL, 2);
    m_file_browser_pane->SetSizer(file_browser_sizer);

    m_left_splitter->SplitHorizontally(m_inst_list_pane, m_file_browser_pane, 400);
    left_sizer->Add(m_left_splitter, 1, wxEXPAND);
    m_left_panel->SetSizer(left_sizer);

    // --- Right Panel: Editors ---
    m_right_panel = new wxPanel(m_main_splitter, wxID_ANY);
    wxBoxSizer* right_sizer = new wxBoxSizer(wxVERTICAL);

    m_main_splitter->SplitVertically(m_left_panel, m_right_panel, 320);
    main_sizer->Add(m_main_splitter, 1, wxEXPAND);
    this->SetSizer(main_sizer);

    // 1. Sampler Editor
    m_sampler_editor = new wxPanel(m_right_panel, wxID_ANY);
    wxBoxSizer* sampler_sizer = new wxBoxSizer(wxVERTICAL);

    // Sample List / Recording Controls
    wxBoxSizer* top_sampler_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Sample List
    m_sample_list_grp = new wxPanel(m_sampler_editor, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);
    wxBoxSizer* sample_list_sizer = new wxBoxSizer(wxVERTICAL);
    m_add_sample_btn = new wxButton(m_sample_list_grp, wxID_ANY, "Add Sample");
    m_add_sample_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_BUTTON, wxSize(16, 16)));
    m_add_sample_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_add_sample, this);
    sample_list_sizer->Add(m_add_sample_btn, 0, wxEXPAND | wxALL, 2);
    
    m_sample_scroll = new wxScrolledWindow(m_sample_list_grp, wxID_ANY, wxDefaultPosition, wxSize(-1, 200), wxVSCROLL);
    m_sample_scroll->SetScrollRate(0, 20);
    m_sample_scroll->SetSizer(new wxBoxSizer(wxVERTICAL));
    sample_list_sizer->Add(m_sample_scroll, 1, wxEXPAND | wxALL, 0);
    m_sample_list_grp->SetSizer(sample_list_sizer);
    top_sampler_sizer->Add(m_sample_list_grp, 1, wxEXPAND | wxALL, 2);

    // Recording / Playback
    wxPanel* rec_panel = new wxPanel(m_sampler_editor, wxID_ANY);
    wxBoxSizer* rec_sizer = new wxBoxSizer(wxVERTICAL);
    
    // Row 1: Playback and Undo/Redo
    wxBoxSizer* playback_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_sample_play_btn = new wxButton(rec_panel, wxID_ANY, "Play", wxDefaultPosition, wxSize(-1, 25));
    m_sample_play_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON, wxSize(16, 16)));
    m_sample_play_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_sample_play, this);
    
    m_sample_stop_btn = new wxButton(rec_panel, wxID_ANY, "Stop", wxDefaultPosition, wxSize(-1, 25));
    m_sample_stop_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_STOP, wxART_BUTTON, wxSize(16, 16)));
    m_sample_stop_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_sample_stop, this);
    
    m_rec_btn = new wxButton(rec_panel, wxID_ANY, "Record", wxDefaultPosition, wxSize(-1, 25));
    m_rec_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON, wxSize(16, 16)));
    m_rec_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_record_sample, this);
    
    m_undo_btn = new wxButton(rec_panel, wxID_ANY, "Undo", wxDefaultPosition, wxSize(-1, 25));
    m_undo_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_UNDO, wxART_BUTTON, wxSize(16, 16)));
    m_undo_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_undo, this);
    
    m_redo_btn = new wxButton(rec_panel, wxID_ANY, "Redo", wxDefaultPosition, wxSize(-1, 25));
    m_redo_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_REDO, wxART_BUTTON, wxSize(16, 16)));
    m_redo_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_redo, this);
    
    playback_sizer->Add(m_sample_play_btn, 0, wxRIGHT, 2);
    playback_sizer->Add(m_sample_stop_btn, 0, wxRIGHT, 2);
    playback_sizer->Add(m_rec_btn, 0, wxRIGHT, 10);
    m_preview_fx_check = new wxCheckBox(rec_panel, wxID_ANY, "Use FX Chain");
    m_preview_fx_check->SetToolTip("Play preview through the track's effect chain (only when instrument is assigned to a track)");
    m_preview_fx_check->Enable(false);
    playback_sizer->Add(m_preview_fx_check, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 8);
    m_preview_loop_check = new wxCheckBox(rec_panel, wxID_ANY, "Loop");
    m_preview_loop_check->SetToolTip("Loop the sample (or selection) during preview");
    playback_sizer->Add(m_preview_loop_check, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 8);
    playback_sizer->Add(m_undo_btn, 0, wxRIGHT, 2);
    playback_sizer->Add(m_redo_btn, 0, wxRIGHT, 2);
    rec_sizer->Add(playback_sizer, 0, wxTOP | wxBOTTOM, 4);

    // Row 2: Input Selection and Mono
    wxBoxSizer* input_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_rec_input_ch = new wxChoice(rec_panel, wxID_ANY);
    m_mono_btn = new wxCheckBox(rec_panel, wxID_ANY, "Mono Input");
    m_mono_btn->Bind(wxEVT_CHECKBOX, &InstrumentPanel::on_mono_toggle, this);
    
    input_sizer->Add(new wxStaticText(rec_panel, wxID_ANY, "Input:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    input_sizer->Add(m_rec_input_ch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    input_sizer->Add(m_mono_btn, 0, wxALIGN_CENTER_VERTICAL);
    rec_sizer->Add(input_sizer, 0, wxTOP | wxBOTTOM, 4);

    // Row 3: Recording Mode
    wxBoxSizer* mode_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_rec_mode_ch = new wxChoice(rec_panel, wxID_ANY);
    m_rec_mode_ch->Append("Free"); m_rec_mode_ch->Append("Synced"); m_rec_mode_ch->SetSelection(0);
    
    mode_sizer->Add(new wxStaticText(rec_panel, wxID_ANY, "Rec Mode:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    mode_sizer->Add(m_rec_mode_ch, 0, wxALIGN_CENTER_VERTICAL);
    rec_sizer->Add(mode_sizer, 0, wxTOP | wxBOTTOM, 4);

    // Row 4: Recording status indicator (waiting / active / loop counter)
    m_rec_status_lbl = new wxStaticText(rec_panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_rec_status_lbl->SetMinSize(wxSize(-1, 18));
    rec_sizer->Add(m_rec_status_lbl, 0, wxEXPAND | wxTOP | wxBOTTOM, 2);

    // Input VU meters — L and R for the selected input channel pair
    auto* vu_label_l = new wxStaticText(rec_panel, wxID_ANY, "L");
    auto* vu_label_r = new wxStaticText(rec_panel, wxID_ANY, "R");
    m_input_vu_l = new VUMeter(rec_panel, wxID_ANY, m_engine, true);
    m_input_vu_r = new VUMeter(rec_panel, wxID_ANY, m_engine, true);
    m_input_vu_l->SetMinSize(wxSize(80, 14));
    m_input_vu_r->SetMinSize(wxSize(80, 14));

    wxFlexGridSizer* vu_grid = new wxFlexGridSizer(2, 2, 3, 4);
    vu_grid->AddGrowableCol(1, 1);
    vu_grid->Add(vu_label_l, 0, wxALIGN_CENTER_VERTICAL);
    vu_grid->Add(m_input_vu_l, 1, wxEXPAND);
    vu_grid->Add(vu_label_r, 0, wxALIGN_CENTER_VERTICAL);
    vu_grid->Add(m_input_vu_r, 1, wxEXPAND);
    rec_sizer->Add(vu_grid, 0, wxEXPAND | wxTOP, 6);

    rec_panel->SetSizer(rec_sizer);
    top_sampler_sizer->Add(rec_panel, 1, wxEXPAND | wxLEFT, 10);
    sampler_sizer->Add(top_sampler_sizer, 0, wxEXPAND | wxALL, 2);

    // Keep input meters responsive enough to feel continuous while recording.
    m_vu_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (m_input_vu_l && m_input_vu_r) {
            int sel = m_rec_input_ch ? m_rec_input_ch->GetSelection() : 0;
            if (sel < 0) sel = 0;
            bool mono = m_mono_btn && m_mono_btn->GetValue();
            uint32_t ch = (uint32_t)sel;
            if (!mono) ch *= 2;  // dropdown index → physical channel
            float lvl_l = m_engine.input_level(ch);
            float lvl_r = mono ? lvl_l : m_engine.input_level(ch + 1);
            m_input_vu_l->level(lvl_l);
            m_input_vu_r->level(lvl_r);
        }
        // Live waveform update during recording.
        if (m_waveform_view && m_engine.is_recording_sample()) {
            auto data = m_engine.recording_sample_data();
            if (data) {
                m_waveform_view->set_sample(data);
            }
        }
        // Update waveform playback cursor
        if (m_waveform_view) {
            m_waveform_view->set_playback_pos(m_engine.preview_playback_pos());
        }
        // Recording status label
        if (m_rec_status_lbl) {
            if (!m_engine.is_recording_sample()) {
                if (!m_rec_status_lbl->GetLabel().empty()) {
                    m_rec_status_lbl->SetLabel("");
                    m_rec_status_lbl->SetForegroundColour(wxNullColour);
                }
            } else {
                Engine::SampleRecordMode mode = m_engine.m_recording_sample_mode.load();
                if (mode == Engine::SampleRecordMode::Free) {
                    auto data = m_engine.recording_sample_data();
                    size_t frames = data ? data->left.size() : 0;
                    float secs = frames / (float)std::max(1u, m_engine.sample_rate());
                    m_rec_status_lbl->SetLabel(wxString::Format("\u25cf REC  %.1fs", secs));
                    m_rec_status_lbl->SetForegroundColour(*wxRED);
                } else {
                    size_t row   = m_engine.m_recording_synced_row.load();
                    size_t total = m_engine.pattern().row_count();
                    if (m_engine.m_recording_synced_active.load()) {
                        size_t loops = m_engine.m_recording_loop_count.load();
                        m_rec_status_lbl->SetLabel(wxString::Format(
                            "\u25cf REC  row %zu/%zu  loop %zu",
                            row + 1, total, loops + 1));
                        m_rec_status_lbl->SetForegroundColour(*wxRED);
                    } else {
                        size_t rows_to_go = (row == 0) ? 0 : (total - row);
                        m_rec_status_lbl->SetLabel(wxString::Format(
                            "Waiting...  row %zu/%zu  (%zu to go)",
                            row + 1, total, rows_to_go));
                        m_rec_status_lbl->SetForegroundColour(wxColour(180, 140, 0));
                    }
                }
            }
        }
    }, m_vu_timer->GetId());
    m_vu_timer->Start(kInputMeterIntervalMs);

    // Processing Controls - Using a FlexGridSizer for better alignment
    wxFlexGridSizer* proc_sizer = new wxFlexGridSizer(4, 2, 5, 5);
    proc_sizer->AddGrowableCol(1, 1);

    // Row 1: Volume
    proc_sizer->Add(new wxStaticText(m_sampler_editor, wxID_ANY, "Volume:"), 0, wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* vol_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_norm_btn = new wxButton(m_sampler_editor, wxID_ANY, "Normalize", wxDefaultPosition, wxSize(-1, 25));
    m_norm_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_BUTTON, wxSize(16, 16)));
    m_norm_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_normalize, this);
    m_vol_btn = new wxButton(m_sampler_editor, wxID_ANY, "Gain", wxDefaultPosition, wxSize(-1, 25));
    m_vol_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_UP, wxART_BUTTON, wxSize(16, 16)));
    m_vol_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_adjust_vol, this);
    m_vol_input = new wxSpinCtrlDouble(m_sampler_editor, wxID_ANY);
    m_vol_input->SetRange(0.0, 10.0); m_vol_input->SetIncrement(0.1); m_vol_input->SetValue(1.0);
    vol_sizer->Add(m_norm_btn, 0, wxRIGHT, 2);
    vol_sizer->Add(m_vol_btn, 0, wxRIGHT, 2);
    vol_sizer->Add(m_vol_input, 0);
    proc_sizer->Add(vol_sizer, 0, wxEXPAND);

    // Row 2: Fades
    proc_sizer->Add(new wxStaticText(m_sampler_editor, wxID_ANY, "Fades:"), 0, wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* fade_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_fade_in_lin_btn = new wxButton(m_sampler_editor, wxID_ANY, "In Lin", wxDefaultPosition, wxSize(-1, 25));
    m_fade_in_lin_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON, wxSize(16, 16)));
    m_fade_in_lin_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_fade_in_lin, this);
    m_fade_in_log_btn = new wxButton(m_sampler_editor, wxID_ANY, "In Log", wxDefaultPosition, wxSize(-1, 25));
    m_fade_in_log_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON, wxSize(16, 16)));
    m_fade_in_log_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_fade_in_log, this);
    m_fade_out_lin_btn = new wxButton(m_sampler_editor, wxID_ANY, "Out Lin", wxDefaultPosition, wxSize(-1, 25));
    m_fade_out_lin_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_BACK, wxART_BUTTON, wxSize(16, 16)));
    m_fade_out_lin_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_fade_out_lin, this);
    m_fade_out_log_btn = new wxButton(m_sampler_editor, wxID_ANY, "Out Log", wxDefaultPosition, wxSize(-1, 25));
    m_fade_out_log_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_BACK, wxART_BUTTON, wxSize(16, 16)));
    m_fade_out_log_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_fade_out_log, this);
    fade_sizer->Add(m_fade_in_lin_btn, 0, wxRIGHT, 2);
    fade_sizer->Add(m_fade_in_log_btn, 0, wxRIGHT, 2);
    fade_sizer->Add(m_fade_out_lin_btn, 0, wxRIGHT, 2);
    fade_sizer->Add(m_fade_out_log_btn, 0);
    proc_sizer->Add(fade_sizer, 0, wxEXPAND);

    // Row 3: Transform
    proc_sizer->Add(new wxStaticText(m_sampler_editor, wxID_ANY, "Transform:"), 0, wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* transform_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_crop_btn = new wxButton(m_sampler_editor, wxID_ANY, "Crop", wxDefaultPosition, wxSize(-1, 25));
    m_crop_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_CUT, wxART_BUTTON, wxSize(16, 16)));
    m_crop_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_crop, this);
    m_resample_btn = new wxButton(m_sampler_editor, wxID_ANY, "Resample", wxDefaultPosition, wxSize(-1, 25));
    m_resample_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_EXECUTABLE_FILE, wxART_BUTTON, wxSize(16, 16)));
    m_resample_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_resample, this);
    m_stretch_btn = new wxButton(m_sampler_editor, wxID_ANY, "Stretch", wxDefaultPosition, wxSize(-1, 25));
    m_stretch_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_REPORT_VIEW, wxART_BUTTON, wxSize(16, 16)));
    m_stretch_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_time_stretch, this);
    transform_sizer->Add(m_crop_btn, 0, wxRIGHT, 2);
    transform_sizer->Add(m_resample_btn, 0, wxRIGHT, 2);
    transform_sizer->Add(m_stretch_btn, 0);
    proc_sizer->Add(transform_sizer, 0, wxEXPAND);

    // Row 4: Editing
    proc_sizer->Add(new wxStaticText(m_sampler_editor, wxID_ANY, "Edit:"), 0, wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* edit_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_silence_btn = new wxButton(m_sampler_editor, wxID_ANY, "Silence", wxDefaultPosition, wxSize(-1, 25));
    m_silence_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_CROSS_MARK, wxART_BUTTON, wxSize(16, 16)));
    m_silence_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_silence, this);
    m_ins_sil_btn = new wxButton(m_sampler_editor, wxID_ANY, "Insert Sil", wxDefaultPosition, wxSize(-1, 25));
    m_ins_sil_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_BUTTON, wxSize(16, 16)));
    m_ins_sil_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_insert_silence, this);
    m_cut_btn = new wxButton(m_sampler_editor, wxID_ANY, "Cut", wxDefaultPosition, wxSize(-1, 25));
    m_cut_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_CUT, wxART_BUTTON, wxSize(16, 16)));
    m_cut_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ cut(); });
    m_copy_btn = new wxButton(m_sampler_editor, wxID_ANY, "Copy", wxDefaultPosition, wxSize(-1, 25));
    m_copy_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_COPY, wxART_BUTTON, wxSize(16, 16)));
    m_copy_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ copy(); });
    m_paste_btn = new wxButton(m_sampler_editor, wxID_ANY, "Paste", wxDefaultPosition, wxSize(-1, 25));
    m_paste_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_PASTE, wxART_BUTTON, wxSize(16, 16)));
    m_paste_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){ paste(); });
    edit_sizer->Add(m_silence_btn, 0, wxRIGHT, 2);
    edit_sizer->Add(m_ins_sil_btn, 0, wxRIGHT, 2);
    edit_sizer->Add(m_cut_btn, 0, wxRIGHT, 2);
    edit_sizer->Add(m_copy_btn, 0, wxRIGHT, 2);
    edit_sizer->Add(m_paste_btn, 0);
    proc_sizer->Add(edit_sizer, 0, wxEXPAND);

    sampler_sizer->Add(proc_sizer, 0, wxEXPAND | wxALL, 5);

    // Sample name label shown above the waveform when a sample is selected.
    m_sample_name_label = new wxStaticText(m_sampler_editor, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    sampler_sizer->Add(m_sample_name_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);

    // Waveform View
    m_waveform_view = new WaveformView(m_sampler_editor, wxID_ANY, m_engine);
    m_waveform_view->SetMinSize(wxSize(-1, 350));
    sampler_sizer->Add(m_waveform_view, 1, wxEXPAND | wxALL, 2);

    // Waveform Controls
    wxBoxSizer* wf_ctrl_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_zoom_in_btn = new wxButton(m_sampler_editor, wxID_ANY, "Zoom In", wxDefaultPosition, wxSize(-1, 25));
    m_zoom_in_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_PLUS, wxART_BUTTON, wxSize(16, 16)));
    m_zoom_in_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_zoom_in, this);
    
    m_zoom_out_btn = new wxButton(m_sampler_editor, wxID_ANY, "Zoom Out", wxDefaultPosition, wxSize(-1, 25));
    m_zoom_out_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_MINUS, wxART_BUTTON, wxSize(16, 16)));
    m_zoom_out_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_zoom_out, this);
    
    m_view_all_btn = new wxButton(m_sampler_editor, wxID_ANY, "View All", wxDefaultPosition, wxSize(-1, 25));
    m_view_all_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_HOME, wxART_BUTTON, wxSize(16, 16)));
    m_view_all_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_view_all, this);
    
    m_view_sel_btn = new wxButton(m_sampler_editor, wxID_ANY, "View Sel", wxDefaultPosition, wxSize(-1, 25));
    m_view_sel_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FIND, wxART_BUTTON, wxSize(16, 16)));
    m_view_sel_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_view_sel, this);
    
    m_view_mode_ch = new wxChoice(m_sampler_editor, wxID_ANY);
    m_view_mode_ch->Append("Both"); m_view_mode_ch->Append("Left"); m_view_mode_ch->Append("Right");
    m_view_mode_ch->SetSelection(0);
    m_view_mode_ch->Bind(wxEVT_CHOICE, &InstrumentPanel::on_view_mode, this);

    wf_ctrl_sizer->Add(m_zoom_in_btn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 1);
    wf_ctrl_sizer->Add(m_zoom_out_btn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 1);
    wf_ctrl_sizer->Add(m_view_all_btn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 1);
    wf_ctrl_sizer->Add(m_view_sel_btn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 1);
    wf_ctrl_sizer->Add(m_view_mode_ch, 0, wxALIGN_CENTER_VERTICAL | wxALL, 1);
    sampler_sizer->Add(wf_ctrl_sizer, 0, wxEXPAND | wxALL, 2);

    m_sampler_editor->SetSizer(sampler_sizer);
    
    // Create m_sample_fmt_ch early so it can be used in update_editor
    m_sample_fmt_ch = new wxChoice(m_sampler_editor, wxID_ANY);
    m_sample_fmt_ch->Append("Stereo");
    m_sample_fmt_ch->Append("Stereo -> Mono (L)");
    m_sample_fmt_ch->Append("Stereo -> Mono (R)");
    m_sample_fmt_ch->Append("Stereo -> Mono (Mix)");
    m_sample_fmt_ch->Append("Mono -> Stereo");
    m_sample_fmt_ch->SetSelection(0);
    m_sample_fmt_ch->Bind(wxEVT_CHOICE, &InstrumentPanel::on_sample_fmt, this);
    m_sample_fmt_ch->Hide(); // Will be shown in sample rows

    right_sizer->Add(m_sampler_editor, 1, wxEXPAND | wxALL, 2);

    // 2. SoundFont Editor
    m_sfont_editor = new wxPanel(m_right_panel, wxID_ANY);
    wxBoxSizer* sfont_sizer = new wxBoxSizer(wxVERTICAL);
    m_sfont_load_btn = new wxButton(m_sfont_editor, wxID_ANY, "Load SoundFont");
    m_sfont_load_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_BUTTON, wxSize(16, 16)));
    m_sfont_load_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&){
        wxFileDialog dlg(this, "Load SoundFont", "", "", "SoundFont files (*.sf2;*.sf3)|*.sf2;*.sf3", wxFD_OPEN);
        if (dlg.ShowModal() == wxID_OK) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::SoundFont) {
                static_cast<SoundFontInstrument*>(&inst)->load_soundfont(dlg.GetPath().ToStdString());
                update_editor();
            }
        }
    });
    sfont_sizer->Add(m_sfont_load_btn, 0, wxALL, 5);

    m_sfont_browser = new wxListBox(m_sfont_editor, wxID_ANY);
    m_sfont_browser->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& ev){
        int idx = m_sfont_browser->GetSelection();
        if (idx != wxNOT_FOUND) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::SoundFont) {
                static_cast<SoundFontInstrument*>(&inst)->set_preset(idx);
            }
        }
    });
    sfont_sizer->Add(m_sfont_browser, 1, wxEXPAND | wxALL, 5);

    m_sfont_vol_slider = new wxSlider(m_sfont_editor, wxID_ANY, 100, 0, 128);
    m_sfont_vol_slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent& ev){
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::SoundFont) {
            static_cast<SoundFontInstrument*>(&inst)->set_volume((float)m_sfont_vol_slider->GetValue() / 128.0f);
        }
    });
    sfont_sizer->Add(new wxStaticText(m_sfont_editor, wxID_ANY, "Volume"), 0, wxALIGN_LEFT|wxLEFT, 5);
    sfont_sizer->Add(m_sfont_vol_slider, 0, wxEXPAND | wxALL, 5);
    
    m_sfont_editor->SetSizer(sfont_sizer);
    m_sfont_editor->Hide();
    right_sizer->Add(m_sfont_editor, 1, wxEXPAND | wxALL, 2);

    // 3. SFZ Editor
    m_sfz_editor = new wxPanel(m_right_panel, wxID_ANY);
    wxBoxSizer* sfz_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* sfz_top = new wxBoxSizer(wxHORIZONTAL);
    m_sfz_load_btn = new wxButton(m_sfz_editor, wxID_ANY, "Load SFZ");
    m_sfz_load_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_BUTTON, wxSize(16, 16)));
    m_sfz_load_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        wxFileDialog dlg(this, "Load SFZ Instrument", "", "",
                         "SFZ files (*.sfz)|*.sfz", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK) return;
        if (m_selected_instrument < 0) return;
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::SFZ) {
            auto& sfz = static_cast<SfzInstrument&>(inst);
            if (sfz.load_sfz(dlg.GetPath().ToStdString()))
                update_editor();
        }
    });
    sfz_top->Add(m_sfz_load_btn, 0, wxALL, 5);
    m_sfz_path_label = new wxStaticText(m_sfz_editor, wxID_ANY, "(no file)",
                                         wxDefaultPosition, wxSize(200, -1),
                                         wxST_ELLIPSIZE_START | wxST_NO_AUTORESIZE);
    sfz_top->Add(m_sfz_path_label, 1, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    sfz_sizer->Add(sfz_top, 0, wxEXPAND);

    sfz_sizer->Add(new wxStaticText(m_sfz_editor, wxID_ANY, "Groups"), 0, wxLEFT | wxTOP, 5);
    m_sfz_browser = new wxListBox(m_sfz_editor, wxID_ANY);
    m_sfz_browser->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& ev) {
        if (m_selected_instrument < 0) return;
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::SFZ) {
            // -1 = all; index 0 = "All groups", rest are real group indices
            int idx = m_sfz_browser->GetSelection();
            static_cast<SfzInstrument&>(inst).set_group(idx - 1);
        }
    });
    sfz_sizer->Add(m_sfz_browser, 1, wxEXPAND | wxALL, 5);

    sfz_sizer->Add(new wxStaticText(m_sfz_editor, wxID_ANY, "Volume"), 0, wxLEFT, 5);
    m_sfz_vol_slider = new wxSlider(m_sfz_editor, wxID_ANY, 100, 0, 128);
    m_sfz_vol_slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        if (m_selected_instrument < 0) return;
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::SFZ)
            static_cast<SfzInstrument&>(inst).set_volume((float)m_sfz_vol_slider->GetValue() / 100.0f);
    });
    sfz_sizer->Add(m_sfz_vol_slider, 0, wxEXPAND | wxALL, 5);

    m_sfz_editor->SetSizer(sfz_sizer);
    m_sfz_editor->Hide();
    right_sizer->Add(m_sfz_editor, 1, wxEXPAND | wxALL, 2);

    m_plugin_editor = new wxPanel(m_right_panel, wxID_ANY);
    wxBoxSizer* plugin_sizer = new wxBoxSizer(wxVERTICAL);
    m_plugin_scan_btn = new wxButton(m_plugin_editor, wxID_ANY, "Scan Plugins");
    m_plugin_scan_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_FIND, wxART_BUTTON, wxSize(16, 16)));
    m_plugin_scan_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_plugin_scan, this);
    plugin_sizer->Add(m_plugin_scan_btn, 0, wxALL, 5);

    wxBoxSizer* pl_split = new wxBoxSizer(wxHORIZONTAL);
    m_plugin_browser = new wxListBox(m_plugin_editor, wxID_ANY, wxDefaultPosition, wxSize(250, -1));
    m_plugin_browser->Bind(wxEVT_LISTBOX, &InstrumentPanel::on_plugin_select, this);
    pl_split->Add(m_plugin_browser, 0, wxEXPAND | wxALL, 2);

    wxBoxSizer* pl_right_sizer = new wxBoxSizer(wxVERTICAL);
    
    // ZynAddSubFX Editor
    m_zyn_editor = new wxPanel(m_plugin_editor, wxID_ANY);
    wxBoxSizer* zyn_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* zyn_top = new wxBoxSizer(wxHORIZONTAL);
    m_zyn_bank_ch = new wxChoice(m_zyn_editor, wxID_ANY);
    m_zyn_bank_ch->Bind(wxEVT_CHOICE, &InstrumentPanel::on_zyn_bank, this);
    zyn_top->Add(new wxStaticText(m_zyn_editor, wxID_ANY, "Bank:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 2);
    zyn_top->Add(m_zyn_bank_ch, 1, wxALL, 2);
    zyn_sizer->Add(zyn_top, 0, wxEXPAND | wxALL, 2);
    
    wxBoxSizer* zyn_btns = new wxBoxSizer(wxHORIZONTAL);
    m_zyn_prev_btn = new wxButton(m_zyn_editor, wxID_ANY, "<", wxDefaultPosition, wxSize(30, 25));
    m_zyn_prev_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_BACK, wxART_BUTTON, wxSize(14, 14)));
    m_zyn_prev_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_zyn_prev, this);
    m_zyn_next_btn = new wxButton(m_zyn_editor, wxID_ANY, ">", wxDefaultPosition, wxSize(30, 25));
    m_zyn_next_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_BUTTON, wxSize(14, 14)));
    m_zyn_next_btn->Bind(wxEVT_BUTTON, &InstrumentPanel::on_zyn_next, this);
    zyn_btns->Add(m_zyn_prev_btn, 0, wxALL, 2);
    zyn_btns->Add(m_zyn_next_btn, 0, wxALL, 2);
    zyn_sizer->Add(zyn_btns, 0, wxALL, 2);
    
    m_zyn_preset_browser = new wxListBox(m_zyn_editor, wxID_ANY);
    m_zyn_preset_browser->Bind(wxEVT_LISTBOX, &InstrumentPanel::on_zyn_preset, this);
    zyn_sizer->Add(m_zyn_preset_browser, 1, wxEXPAND | wxALL, 2);
    m_zyn_editor->SetSizer(zyn_sizer);
    m_zyn_editor->Hide();
    pl_right_sizer->Add(m_zyn_editor, 0, wxEXPAND | wxALL, 0);

    m_plugin_scroll = new wxScrolledWindow(m_plugin_editor, wxID_ANY);
    m_plugin_scroll->SetScrollRate(0, 20);
    m_plugin_scroll->SetSizer(new wxBoxSizer(wxVERTICAL));
    pl_right_sizer->Add(m_plugin_scroll, 1, wxEXPAND | wxALL, 2);

    pl_split->Add(pl_right_sizer, 1, wxEXPAND | wxALL, 2);

    plugin_sizer->Add(pl_split, 1, wxEXPAND | wxALL, 2);
    m_plugin_editor->SetSizer(plugin_sizer);
    m_plugin_editor->Hide();
    right_sizer->Add(m_plugin_editor, 1, wxEXPAND | wxALL, 2);

    // 4. MIDI Editor
    m_midi_editor = new wxPanel(m_right_panel, wxID_ANY);
    wxBoxSizer* midi_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* m_ch_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_ch_sizer->Add(new wxStaticText(m_midi_editor, wxID_ANY, "Channel:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    m_midi_channel = new wxSpinCtrl(m_midi_editor, wxID_ANY);
    m_midi_channel->SetRange(1, 16);
    m_midi_channel->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& ev){
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Midi)
                static_cast<MidiInstrument*>(&inst)->set_channel(ev.GetPosition() - 1);
        }
    });
    m_ch_sizer->Add(m_midi_channel, 0, wxALL, 5);
    midi_sizer->Add(m_ch_sizer, 0, wxALL, 5);
    
    wxBoxSizer* m_pg_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_pg_sizer->Add(new wxStaticText(m_midi_editor, wxID_ANY, "Program:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    m_midi_program = new wxSpinCtrl(m_midi_editor, wxID_ANY);
    m_midi_program->SetRange(0, 127);
    m_midi_program->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& ev){
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Midi)
                static_cast<MidiInstrument*>(&inst)->set_program(ev.GetPosition());
        }
    });
    m_pg_sizer->Add(m_midi_program, 0, wxALL, 5);
    midi_sizer->Add(m_pg_sizer, 0, wxALL, 5);

    // Audio Input Selection
    wxBoxSizer* m_input_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_input_sizer->Add(new wxStaticText(m_midi_editor, wxID_ANY, "Audio Input:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    m_midi_input_choice = new wxChoice(m_midi_editor, wxID_ANY);
    m_midi_input_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent& ev) {
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Midi) {
                MidiInstrument* midi = static_cast<MidiInstrument*>(&inst);
                int sel = ev.GetSelection();
                uint32_t num_ins = m_engine.m_num_ins;
                
                if (sel == 0) {
                    // "Muted" / "Not Attached"
                    midi->set_audio_input(-1, -1);
                } else if (sel <= (int)num_ins) {
                    // Mono channel sel-1
                    midi->set_audio_input(sel - 1, -1);
                } else {
                    // Stereo pair
                    int pair_idx = sel - (int)num_ins - 1;
                    midi->set_audio_input(pair_idx * 2, pair_idx * 2 + 1);
                }
            }
        }
    });
    m_input_sizer->Add(m_midi_input_choice, 1, wxEXPAND | wxALL, 5);
    midi_sizer->Add(m_input_sizer, 0, wxEXPAND | wxALL, 5);

    m_midi_editor->SetSizer(midi_sizer);
    m_midi_editor->Hide();
    right_sizer->Add(m_midi_editor, 1, wxEXPAND | wxALL, 2);

    // Voice Instrument Editor
    m_voice_editor = new wxPanel(m_right_panel, wxID_ANY);
    wxBoxSizer* voice_sizer = new wxBoxSizer(wxVERTICAL);
    
    wxBoxSizer* voice_tts_sizer = new wxBoxSizer(wxHORIZONTAL);
    voice_tts_sizer->Add(new wxStaticText(m_voice_editor, wxID_ANY, "TTS Mode:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    m_voice_tts_mode_ch = new wxChoice(m_voice_editor, wxID_ANY);
    m_voice_tts_mode_ch->Append("Real-time (espeak-ng)");
    m_voice_tts_mode_ch->Append("Offline (Festival)");
    m_voice_tts_mode_ch->SetSelection(0);
    m_voice_tts_mode_ch->Bind(wxEVT_CHOICE, [this](wxCommandEvent& ev) {
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Voice) {
                VoiceInstrument* voice = static_cast<VoiceInstrument*>(&inst);
                int sel = ev.GetSelection();
                TTSMode mode = (sel == 0) ? TTSMode::RealTimeEspeak : TTSMode::OfflineFestival;
                voice->set_tts_mode(mode);
            }
        }
    });
    voice_tts_sizer->Add(m_voice_tts_mode_ch, 1, wxEXPAND | wxALL, 5);
    voice_sizer->Add(voice_tts_sizer, 0, wxEXPAND | wxALL, 5);
    
    // Language selector (populated from installed espeak-ng voices)
    wxBoxSizer* voice_lang_sizer = new wxBoxSizer(wxHORIZONTAL);
    voice_lang_sizer->Add(new wxStaticText(m_voice_editor, wxID_ANY, "Language:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    m_voice_language_ch = new wxChoice(m_voice_editor, wxID_ANY);
    {
        auto langs = VoiceInstrument::list_espeak_languages();
        if (langs.empty()) {
            m_voice_language_ch->Append("en", new wxStringClientData("en"));
        } else {
            for (auto& [code, label] : langs)
                m_voice_language_ch->Append(wxString::FromUTF8(label), new wxStringClientData(wxString::FromUTF8(code)));
        }
        m_voice_language_ch->SetSelection(0);
    }
    m_voice_language_ch->Bind(wxEVT_CHOICE, [this](wxCommandEvent& ev) {
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Voice) {
                VoiceInstrument* voice = static_cast<VoiceInstrument*>(&inst);
                int sel = ev.GetSelection();
                if (sel >= 0) {
                    auto* cd = dynamic_cast<wxStringClientData*>(m_voice_language_ch->GetClientObject(sel));
                    std::string code = cd ? cd->GetData().ToStdString() : ev.GetString().ToStdString();
                    voice->set_language(code);
                    voice->clear_cache();
                }
            }
        }
    });
    voice_lang_sizer->Add(m_voice_language_ch, 1, wxEXPAND | wxALL, 5);
    voice_sizer->Add(voice_lang_sizer, 0, wxEXPAND | wxALL, 5);

    // Gender selector
    wxBoxSizer* voice_gender_sizer = new wxBoxSizer(wxHORIZONTAL);
    voice_gender_sizer->Add(new wxStaticText(m_voice_editor, wxID_ANY, "Gender:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    m_voice_gender_ch = new wxChoice(m_voice_editor, wxID_ANY);
    m_voice_gender_ch->Append("Default");
    m_voice_gender_ch->Append("Male");
    m_voice_gender_ch->Append("Female");
    m_voice_gender_ch->SetSelection(0);
    m_voice_gender_ch->Bind(wxEVT_CHOICE, [this](wxCommandEvent& ev) {
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Voice) {
                VoiceInstrument* voice = static_cast<VoiceInstrument*>(&inst);
                voice->set_gender(ev.GetSelection());
                voice->clear_cache();
            }
        }
    });
    voice_gender_sizer->Add(m_voice_gender_ch, 1, wxEXPAND | wxALL, 5);
    voice_sizer->Add(voice_gender_sizer, 0, wxEXPAND | wxALL, 5);

    // Variant selector (pick among multiple voices matching language+gender)
    wxBoxSizer* voice_variant_sizer = new wxBoxSizer(wxHORIZONTAL);
    voice_variant_sizer->Add(new wxStaticText(m_voice_editor, wxID_ANY, "Variant:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    m_voice_variant_ch = new wxChoice(m_voice_editor, wxID_ANY);
    for (int v = 0; v <= 9; ++v)
        m_voice_variant_ch->Append(v == 0 ? "Default" : wxString::Format("%d", v));
    m_voice_variant_ch->SetSelection(0);
    m_voice_variant_ch->Bind(wxEVT_CHOICE, [this](wxCommandEvent& ev) {
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Voice) {
                VoiceInstrument* voice = static_cast<VoiceInstrument*>(&inst);
                voice->set_variant(ev.GetSelection());
                voice->clear_cache();
            }
        }
    });
    voice_variant_sizer->Add(m_voice_variant_ch, 1, wxEXPAND | wxALL, 5);
    voice_sizer->Add(voice_variant_sizer, 0, wxEXPAND | wxALL, 5);
    
    // Speed slider (0.5x to 2.0x)
    wxBoxSizer* voice_speed_sizer = new wxBoxSizer(wxHORIZONTAL);
    voice_speed_sizer->Add(new wxStaticText(m_voice_editor, wxID_ANY, "Speed:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    m_voice_speed_slider = new wxSlider(m_voice_editor, wxID_ANY, 50, 25, 100, wxDefaultPosition, wxSize(150, -1));
    m_voice_speed_slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent& ev) {
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Voice) {
                VoiceInstrument* voice = static_cast<VoiceInstrument*>(&inst);
                voice->set_speed(ev.GetInt() / 50.0f);  // 50=1.0x
            }
        }
    });
    voice_speed_sizer->Add(m_voice_speed_slider, 1, wxEXPAND | wxALL, 5);
    voice_sizer->Add(voice_speed_sizer, 0, wxEXPAND | wxALL, 5);
    
    // Pitch accent slider (0.0 to 1.0)
    wxBoxSizer* voice_accent_sizer = new wxBoxSizer(wxHORIZONTAL);
    voice_accent_sizer->Add(new wxStaticText(m_voice_editor, wxID_ANY, "Accent:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    m_voice_accent_slider = new wxSlider(m_voice_editor, wxID_ANY, 50, 0, 100, wxDefaultPosition, wxSize(150, -1));
    m_voice_accent_slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent& ev) {
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Voice) {
                VoiceInstrument* voice = static_cast<VoiceInstrument*>(&inst);
                voice->set_pitch_accent(ev.GetInt() / 100.0f);
            }
        }
    });
    voice_accent_sizer->Add(m_voice_accent_slider, 1, wxEXPAND | wxALL, 5);
    voice_sizer->Add(voice_accent_sizer, 0, wxEXPAND | wxALL, 5);
    
    // Process button (now before the phrase list)
    m_voice_process_btn = new wxButton(m_voice_editor, wxID_ANY, "Pre-render All 256 Phrases");
    m_voice_process_btn->SetBitmap(wxArtProvider::GetBitmap(wxART_REFRESH, wxART_BUTTON, wxSize(16, 16)));
    m_voice_process_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) {
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Voice) {
                VoiceInstrument* voice = static_cast<VoiceInstrument*>(&inst);
                
                // Scan all 256 phrases
                std::set<std::string> unique_texts;
                for (size_t i = 0; i < 256; ++i) {
                    std::string text = voice->get_text((uint8_t)i);
                    if (!text.empty()) {
                        unique_texts.insert(text);
                    }
                }
                
                if (unique_texts.empty()) {
                    wxMessageBox("No phrases found in this voice instrument", "Info");
                    return;
                }
                
                // Start worker thread
                voice->start_synthesis_worker();
                auto* worker = voice->get_worker();
                
                if (!worker) return;
                
                // Queue phrases across multiple octaves for better coverage
                // Standard range: C-2 (36) to C-6 (84) in steps of 4 semitones for reasonable cache size
                std::vector<uint8_t> notes;
                for (uint8_t note = 36; note <= 84; note += 4) {
                    notes.push_back(note);
                }
                
                size_t total_tasks = unique_texts.size() * notes.size();
                
                // Queue all texts for synthesis at different pitches
                for (const auto& text : unique_texts) {
                    for (uint8_t note : notes) {
                        float freq = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
                        worker->queue_synthesis(text, freq, false);
                    }
                }
                
                // Create progress dialog
                wxProgressDialog* dlg = new wxProgressDialog(
                    "Pre-rendering Voice Audio", 
                    "Synthesizing phrases across octaves...",
                    (int)total_tasks, 
                    this,
                    wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME
                );
                
                fprintf(stderr, "[GUI] Starting pre-render poll loop for %zu tasks\n", total_tasks);

                bool aborted = false;
                while (true) {
                    int completed = (int)worker->get_completed();
                    
                    // Update dialog
                    wxString msg = wxString::Format("Completed %d of %d tasks\nMemory Used: %.2f MB", 
                                                    completed, (int)total_tasks, voice->get_memory_used() / (1024.0 * 1024.0));
                    
                    if (!dlg->Update(completed, msg)) {
                        fprintf(stderr, "[GUI] Pre-render ABORTED by user\n");
                        aborted = true;
                        break;
                    }

                    if (worker->is_queue_empty() && completed >= (int)total_tasks) {
                        fprintf(stderr, "[GUI] Pre-render FINISHED: %d tasks\n", completed);
                        break;
                    }

                    // Keep GUI responsive and wait a bit
                    wxSafeYield();
                    wxMilliSleep(50);
                }

                voice->stop_synthesis_worker();
                dlg->Destroy();

                if (!aborted) {
                    wxMessageBox(wxString::Format("Pre-rendered %d tasks across all phrases", (int)worker->get_completed()), "Complete");
                }
            }
        }
    });
    voice_sizer->Add(m_voice_process_btn, 0, wxEXPAND | wxALL, 5);

    // Phrase Selector and Editor
    wxStaticBoxSizer* phrase_box = new wxStaticBoxSizer(wxVERTICAL, m_voice_editor, "Phrases");
    
    wxBoxSizer* phrase_idx_sizer = new wxBoxSizer(wxHORIZONTAL);
    phrase_idx_sizer->Add(new wxStaticText(m_voice_editor, wxID_ANY, "Index:"), 0, wxALIGN_CENTER_VERTICAL|wxALL, 5);
    m_voice_phrase_idx = new wxSpinCtrl(m_voice_editor, wxID_ANY);
    m_voice_phrase_idx->SetRange(0, 255);
    m_voice_phrase_idx->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& ev) {
        int idx = ev.GetPosition();
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Voice) {
                VoiceInstrument* voice = static_cast<VoiceInstrument*>(&inst);
                m_voice_phrase_text->ChangeValue(wxString::FromUTF8(voice->get_text((uint8_t)idx)));
                m_voice_phrase_list->SetSelection(idx);
                m_voice_phrase_list->EnsureVisible(idx);
            }
        }
    });
    phrase_idx_sizer->Add(m_voice_phrase_idx, 0, wxALL, 5);
    phrase_box->Add(phrase_idx_sizer, 0, wxEXPAND | wxALL, 0);
    
    m_voice_phrase_text = new wxTextCtrl(m_voice_editor, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 80), wxTE_MULTILINE | wxTE_WORDWRAP);
    m_voice_phrase_text->Bind(wxEVT_TEXT, [this](wxCommandEvent& ev) {
        if (m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Voice) {
                VoiceInstrument* voice = static_cast<VoiceInstrument*>(&inst);
                uint8_t idx = (uint8_t)m_voice_phrase_idx->GetValue();
                std::string new_text = ev.GetString().ToStdString();
                voice->set_text(new_text, idx);
                
                // Update list box item text safely
                wxString label = wxString::Format("%02X: %s", (int)idx, wxString::FromUTF8(new_text));
                m_voice_phrase_list->SetString(idx, label);
            }
        }
    });
    phrase_box->Add(m_voice_phrase_text, 0, wxEXPAND | wxALL, 5);

    m_voice_phrase_list = new wxListBox(m_voice_editor, wxID_ANY, wxDefaultPosition, wxSize(-1, 150));
    m_voice_phrase_list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& ev) {
        int sel = ev.GetSelection();
        if (sel != wxNOT_FOUND && m_selected_instrument >= 0) {
            auto& inst = m_engine.instrument(m_selected_instrument);
            if (inst.type() == InstrumentType::Voice) {
                VoiceInstrument* voice = static_cast<VoiceInstrument*>(&inst);
                m_voice_phrase_text->ChangeValue(wxString::FromUTF8(voice->get_text((uint8_t)sel)));
                m_voice_phrase_idx->SetValue(sel);
            }
        }
    });
    phrase_box->Add(m_voice_phrase_list, 1, wxEXPAND | wxALL, 5);
    
    voice_sizer->Add(phrase_box, 1, wxEXPAND | wxALL, 5);
    
    m_voice_editor->SetSizer(voice_sizer);
    m_voice_editor->Hide();
    right_sizer->Add(m_voice_editor, 1, wxEXPAND | wxALL, 2);

    m_right_panel->SetSizer(right_sizer);

    update_rec_inputs();
    update_midi_input_choice();
    update_instrument_list();
    update_editor();
}

void InstrumentPanel::update_rec_inputs() {
    m_rec_input_ch->Clear();
    bool mono = m_mono_btn->GetValue();
    uint32_t num_ins = m_engine.m_num_ins;
    if (mono) {
        for (uint32_t i = 0; i < num_ins; ++i) {
            m_rec_input_ch->Append(wxString::Format("Mono %u", i + 1));
        }
    } else {
        for (uint32_t i = 0; i < num_ins; i += 2) {
            if (i + 1 < num_ins) m_rec_input_ch->Append(wxString::Format("%u/%u", i + 1, i + 2));
            else m_rec_input_ch->Append(wxString::Format("%u (L)", i + 1));
        }
    }
    if (m_rec_input_ch->GetCount() > 0) m_rec_input_ch->SetSelection(0);
}

void InstrumentPanel::update_midi_input_choice() {
    if (!m_midi_input_choice) return;
    
    m_midi_input_choice->Clear();
    uint32_t num_ins = m_engine.m_num_ins;
    
    // Option 0: Muted / Not Attached
    m_midi_input_choice->Append("Muted");
    
    // Mono channels
    for (uint32_t i = 0; i < num_ins; ++i) {
        m_midi_input_choice->Append(wxString::Format("Channel %u", i + 1));
    }
    
    // Stereo pairs
    for (uint32_t i = 0; i < num_ins; i += 2) {
        if (i + 1 < num_ins) {
            m_midi_input_choice->Append(wxString::Format("Stereo %u/%u", i + 1, i + 2));
        }
    }
    
    m_midi_input_choice->SetSelection(0);
}


void InstrumentPanel::update_instrument_list() {
    if (m_inst_scroll->GetSizer()) {
        m_inst_scroll->GetSizer()->Clear(true);
    }
    m_inst_scroll->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_tracker_bg));
    
    for (size_t i = 0; i < m_engine.instrument_count(); ++i) {
        auto& inst = m_engine.instrument(i);
        wxPanel* row = new wxPanel(m_inst_scroll, wxID_ANY);
        wxBoxSizer* row_sizer = new wxBoxSizer(wxHORIZONTAL);
        
        wxButton* sel = new wxButton(row, wxID_ANY, wxString::Format("%zu", i + 1), wxDefaultPosition, wxSize(36, -1), wxBORDER_NONE);
        if ((int)i == m_selected_instrument) {
            sel->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_selection_color));
            sel->SetForegroundColour(ThemeManager::contrastColor(m_engine.m_selection_color));
        } else {
            sel->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_bg_color));
            sel->SetForegroundColour(ThemeManager::toWxColour(m_engine.m_fg_color));
        }
        sel->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&){ 
            this->CallAfter([this, i](){ on_inst_select_idx(i); });
        });
        // Clicking the row background also selects it
        row->Bind(wxEVT_LEFT_DOWN, [this, i](wxMouseEvent& ev){
            ev.Skip();
            this->CallAfter([this, i](){ on_inst_select_idx(i); });
        });
        row_sizer->Add(sel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
        
        wxTextCtrl* name = new wxTextCtrl(row, wxID_ANY, inst.name(), wxDefaultPosition, wxSize(120, -1), wxTE_PROCESS_ENTER);
        name->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_tracker_bg));
        name->SetForegroundColour(ThemeManager::toWxColour(m_engine.m_tracker_text));
        // Clicking/focusing the name field also selects the row
        name->Bind(wxEVT_SET_FOCUS, [this, i](wxFocusEvent& ev){
            ev.Skip();
            if (m_selected_instrument != (int)i)
                this->CallAfter([this, i](){ on_inst_select_idx(i); });
        });
        auto inst_name_commit = [this, i, name](){ m_engine.instrument(i).set_name(name->GetValue().ToStdString()); };
        name->Bind(wxEVT_TEXT_ENTER, [inst_name_commit](wxCommandEvent&){ inst_name_commit(); });
        name->Bind(wxEVT_KILL_FOCUS,  [inst_name_commit](wxFocusEvent& ev){ ev.Skip(); inst_name_commit(); });
        row_sizer->Add(name, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
        
        wxChoice* type = new wxChoice(row, wxID_ANY);
        type->Append("None"); type->Append("Sampler"); type->Append("SoundFont"); type->Append("SFZ");
        type->SetSelection((int)inst.type());
        // Clicking/focusing the type choice also selects the row
        type->Bind(wxEVT_LEFT_DOWN, [this, i](wxMouseEvent& ev){
            ev.Skip();
            if (m_selected_instrument != (int)i)
                this->CallAfter([this, i](){ on_inst_select_idx(i); });
        });
        type->Bind(wxEVT_CHOICE, [this, i](wxCommandEvent& ev){
            int sel_idx = ev.GetSelection();
            this->CallAfter([this, i, sel_idx](){
                m_engine.set_instrument_type(i, (InstrumentType)sel_idx);
                m_selected_instrument = (int)i;
                m_selected_sample = -1;
                update_instrument_list();
                update_editor();
            });
        });
        row_sizer->Add(type, 0, wxALIGN_CENTER_VERTICAL, 0);

        row->SetSizer(row_sizer);
        m_inst_scroll->GetSizer()->Add(row, 0, wxEXPAND | wxBOTTOM, 2);
    }
    m_inst_scroll->Layout();
    m_inst_scroll->FitInside();
}

void InstrumentPanel::on_inst_select_idx(int idx) {
    m_selected_instrument = idx;
    m_selected_sample = -1;
    update_instrument_list();
    update_editor();
}

void InstrumentPanel::update_editor() {
    m_sampler_editor->Hide();
    m_sfont_editor->Hide();
    if (m_sfz_editor) m_sfz_editor->Hide();
    m_plugin_editor->Hide();
    m_midi_editor->Hide();
    m_voice_editor->Hide();
    if (m_sample_name_label) m_sample_name_label->SetLabel("");

    if (m_selected_instrument >= 0 && m_selected_instrument < (int)m_engine.instrument_count()) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        
        if (inst.type() == InstrumentType::Sampler) {
            m_sampler_editor->Show();
            SampleInstrument* sampler = static_cast<SampleInstrument*>(&inst);

            if (m_preview_fx_check) {
                bool on_track = false;
                for (size_t t = 0; t < m_engine.track_count(); ++t) {
                    if (m_engine.track(t).instrument() == &inst) { on_track = true; break; }
                }
                m_preview_fx_check->Enable(on_track);
                if (!on_track) m_preview_fx_check->SetValue(false);
            }
            
            // Safety: Reparent the format choice back to the editor before clearing the scroll panel
            // to avoid it being destroyed when the rows are deleted.
            m_sample_fmt_ch->Reparent(m_sampler_editor);
            m_sample_fmt_ch->Hide();

            if (m_sample_scroll->GetSizer()) {
                m_sample_scroll->GetSizer()->Clear(true);
            }
            m_sample_scroll->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_tracker_bg));

            for (size_t i = 0; i < sampler->sample_count(); ++i) {
                const auto& entry = sampler->get_sample(i);
                wxPanel* row = new wxPanel(m_sample_scroll, wxID_ANY);
                wxBoxSizer* rs = new wxBoxSizer(wxHORIZONTAL);
                
                wxButton* sel = new wxButton(row, wxID_ANY, wxString::Format("%zu", i + 1), wxDefaultPosition, wxSize(36, -1), wxBORDER_NONE);
                if ((int)i == m_selected_sample) {
                    sel->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_selection_color));
                    sel->SetForegroundColour(ThemeManager::contrastColor(m_engine.m_selection_color));
                } else {
                    sel->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_bg_color));
                    sel->SetForegroundColour(ThemeManager::toWxColour(m_engine.m_fg_color));
                }
                auto select_sample_row = [this, i](){
                    m_selected_sample = (int)i;
                    static_cast<SampleInstrument*>(&m_engine.instrument(m_selected_instrument))->set_selected_sample(i);
                    update_editor();
                };
                sel->Bind(wxEVT_BUTTON, [this, select_sample_row](wxCommandEvent&){ select_sample_row(); });
                // Clicking the row background also selects it
                row->Bind(wxEVT_LEFT_DOWN, [this, select_sample_row](wxMouseEvent& ev){
                    ev.Skip(); select_sample_row();
                });
                rs->Add(sel, 0, wxALL, 1);
                
                wxTextCtrl* name = new wxTextCtrl(row, wxID_ANY, entry.name, wxDefaultPosition, wxSize(80, -1), wxTE_PROCESS_ENTER);
                name->SetBackgroundColour(ThemeManager::toWxColour(m_engine.m_tracker_bg));
                name->SetForegroundColour(ThemeManager::toWxColour(m_engine.m_tracker_text));
                // Clicking/focusing the name field also selects the row
                name->Bind(wxEVT_SET_FOCUS, [this, i, select_sample_row](wxFocusEvent& ev){
                    ev.Skip();
                    if (m_selected_sample != (int)i) select_sample_row();
                });
                auto smp_name_commit = [this, i, name](){
                    if (m_selected_instrument >= 0)
                        static_cast<SampleInstrument*>(&m_engine.instrument(m_selected_instrument))->set_sample_name(i, name->GetValue().ToStdString());
                };
                name->Bind(wxEVT_TEXT_ENTER, [smp_name_commit](wxCommandEvent&){ smp_name_commit(); });
                name->Bind(wxEVT_KILL_FOCUS,  [smp_name_commit](wxFocusEvent& ev){ ev.Skip(); smp_name_commit(); });
                rs->Add(name, 1, wxALIGN_CENTER_VERTICAL | wxALL, 1);

                wxButton* load = new wxButton(row, wxID_ANY, "L", wxDefaultPosition, wxSize(25, 25));
                load->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_BUTTON, wxSize(14, 14)));
                load->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent& ev){ 
                    m_selected_sample = (int)i;
                    wxCommandEvent dummy; on_load_sample(dummy); 
                });
                rs->Add(load, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);

                wxButton* save = new wxButton(row, wxID_ANY, "S", wxDefaultPosition, wxSize(25, 25));
                save->SetBitmap(wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_BUTTON, wxSize(14, 14)));
                save->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent& ev){
                    m_selected_sample = (int)i;
                    wxCommandEvent dummy; on_save_sample(dummy);
                });
                rs->Add(save, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);

                wxButton* up = new wxButton(row, wxID_ANY, "U", wxDefaultPosition, wxSize(25, 25));
                up->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_UP, wxART_BUTTON, wxSize(14, 14)));
                up->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&){
                    if (i > 0) {
                        static_cast<SampleInstrument*>(&m_engine.instrument(m_selected_instrument))->move_sample(i, i - 1);
                        m_selected_sample = (int)(i - 1);
                        m_engine.refresh_timeline_audio_tracks();
                        update_editor();
                    }
                });
                rs->Add(up, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);

                wxButton* down = new wxButton(row, wxID_ANY, "D", wxDefaultPosition, wxSize(25, 25));
                down->SetBitmap(wxArtProvider::GetBitmap(wxART_GO_DOWN, wxART_BUTTON, wxSize(14, 14)));
                down->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&){
                    auto* s = static_cast<SampleInstrument*>(&m_engine.instrument(m_selected_instrument));
                    if (i < s->sample_count() - 1) {
                        s->move_sample(i, i + 1);
                        m_selected_sample = (int)(i + 1);
                        m_engine.refresh_timeline_audio_tracks();
                        update_editor();
                    }
                });
                rs->Add(down, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);

                wxButton* rem = new wxButton(row, wxID_ANY, "X", wxDefaultPosition, wxSize(25, 25));
                rem->SetBitmap(wxArtProvider::GetBitmap(wxART_DELETE, wxART_BUTTON, wxSize(14, 14)));
                rem->SetForegroundColour(ThemeManager::toWxColour(m_engine.m_warning_color));
                rem->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&){
                    static_cast<SampleInstrument*>(&m_engine.instrument(m_selected_instrument))->remove_sample(i);
                    m_selected_sample = -1;
                    m_engine.refresh_timeline_audio_tracks();
                    update_editor();
                });
                rs->Add(rem, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);
                
                if ((int)i == m_selected_sample) {
                    m_sample_fmt_ch->Reparent(row);
                    m_sample_fmt_ch->Show();
                    rs->Add(m_sample_fmt_ch, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);
                }
                
                row->SetSizer(rs);
                m_sample_scroll->GetSizer()->Add(row, 0, wxEXPAND | wxBOTTOM, 1);
            }
            m_sample_scroll->Layout();
            m_sample_scroll->FitInside();

            // Update undo/redo button states
            if (m_selected_sample >= 0) {
                m_undo_btn->Enable(static_cast<SampleInstrument*>(&inst)->can_undo(m_selected_sample));
                m_redo_btn->Enable(static_cast<SampleInstrument*>(&inst)->can_redo(m_selected_sample));
                
                // Update format choice based on current sample
                auto const& sample = sampler->get_sample(m_selected_sample);
                if (sample.data) {
                    m_sample_fmt_ch->SetSelection(sample.data->right.empty() ? 1 : 0);
                }
            } else {
                m_undo_btn->Disable();
                m_redo_btn->Disable();
            }

            m_waveform_view->set_color(m_engine.m_waveform_color);
            if (m_selected_sample >= 0 && m_selected_sample < (int)sampler->sample_count()) {
                sampler->set_selected_sample(m_selected_sample);
                auto const& smp = sampler->get_sample(m_selected_sample);
                if (m_sample_name_label)
                    m_sample_name_label->SetLabel(wxString::Format("[%d] %s", m_selected_sample + 1, smp.name));
                m_waveform_view->set_sample(smp.data);
            } else {
                if (m_sample_name_label) m_sample_name_label->SetLabel("");
                m_waveform_view->set_sample(nullptr);
            }
        } 
        else if (inst.type() == InstrumentType::SoundFont) {
            m_sfont_editor->Show();
            SoundFontInstrument* sf = static_cast<SoundFontInstrument*>(&inst);
            m_sfont_browser->Clear();
            const auto& presets = sf->presets();
            for (const auto& p : presets) {
                m_sfont_browser->Append(wxString::Format("[%03d:%03d] %s", p.bank, p.num, wxString::FromUTF8(p.name)));
            }
            if (sf->current_preset() >= 0 && sf->current_preset() < (int)m_sfont_browser->GetCount()) {
                m_sfont_browser->SetSelection(sf->current_preset());
            }
            m_sfont_vol_slider->SetValue((int)(sf->get_volume() * 128));
            m_sfont_editor->Layout();
        }
        else if (inst.type() == InstrumentType::SFZ && m_sfz_editor) {
            m_sfz_editor->Show();
            SfzInstrument* sfz = static_cast<SfzInstrument*>(&inst);
            // Path label
            m_sfz_path_label->SetLabel(sfz->path().empty() ? "(no file)" :
                wxString::FromUTF8(sfz->path()));
            // Group browser
            m_sfz_browser->Clear();
            m_sfz_browser->Append("(All groups)");
            for (const auto& g : sfz->group_names())
                m_sfz_browser->Append(wxString::FromUTF8(g));
            int sel = sfz->current_group() + 1; // -1 → 0 ("All")
            if (sel >= 0 && sel < (int)m_sfz_browser->GetCount())
                m_sfz_browser->SetSelection(sel);
            m_sfz_vol_slider->SetValue((int)(sfz->get_volume() * 100.0f));
            m_sfz_editor->Layout();
        }
        else if (inst.type() == InstrumentType::Midi) {
            m_midi_editor->Show();
            MidiInstrument* midi = static_cast<MidiInstrument*>(&inst);
            m_midi_channel->SetValue(midi->channel() + 1);
            m_midi_program->SetValue(midi->program());
            
            // Update audio input choice
            int in_l, in_r;
            midi->get_audio_input(in_l, in_r);
            uint32_t num_ins = m_engine.m_num_ins;
            
            if (in_l < 0) {
                m_midi_input_choice->SetSelection(0); // Muted
            } else if (in_r < 0) {
                // Mono input
                m_midi_input_choice->SetSelection(in_l + 1);
            } else {
                // Stereo pair input
                int pair_idx = in_l / 2;
                m_midi_input_choice->SetSelection(num_ins + 1 + pair_idx);
            }
            
            m_midi_editor->Layout();
        }
        else if (inst.type() == InstrumentType::Voice) {
            m_voice_editor->Show();
            VoiceInstrument* voice = static_cast<VoiceInstrument*>(&inst);
            TTSMode mode = voice->tts_mode();
            m_voice_tts_mode_ch->SetSelection((mode == TTSMode::RealTimeEspeak) ? 0 : 1);

            // Language: find matching entry by language code stored as client data
            {
                const std::string& lang = voice->get_language();
                int found = 0;
                for (unsigned int i = 0; i < m_voice_language_ch->GetCount(); ++i) {
                    auto* cd = dynamic_cast<wxStringClientData*>(m_voice_language_ch->GetClientObject(i));
                    if (cd && cd->GetData().ToStdString() == lang) { found = (int)i; break; }
                }
                m_voice_language_ch->SetSelection(found);
            }
            m_voice_gender_ch->SetSelection(voice->get_gender());
            m_voice_variant_ch->SetSelection(voice->get_variant());
            m_voice_speed_slider->SetValue((int)(voice->get_speed() * 50.0f));
            m_voice_accent_slider->SetValue((int)(voice->get_pitch_accent() * 100.0f));
            
            m_voice_phrase_list->Clear();
            for (int i = 0; i < 256; ++i) {
                wxString label;
                label.Printf("%02X: %s", i, wxString::FromUTF8(voice->get_text((uint8_t)i)));
                m_voice_phrase_list->Append(label);
            }
            
            m_voice_phrase_idx->SetValue(0);
            m_voice_phrase_list->SetSelection(0);
            m_voice_phrase_text->ChangeValue(wxString::FromUTF8(voice->get_text(0)));
            m_voice_editor->Layout();
        }
        else if (inst.type() == InstrumentType::Plugin) {
            m_plugin_editor->Show();
            if (m_plugin_browser->GetCount() == 0) {
                wxCommandEvent dummy; on_plugin_scan(dummy);
            }
            
            std::string current_path;
            if (auto* dssi = dynamic_cast<DSSIInstrument*>(&inst)) {
                current_path = dssi->path();
                for (int i = 0; i < (int)m_plugin_browser->GetCount(); ++i) {
                    if (m_plugin_map.count(i)) {
                        auto const& info = m_plugin_map[i];
                        if (info.path == current_path && info.index == dssi->index()) {
                            m_plugin_browser->SetSelection(i);
                            break;
                        }
                    }
                }
            }

            std::string p_name = inst.plugin_name();
            bool is_zyn = (!p_name.empty() && p_name.find("ZynAddSubFX") != std::string::npos);
            if (is_zyn) {
                m_zyn_editor->Show();
                auto* dssi_zyn = dynamic_cast<DSSIInstrument*>(&inst);
                unsigned long saved_bank = dssi_zyn ? dssi_zyn->bank() : 0;
                unsigned long saved_prog = dssi_zyn ? dssi_zyn->program() : 0;

                if (m_zyn_bank_ch->GetCount() == 0) {
                    std::vector<std::string> bank_paths = {"/usr/share/zynaddsubfx/banks", "/usr/local/share/zynaddsubfx/banks"};
                    for (const auto& bp : bank_paths) {
                        wxDir dir(bp);
                        if (!dir.IsOpened()) continue;
                        wxString dirname;
                        bool cont = dir.GetFirst(&dirname, "", wxDIR_DIRS);
                        while (cont) {
                            m_zyn_bank_ch->Append(dirname);
                            cont = dir.GetNext(&dirname);
                        }
                    }
                }

                // Restore saved bank/program selection
                if (m_zyn_bank_ch->GetCount() > 0) {
                    int bidx = (saved_bank < (unsigned long)m_zyn_bank_ch->GetCount()) ? (int)saved_bank : 0;
                    m_zyn_bank_ch->SetSelection(bidx);
                    wxCommandEvent dummy; on_zyn_bank(dummy);
                    if (saved_prog < (unsigned long)m_zyn_preset_browser->GetCount()) {
                        m_zyn_preset_browser->SetSelection((int)saved_prog);
                        m_zyn_preset_browser->EnsureVisible((int)saved_prog);
                    }
                }
            } else {
                m_zyn_editor->Hide();
            }

            if (m_plugin_scroll->GetSizer()) {
                m_plugin_scroll->GetSizer()->Clear(true);
            }
            size_t num_params = inst.parameter_count();
            for (size_t i = 0; i < num_params; ++i) {
                auto param = inst.get_parameter(i);
                wxPanel* p_row = new wxPanel(m_plugin_scroll, wxID_ANY);
                wxBoxSizer* p_sizer = new wxBoxSizer(wxVERTICAL);
                
                wxStaticText* label = new wxStaticText(p_row, wxID_ANY, param.name);
                wxFont lbl_font = label->GetFont(); lbl_font.SetPointSize(8); label->SetFont(lbl_font);
                p_sizer->Add(label, 0, wxALL, 1);
                
                wxSlider* slider = new wxSlider(p_row, wxID_ANY, (int)(param.value * 1000), (int)(param.min * 1000), (int)(param.max * 1000));
                slider->Bind(wxEVT_SLIDER, [this, i](wxCommandEvent& ev){
                    m_engine.instrument(m_selected_instrument).set_parameter(i, (float)ev.GetInt() / 1000.0f);
                });
                p_sizer->Add(slider, 0, wxEXPAND | wxALL, 1);
                
                p_row->SetSizer(p_sizer);
                m_plugin_scroll->GetSizer()->Add(p_row, 0, wxEXPAND | wxBOTTOM, 2);
            }
            m_plugin_scroll->Layout();
            m_plugin_scroll->FitInside();
            m_plugin_editor->Layout();
        }
    }
    m_right_panel->Layout();
    m_right_panel->Refresh();
}

void InstrumentPanel::on_new(wxCommandEvent& event) {
    m_engine.add_instrument();
    m_selected_instrument = (int)m_engine.instrument_count() - 1;
    update_instrument_list();
    update_editor();
}

void InstrumentPanel::on_load(wxCommandEvent& event) {
    wxFileDialog dlg(this,
                     "Load Instrument",
                     "",
                     "",
                     kInstrumentArchiveOpenFilter,
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }

    LoadedSampleInstrument loaded;
    if (!load_sample_instrument_archive(dlg.GetPath().ToStdString(), loaded)) {
        wxMessageBox("Failed to load sample instrument.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    bool replace_selected = false;
    if (m_selected_instrument >= 0) {
        const int choice = wxMessageBox(
            "Replace the selected instrument?\nChoose No to load the sampler as a new instrument.",
            "Load Instrument",
            wxYES_NO | wxCANCEL | wxICON_QUESTION,
            this
        );
        if (choice == wxCANCEL) {
            return;
        }
        replace_selected = (choice == wxYES);
    }

    if (replace_selected) {
        if (m_engine.instrument(m_selected_instrument).type() == InstrumentType::Sampler) {
            m_engine.set_instrument_type(m_selected_instrument, InstrumentType::None);
        }
    } else {
        m_engine.add_instrument();
        m_selected_instrument = (int)m_engine.instrument_count() - 1;
    }

    m_engine.set_instrument_type(m_selected_instrument, InstrumentType::Sampler);
    auto& inst = m_engine.instrument(m_selected_instrument);
    inst.set_name(loaded.name);

    auto* sampler = static_cast<SampleInstrument*>(&inst);
    for (const auto& sample : loaded.samples) {
        sampler->add_sample(sample.name, sample.data);
    }

    if (!loaded.samples.empty()) {
        m_selected_sample = (int)loaded.selected_sample;
        sampler->set_selected_sample(loaded.selected_sample);
    } else {
        m_selected_sample = -1;
    }

    m_engine.refresh_timeline_audio_tracks();
    m_engine.mark_dirty();
    update_instrument_list();
    update_editor();
}

void InstrumentPanel::on_save(wxCommandEvent& event) {
    if (m_selected_instrument < 0) {
        wxMessageBox("Select a sample instrument to save.", "Save Instrument", wxOK | wxICON_INFORMATION);
        return;
    }

    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() != InstrumentType::Sampler) {
        wxMessageBox("Only sample instruments can be saved right now.", "Save Instrument", wxOK | wxICON_INFORMATION);
        return;
    }

    wxString default_name = wxFileName(inst.name()).GetFullName();
    if (default_name.empty()) {
        default_name = "instrument";
    }
    if (!default_name.Lower().EndsWith(".tpi")) {
        default_name += ".tpi";
    }

    wxFileDialog dlg(this,
                     "Save Instrument",
                     "",
                     default_name,
                     kInstrumentArchiveSaveFilter,
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }

    wxFileName out_path(dlg.GetPath());
    if (out_path.GetExt().CmpNoCase("dgi") != 0) {
        out_path.SetExt("dgi");
    }

    if (!save_sample_instrument_archive(static_cast<const SampleInstrument&>(inst),
                                        out_path.GetFullPath().ToStdString())) {
        wxMessageBox("Failed to save sample instrument.", "Error", wxOK | wxICON_ERROR);
    }
}

void InstrumentPanel::on_delete(wxCommandEvent& event) {
    if (m_selected_instrument >= 0) {
        if (wxMessageBox("Delete selected instrument?", "Confirm", wxYES_NO | wxICON_QUESTION) == wxYES) {
            m_engine.remove_instrument(m_selected_instrument);
            m_selected_instrument = -1;
            m_selected_sample = -1;
            update_instrument_list();
            update_editor();
        }
    }
}

void InstrumentPanel::on_inst_select(wxCommandEvent& event) {}

void InstrumentPanel::on_add_sample(wxCommandEvent& event) {
    if (m_selected_instrument < 0) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() == InstrumentType::Sampler) {
        static_cast<SampleInstrument*>(&inst)->add_sample("New Sample", std::make_shared<SampleData>());
        update_editor();
    }
}

void InstrumentPanel::on_sample_play(wxCommandEvent& event) {
    if (m_selected_instrument < 0) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() != InstrumentType::Sampler) return;
    auto* sampler = static_cast<SampleInstrument*>(&inst);

    bool use_fx = m_preview_fx_check && m_preview_fx_check->GetValue();
    bool do_loop = m_preview_loop_check && m_preview_loop_check->GetValue();

    // Determine selection region
    size_t sel_start = 0, sel_end = 0;
    bool has_sel = false;
    if (m_waveform_view) {
        size_t ws = m_waveform_view->selection_start();
        size_t we = m_waveform_view->selection_end();
        if (ws != we) {
            sel_start = ws;
            sel_end   = we;
            has_sel   = true;
        }
    }

    int track_for_fx = -1;
    if (use_fx) {
        for (size_t t = 0; t < m_engine.track_count(); ++t) {
            if (m_engine.track(t).instrument() == &inst) {
                track_for_fx = (int)t;
                break;
            }
        }
    }

    if (track_for_fx >= 0) {
        inst.note_off();
        inst.note_on(69, 100, 0, has_sel ? sel_start : 0);
        m_engine.start_sample_preview(nullptr, track_for_fx);
    } else {
        inst.note_off();
        int sel = (m_selected_sample >= 0 && m_selected_sample < (int)sampler->sample_count())
                  ? m_selected_sample : 0;
        if ((int)sampler->sample_count() > 0) {
            auto data = sampler->get_sample(sel).data;
            if (data) {
                m_engine.start_sample_preview(data, -1, sel_start, sel_end, do_loop);
            }
        }
    }
}

void InstrumentPanel::on_sample_stop(wxCommandEvent& event) {
    m_engine.stop_sample_preview();

    // Stop playback.
    if (m_selected_instrument >= 0)
        m_engine.instrument(m_selected_instrument).note_off();

    // Stop recording and commit data if active.
    if (!m_engine.is_recording_sample()) return;
    m_engine.stop_recording_sample();
    if (m_selected_instrument < 0) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() != InstrumentType::Sampler) return;
    auto* sampler = static_cast<SampleInstrument*>(&inst);
    if (m_engine.m_recording_sample_data && !m_engine.m_recording_sample_data->left.empty()) {
        if (m_selected_sample >= 0 && m_selected_sample < (int)sampler->sample_count()) {
            sampler->push_undo(m_selected_sample);
            sampler->get_sample(m_selected_sample).data = m_engine.m_recording_sample_data;
        } else {
            sampler->add_sample("Recorded Sample", m_engine.m_recording_sample_data);
            m_selected_sample = (int)sampler->sample_count() - 1;
        }
        m_engine.refresh_timeline_audio_tracks();
        update_editor();
    }
}

void InstrumentPanel::on_record_sample(wxCommandEvent& event) {
    if (m_engine.is_recording_sample()) return; // already recording
    uint32_t channel = 0;
    int sel = m_rec_input_ch->GetSelection();
    if (sel != wxNOT_FOUND) channel = (uint32_t)sel;
    bool mono = m_mono_btn->GetValue();
    if (!mono) channel *= 2;
    Engine::SampleRecordMode mode = (Engine::SampleRecordMode)m_rec_mode_ch->GetSelection();
    m_engine.start_recording_sample(mode, channel, mono);
}

void InstrumentPanel::on_mono_toggle(wxCommandEvent& event) {
    update_rec_inputs();
}

void InstrumentPanel::on_zoom_in(wxCommandEvent& event) { if (m_waveform_view) m_waveform_view->zoom_in(); }
void InstrumentPanel::on_zoom_out(wxCommandEvent& event) { if (m_waveform_view) m_waveform_view->zoom_out(); }
void InstrumentPanel::on_view_all(wxCommandEvent& event) { if (m_waveform_view) m_waveform_view->view_all(); }
void InstrumentPanel::on_view_sel(wxCommandEvent& event) { if (m_waveform_view) m_waveform_view->view_selection(); }
void InstrumentPanel::on_view_mode(wxCommandEvent& event) { if (m_waveform_view) m_waveform_view->set_channel_mode((ChannelMode)event.GetSelection()); }
void InstrumentPanel::on_sample_fmt(wxCommandEvent& event) {
    if (m_selected_sample >= 0 && m_selected_instrument >= 0) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Sampler) {
             static_cast<SampleInstrument*>(&inst)->push_undo(m_selected_sample);
             static_cast<SampleInstrument*>(&inst)->convert_sample_format(m_selected_sample, (SampleFormatAction)event.GetSelection());
             m_engine.refresh_timeline_audio_tracks();
             update_editor();
        }
    }
}

void InstrumentPanel::on_silence(wxCommandEvent& event) {
    if (m_selected_sample < 0 || !m_waveform_view) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() == InstrumentType::Sampler) {
        auto& sample = static_cast<SampleInstrument*>(&inst)->get_sample(m_selected_sample);
        if (sample.data) {
            static_cast<SampleInstrument*>(&inst)->push_undo(m_selected_sample);
            size_t s1 = m_waveform_view->selection_start(), s2 = m_waveform_view->selection_end();
            if (s1 == s2) { s1 = 0; s2 = sample.data->left.size(); }
            else if (s1 > s2) std::swap(s1, s2);
            sample.data->silence(s1, s2);
            update_editor();
        }
    }
}

void InstrumentPanel::on_insert_silence(wxCommandEvent& event) {
    if (m_selected_sample < 0 || !m_waveform_view) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() == InstrumentType::Sampler) {
        auto& sample = static_cast<SampleInstrument*>(&inst)->get_sample(m_selected_sample);
        if (sample.data) {
            wxString val = wxGetTextFromUser("Enter duration in seconds to insert:", "Insert Silence", "1.0", this);
            if (!val.IsEmpty()) {
                double seconds = 0;
                if (val.ToDouble(&seconds) && seconds > 0) {
                    static_cast<SampleInstrument*>(&inst)->push_undo(m_selected_sample);
                    size_t num_samples = (size_t)(seconds * sample.data->sample_rate);
                    size_t pos = m_waveform_view->selection_start();
                    sample.data->insert_silence(pos, num_samples);
                    m_engine.refresh_timeline_audio_tracks();
                    update_editor();
                }
            }
        }
    }
}

void InstrumentPanel::on_crop(wxCommandEvent& event) {
    if (m_selected_sample < 0 || !m_waveform_view) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() != InstrumentType::Sampler) return;

    auto* sampler = static_cast<SampleInstrument*>(&inst);
    auto& sample = sampler->get_sample(m_selected_sample);
    if (!sample.data) return;

    size_t s1 = m_waveform_view->selection_start(), s2 = m_waveform_view->selection_end();
    if (s1 == s2) {
        wxMessageBox("Select a range to crop.", "Crop Sample", wxOK | wxICON_INFORMATION);
        return;
    }
    if (s1 > s2) std::swap(s1, s2);

    sampler->push_undo(m_selected_sample);
    sample.data->crop(s1, s2);
    m_engine.refresh_timeline_audio_tracks();
    m_engine.mark_dirty();
    update_editor();
}

void InstrumentPanel::on_resample(wxCommandEvent& event) {
    if (m_selected_sample < 0) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() != InstrumentType::Sampler) return;

    auto* sampler = static_cast<SampleInstrument*>(&inst);
    auto& sample = sampler->get_sample(m_selected_sample);
    if (!sample.data || sample.data->sample_rate <= 0) return;

    wxString val = wxGetTextFromUser("Enter target sample rate in Hz:", "Resample Sample",
                                     wxString::Format("%d", sample.data->sample_rate), this);
    if (val.IsEmpty()) return;

    long target_rate = 0;
    if (!val.ToLong(&target_rate) || target_rate <= 0) {
        wxMessageBox("Enter a valid positive sample rate.", "Resample Sample", wxOK | wxICON_ERROR);
        return;
    }
    if (target_rate == sample.data->sample_rate) return;

    const double ratio = static_cast<double>(target_rate) / static_cast<double>(sample.data->sample_rate);
    auto new_data = std::make_shared<SampleData>(*sample.data);
    if (!Resampler::process(sample.data->left, new_data->left, ratio)) {
        wxMessageBox("Failed to resample left channel.", "Resample Sample", wxOK | wxICON_ERROR);
        return;
    }
    if (!sample.data->right.empty()) {
        if (!Resampler::process(sample.data->right, new_data->right, ratio)) {
            wxMessageBox("Failed to resample right channel.", "Resample Sample", wxOK | wxICON_ERROR);
            return;
        }
        const size_t stereo_frames = std::min(new_data->left.size(), new_data->right.size());
        new_data->left.resize(stereo_frames);
        new_data->right.resize(stereo_frames);
    } else {
        new_data->right.clear();
    }
    new_data->sample_rate = static_cast<int>(target_rate);

    sampler->push_undo(m_selected_sample);
    sampler->update_sample_data(m_selected_sample, new_data);
    m_engine.refresh_timeline_audio_tracks();
    m_engine.mark_dirty();
    update_editor();
}

void InstrumentPanel::on_time_stretch(wxCommandEvent& event) {
    if (m_selected_sample < 0) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() != InstrumentType::Sampler) return;

    auto* sampler = static_cast<SampleInstrument*>(&inst);
    auto& sample = sampler->get_sample(m_selected_sample);
    if (!sample.data || sample.data->sample_rate <= 0) return;

    wxString val = wxGetTextFromUser("Enter tempo percent (100 = unchanged):", "Time Stretch", "100", this);
    if (val.IsEmpty()) return;

    double tempo_percent = 0.0;
    if (!val.ToDouble(&tempo_percent) || tempo_percent <= 0.0) {
        wxMessageBox("Enter a valid positive tempo percentage.", "Time Stretch", wxOK | wxICON_ERROR);
        return;
    }
    if (std::abs(tempo_percent - 100.0) < 1e-6) return;

    const float tempo_ratio = static_cast<float>(tempo_percent / 100.0);
    auto new_data = std::make_shared<SampleData>(*sample.data);
    if (!TimeStretch::stretch(sample.data->left, new_data->left, tempo_ratio, (uint32_t)sample.data->sample_rate)) {
        wxMessageBox("Failed to time-stretch left channel.", "Time Stretch", wxOK | wxICON_ERROR);
        return;
    }
    if (!sample.data->right.empty()) {
        if (!TimeStretch::stretch(sample.data->right, new_data->right, tempo_ratio, (uint32_t)sample.data->sample_rate)) {
            wxMessageBox("Failed to time-stretch right channel.", "Time Stretch", wxOK | wxICON_ERROR);
            return;
        }
        const size_t stereo_frames = std::min(new_data->left.size(), new_data->right.size());
        new_data->left.resize(stereo_frames);
        new_data->right.resize(stereo_frames);
    } else {
        new_data->right.clear();
    }

    sampler->push_undo(m_selected_sample);
    sampler->update_sample_data(m_selected_sample, new_data);
    m_engine.refresh_timeline_audio_tracks();
    m_engine.mark_dirty();
    update_editor();
}

void InstrumentPanel::on_fade_in_lin(wxCommandEvent& event) {
    if (m_selected_sample < 0 || !m_waveform_view) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() == InstrumentType::Sampler) {
        auto& sample = static_cast<SampleInstrument*>(&inst)->get_sample(m_selected_sample);
        if (sample.data) {
            static_cast<SampleInstrument*>(&inst)->push_undo(m_selected_sample);
            size_t s1 = m_waveform_view->selection_start(), s2 = m_waveform_view->selection_end();
            if (s1 == s2) { s1 = 0; s2 = sample.data->left.size(); }
            else if (s1 > s2) std::swap(s1, s2);
            sample.data->fade_in(s1, s2, false);
            update_editor();
        }
    }
}

void InstrumentPanel::on_fade_in_log(wxCommandEvent& event) {
    if (m_selected_sample < 0 || !m_waveform_view) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() == InstrumentType::Sampler) {
        auto& sample = static_cast<SampleInstrument*>(&inst)->get_sample(m_selected_sample);
        if (sample.data) {
            static_cast<SampleInstrument*>(&inst)->push_undo(m_selected_sample);
            size_t s1 = m_waveform_view->selection_start(), s2 = m_waveform_view->selection_end();
            if (s1 == s2) { s1 = 0; s2 = sample.data->left.size(); }
            else if (s1 > s2) std::swap(s1, s2);
            sample.data->fade_in(s1, s2, true);
            update_editor();
        }
    }
}

void InstrumentPanel::on_fade_out_lin(wxCommandEvent& event) {
    if (m_selected_sample < 0 || !m_waveform_view) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() == InstrumentType::Sampler) {
        auto& sample = static_cast<SampleInstrument*>(&inst)->get_sample(m_selected_sample);
        if (sample.data) {
            static_cast<SampleInstrument*>(&inst)->push_undo(m_selected_sample);
            size_t s1 = m_waveform_view->selection_start(), s2 = m_waveform_view->selection_end();
            if (s1 == s2) { s1 = 0; s2 = sample.data->left.size(); }
            else if (s1 > s2) std::swap(s1, s2);
            sample.data->fade_out(s1, s2, false);
            update_editor();
        }
    }
}

void InstrumentPanel::on_fade_out_log(wxCommandEvent& event) {
    if (m_selected_sample < 0 || !m_waveform_view) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() == InstrumentType::Sampler) {
        auto& sample = static_cast<SampleInstrument*>(&inst)->get_sample(m_selected_sample);
        if (sample.data) {
            static_cast<SampleInstrument*>(&inst)->push_undo(m_selected_sample);
            size_t s1 = m_waveform_view->selection_start(), s2 = m_waveform_view->selection_end();
            if (s1 == s2) { s1 = 0; s2 = sample.data->left.size(); }
            else if (s1 > s2) std::swap(s1, s2);
            sample.data->fade_out(s1, s2, true);
            update_editor();
        }
    }
}

void InstrumentPanel::on_normalize(wxCommandEvent& event) {
    if (m_selected_sample >= 0 && m_waveform_view) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Sampler) {
            auto& sample = static_cast<SampleInstrument*>(&inst)->get_sample(m_selected_sample);
            if (sample.data) {
                static_cast<SampleInstrument*>(&inst)->push_undo(m_selected_sample);
                size_t s1 = m_waveform_view->selection_start(), s2 = m_waveform_view->selection_end();
                if (s1 == s2) { s1 = 0; s2 = sample.data->left.size(); }
                else if (s1 > s2) std::swap(s1, s2);
                sample.data->normalize(s1, s2);
                update_editor();
            }
        }
    }
}

void InstrumentPanel::on_adjust_vol(wxCommandEvent& event) {
    double val = m_vol_input->GetValue();
    if (m_selected_sample >= 0 && m_waveform_view) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Sampler) {
            auto& sample = static_cast<SampleInstrument*>(&inst)->get_sample(m_selected_sample);
            if (sample.data) {
                static_cast<SampleInstrument*>(&inst)->push_undo(m_selected_sample);
                size_t s1 = m_waveform_view->selection_start(), s2 = m_waveform_view->selection_end();
                if (s1 == s2) { s1 = 0; s2 = sample.data->left.size(); }
                else if (s1 > s2) std::swap(s1, s2);
                sample.data->adjust_volume(s1, s2, (float)val);
                update_editor();
            }
        }
    }
}

void InstrumentPanel::on_undo(wxCommandEvent& event) {
    if (m_selected_sample >= 0) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Sampler) {
            static_cast<SampleInstrument*>(&inst)->undo(m_selected_sample);
            m_engine.refresh_timeline_audio_tracks();
            update_editor();
        }
    }
}

void InstrumentPanel::on_redo(wxCommandEvent& event) {
    if (m_selected_sample >= 0) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Sampler) {
            static_cast<SampleInstrument*>(&inst)->redo(m_selected_sample);
            m_engine.refresh_timeline_audio_tracks();
            update_editor();
        }
    }
}

void InstrumentPanel::cut() {
    if (m_selected_sample >= 0 && m_waveform_view) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Sampler) {
            auto& sample = static_cast<SampleInstrument*>(&inst)->get_sample(m_selected_sample);
            if (sample.data) {
                static_cast<SampleInstrument*>(&inst)->push_undo(m_selected_sample);
                size_t s1 = m_waveform_view->selection_start(), s2 = m_waveform_view->selection_end();
                if (s1 == s2) { s1 = 0; s2 = sample.data->left.size(); }
                else if (s1 > s2) std::swap(s1, s2);
                m_engine.sample_clipboard().data = std::make_shared<SampleData>(sample.data->cut(s1, s2));
                m_engine.refresh_timeline_audio_tracks();
                update_editor();
            }
        }
    }
}

void InstrumentPanel::copy() {
    // Copy doesn't need undo
    if (m_selected_sample >= 0 && m_waveform_view) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Sampler) {
            auto& sample = static_cast<SampleInstrument*>(&inst)->get_sample(m_selected_sample);
            if (sample.data) {
                size_t s1 = m_waveform_view->selection_start(), s2 = m_waveform_view->selection_end();
                if (s1 == s2) { s1 = 0; s2 = sample.data->left.size(); }
                else if (s1 > s2) std::swap(s1, s2);
                auto cb = std::make_shared<SampleData>();
                cb->sample_rate = sample.data->sample_rate;
                cb->left.assign(sample.data->left.begin() + s1, sample.data->left.begin() + s2);
                if (!sample.data->right.empty())
                    cb->right.assign(sample.data->right.begin() + s1, sample.data->right.begin() + s2);
                m_engine.sample_clipboard().data = cb;
            }
        }
    }
}

void InstrumentPanel::paste() {
    if (m_selected_sample >= 0 && m_waveform_view && m_engine.sample_clipboard().data) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Sampler) {
            auto& sample = static_cast<SampleInstrument*>(&inst)->get_sample(m_selected_sample);
            if (sample.data) {
                static_cast<SampleInstrument*>(&inst)->push_undo(m_selected_sample);
                sample.data->paste_at(m_waveform_view->selection_start(), *m_engine.sample_clipboard().data);
                m_engine.refresh_timeline_audio_tracks();
                update_editor();
            }
        }
    }
}

void InstrumentPanel::on_inst_name(wxCommandEvent& event) {
    wxTextCtrl* ctrl = dynamic_cast<wxTextCtrl*>(event.GetEventObject());
    if (ctrl && m_selected_instrument >= 0) {
        m_engine.instrument(m_selected_instrument).set_name(ctrl->GetValue().ToStdString());
        update_instrument_list();
    }
}

void InstrumentPanel::on_inst_type(wxCommandEvent& event) {
    wxChoice* ctrl = dynamic_cast<wxChoice*>(event.GetEventObject());
    if (ctrl && m_selected_instrument >= 0) {
        m_engine.set_instrument_type(m_selected_instrument, (InstrumentType)ctrl->GetSelection());
        update_editor();
    }
}

void InstrumentPanel::on_load_sample(wxCommandEvent& event) {
    if (m_selected_instrument < 0) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() != InstrumentType::Sampler) return;

    wxFileDialog dlg(this, "Load Sample", "", "", "Audio Files|*.wav;*.flac;*.mp3;*.ogg", wxFD_OPEN);
    if (dlg.ShowModal() == wxID_OK) {
        auto data = std::make_shared<SampleData>();
        uint32_t sr = 0;
        if (AudioFile::load_audio(dlg.GetPath().ToStdString(), data->left, data->right, sr)) {
            data->sample_rate = (int)sr;
            auto* s = static_cast<SampleInstrument*>(&inst);
            if (m_selected_sample < 0) {
                s->add_sample(dlg.GetFilename().ToStdString(), data);
                m_selected_sample = (int)s->sample_count() - 1;
            } else {
                s->set_sample_name(m_selected_sample, dlg.GetFilename().ToStdString());
                s->get_sample(m_selected_sample).data = data;
            }
            m_engine.refresh_timeline_audio_tracks();
            m_engine.mark_dirty();
            update_editor();
        }
    }
}

void InstrumentPanel::on_save_sample(wxCommandEvent& event) {
    if (m_selected_instrument < 0 || m_selected_sample < 0) return;
    auto& inst = m_engine.instrument(m_selected_instrument);
    if (inst.type() != InstrumentType::Sampler) return;
    auto* s = static_cast<SampleInstrument*>(&inst);
    auto& entry = s->get_sample(m_selected_sample);
    if (!entry.data) return;

    wxFileDialog dlg(this, "Save Sample", "", entry.name, "WAV Files|*.wav", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_OK) {
        wxFileName out_path(dlg.GetPath());
        if (out_path.GetExt().CmpNoCase("wav") != 0) {
            out_path.SetExt("wav");
        }
        if (!AudioFile::save_wav(out_path.GetFullPath().ToStdString(), entry.data->left, entry.data->right, (uint32_t)entry.data->sample_rate)) {
            wxMessageBox("Failed to save audio file.", "Error", wxOK | wxICON_ERROR);
        }
    }
}

void InstrumentPanel::on_remove_sample(wxCommandEvent& event) {
    if (m_selected_instrument >= 0 && m_selected_sample >= 0) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Sampler) {
            static_cast<SampleInstrument*>(&inst)->remove_sample(m_selected_sample);
            m_selected_sample = -1;
            m_engine.refresh_timeline_audio_tracks();
            update_editor();
        }
    }
}

void InstrumentPanel::on_move_sample_up(wxCommandEvent& event) {
    if (m_selected_instrument >= 0 && m_selected_sample > 0) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Sampler) {
            static_cast<SampleInstrument*>(&inst)->move_sample(m_selected_sample, m_selected_sample - 1);
            m_selected_sample--;
            update_editor();
        }
    }
}

void InstrumentPanel::on_move_sample_down(wxCommandEvent& event) {
    if (m_selected_instrument >= 0 && m_selected_sample >= 0) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Sampler) {
            auto* s = static_cast<SampleInstrument*>(&inst);
            if (m_selected_sample < (int)s->sample_count() - 1) {
                s->move_sample(m_selected_sample, m_selected_sample + 1);
                m_selected_sample++;
                update_editor();
            }
        }
    }
}

void InstrumentPanel::on_plugin_scan(wxCommandEvent& event) {
    m_plugin_browser->Clear();
    m_plugin_map.clear();
    std::vector<std::string> paths = {"/usr/lib/dssi", "/usr/local/lib/dssi", "/usr/lib/x86_64-linux-gnu/dssi"};
    for (const auto& path : paths) {
        wxDir dir(path); if (!dir.IsOpened()) continue;
        wxString filename; bool cont = dir.GetFirst(&filename, "*.so", wxDIR_FILES);
        while (cont) {
            wxString fullpath = path + "/" + filename;
            void* handle = dlopen(fullpath.ToStdString().c_str(), RTLD_LAZY);
            if (handle) {
                DSSI_Descriptor_Function df = (DSSI_Descriptor_Function)dlsym(handle, "dssi_descriptor");
                if (df) {
                    for (int i=0; ; ++i) {
                        const DSSI_Descriptor* desc = df(i); if (!desc) break;
                        wxString label = wxString::FromUTF8(desc->LADSPA_Plugin->Name) + " (DSSI)";
                        int item = m_plugin_browser->Append(label);
                        m_plugin_map[item] = {desc->LADSPA_Plugin->Name, fullpath.ToStdString(), i, false};
                    }
                }
                dlclose(handle);
            }
            cont = dir.GetNext(&filename);
        }
    }
}

void InstrumentPanel::on_plugin_select(wxCommandEvent& event) {
    int sel = m_plugin_browser->GetSelection();
    if (sel != wxNOT_FOUND && m_plugin_map.count(sel)) {
        auto& info = m_plugin_map[sel];
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Plugin) {
            if (static_cast<DSSIInstrument*>(&inst)->load_plugin(info.path, info.index)) {
                update_editor();
            }
        }
    }
}

void InstrumentPanel::on_plugin_param(wxScrollEvent& event) {}

void InstrumentPanel::on_zyn_bank(wxCommandEvent& event) {
    m_zyn_preset_browser->Clear();
    int bidx = m_zyn_bank_ch->GetSelection(); if (bidx == wxNOT_FOUND) return;
    wxString bank_name = m_zyn_bank_ch->GetString(bidx);
    std::vector<std::string> bank_paths = {"/usr/share/zynaddsubfx/banks", "/usr/local/share/zynaddsubfx/banks"};
    for (const auto& bp : bank_paths) {
        wxString full_path = wxString(bp) + "/" + bank_name;
        wxDir dir(full_path); if (!dir.IsOpened()) continue;
        wxString fname; bool cont = dir.GetFirst(&fname, "*.xiz", wxDIR_FILES);
        while (cont) {
            m_zyn_preset_browser->Append(fname);
            cont = dir.GetNext(&fname);
        }
    }
}

void InstrumentPanel::on_zyn_preset(wxCommandEvent& event) {
    int bidx = m_zyn_bank_ch->GetSelection();
    int pidx = m_zyn_preset_browser->GetSelection();
    if (bidx != wxNOT_FOUND && pidx != wxNOT_FOUND && m_selected_instrument >= 0) {
        auto& inst = m_engine.instrument(m_selected_instrument);
        if (inst.type() == InstrumentType::Plugin) {
            static_cast<DSSIInstrument*>(&inst)->load_program((unsigned long)bidx, (unsigned long)pidx);
        }
    }
}

void InstrumentPanel::on_zyn_prev(wxCommandEvent& event) {
    int sel = m_zyn_preset_browser->GetSelection();
    if (sel > 0) {
        m_zyn_preset_browser->SetSelection(sel - 1);
        wxCommandEvent dummy; on_zyn_preset(dummy);
    }
}

void InstrumentPanel::on_zyn_next(wxCommandEvent& event) {
    int sel = m_zyn_preset_browser->GetSelection();
    if (sel != wxNOT_FOUND && sel < (int)m_zyn_preset_browser->GetCount() - 1) {
        m_zyn_preset_browser->SetSelection(sel + 1);
        wxCommandEvent dummy; on_zyn_preset(dummy);
    }
}

void InstrumentPanel::on_detach(wxCommandEvent& event) {
    if (m_detached_window) {
        return;
    }
    Hide();
    m_detached_window = new DetachedFrame(this, "Instruments", GetParent(), m_tab_index);
    m_detached_window->set_on_detach_callback([this]() { m_detached_window = nullptr; });
}

} // namespace tpanar_ns
