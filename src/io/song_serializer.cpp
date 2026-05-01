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

#include "song_serializer.h"
#include "../core/engine.h"
#include "../sequencer/sequencer.h"
#include "../sequencer/pattern.h"
#include "../mixer/track.h"
#include "../instrument/sample_instrument.h"
#include "../instrument/soundfont_instrument.h"
#include "../instrument/sfz_instrument.h"
#include "audio_file.h"

#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace tpanar_ns
{

    namespace {

        std::string normalize_asset_path(const fs::path& path)
        {
            std::error_code ec;
            fs::path normalized = fs::weakly_canonical(path, ec);
            if (ec) {
                ec.clear();
                normalized = fs::absolute(path, ec);
            }
            if (ec) {
                normalized = path;
            }
            return normalized.lexically_normal().generic_string();
        }

        std::string unique_embedded_filename(const fs::path& source_path,
                                             std::unordered_set<std::string>& used_names)
        {
            const std::string stem = source_path.stem().string().empty()
                ? "soundfont"
                : source_path.stem().string();
            const std::string ext = source_path.extension().string();

            std::string candidate = source_path.filename().string();
            if (candidate.empty()) {
                candidate = stem + ext;
            }

            size_t suffix = 2;
            while (!used_names.insert(candidate).second) {
                candidate = stem + "_" + std::to_string(suffix++) + ext;
            }
            return candidate;
        }

    } // namespace

    bool SongSerializer::save(const Engine& engine, const ::std::string& folder)
    {
        fs::path base_path(folder);
        if (!fs::exists(base_path)) {
            fs::create_directories(base_path);
        }

        fs::path samples_dir = base_path / "samples";
        fs::path soundfonts_dir = base_path / "soundfonts";
        fs::create_directories(samples_dir);
        fs::create_directories(soundfonts_dir);

        json j;
        j["tempo"] = engine.tempo();
        j["lpb"]   = engine.lpb();
        j["order"] = engine.order_list();

        json jmeta;
        jmeta["title"]  = engine.project_title();
        jmeta["artist"] = engine.project_artist();
        jmeta["album"]  = engine.project_album();
        jmeta["year"]   = engine.project_year();
        j["metadata"] = jmeta;

        json jproject_settings;
        jproject_settings["base_octave"] = engine.base_octave();
        jproject_settings["step_size"] = engine.step_size();
        jproject_settings["metronome_enabled"] = engine.m_metronome_enabled.load();
        jproject_settings["metronome_volume"] = engine.metronome_volume();
        jproject_settings["record_preroll_bars"] = engine.record_preroll_bars();
        jproject_settings["export_lead_in_bars"] = engine.export_lead_in_bars();
        j["project_settings"] = jproject_settings;

        json jmaster;
        jmaster["gain"] = engine.master_gain();
        jmaster["pan"] = engine.m_master.pan();
        jmaster["muted"] = engine.m_master.muted();
        jmaster["filter"] = engine.m_master.mastering_filter().get_state();
        jmaster["styles"] = engine.m_master.mastering_styles().get_state();
        jmaster["reference_matcher"] = engine.m_master.reference_matcher().get_state();
        j["master"] = jmaster;

        j["instruments"] = json::array();
        std::unordered_map<std::string, std::string> embedded_soundfonts;
        std::unordered_set<std::string> embedded_soundfont_names;
        for (size_t i = 0; i < engine.instrument_count(); ++i) {
            const Instrument& inst = engine.instrument(i);
            json jinst;
            jinst["name"] = inst.name();
            jinst["type"] = (int)inst.type();

            if (inst.type() == InstrumentType::Sampler) {
                const auto& sampler = static_cast<const SampleInstrument&>(inst);
                json jsamples = json::array();
                for (size_t s = 0; s < sampler.sample_count(); ++s) {
                    const auto& sample = sampler.get_sample(s);
                    std::string sample_filename = "inst_" + std::to_string(i) + "_s" + std::to_string(s) + ".wav";
                    fs::path sample_path = samples_dir / sample_filename;
                    
                    if (sample.data) {
                        AudioFile::save_wav(sample_path.string(), sample.data->left, sample.data->right, sample.data->sample_rate);
                        json js;
                        js["name"] = sample.name;
                        js["file"] = "samples/" + sample_filename;
                        jsamples.push_back(js);
                    }
                }
                jinst["samples"] = jsamples;
            } else if (inst.type() == InstrumentType::SoundFont) {
                const auto& sf = static_cast<const SoundFontInstrument&>(inst);
                if (!sf.path().empty()) {
                    fs::path sf_src(sf.path());
                    const std::string sf_key = normalize_asset_path(sf_src);
                    auto existing = embedded_soundfonts.find(sf_key);
                    if (existing != embedded_soundfonts.end()) {
                        jinst["soundfont"] = existing->second;
                    } else {
                        std::error_code exists_ec;
                        if (!fs::exists(sf_src, exists_ec) || exists_ec) {
                            return false;
                        }

                        const std::string sf_filename =
                            unique_embedded_filename(sf_src, embedded_soundfont_names);
                        const fs::path sf_dest = soundfonts_dir / sf_filename;
                        const std::string dest_key = normalize_asset_path(sf_dest);

                        if (sf_key != dest_key) {
                            std::error_code copy_ec;
                            fs::copy_file(sf_src, sf_dest, fs::copy_options::overwrite_existing, copy_ec);
                            if (copy_ec) {
                                return false;
                            }
                        }

                        const std::string embedded_path = "soundfonts/" + sf_filename;
                        embedded_soundfonts.emplace(sf_key, embedded_path);
                        jinst["soundfont"] = embedded_path;
                    }
                }
                jinst["preset"] = sf.current_preset();
            } else if (inst.type() == InstrumentType::SFZ) {
                // SFZ: save only the absolute path — sample files are NOT copied into the project
                const auto& sfz = static_cast<const SfzInstrument&>(inst);
                if (!sfz.path().empty()) {
                    jinst["sfz_path"] = sfz.path();
                    jinst["sfz_group"] = sfz.current_group();
                    jinst["sfz_volume"] = sfz.get_volume();
                }
            }
            j["instruments"].push_back(jinst);
        }

        j["tracks"] = json::array();
        for (size_t t = 0; t < engine.track_count(); ++t) {
            json jt;
            jt["name"] = engine.track(t).name();
            jt["kind"] = (int)engine.track(t).kind();
            jt["linked_track"] = engine.track(t).linked_track();
            jt["audio_sample_index"] = engine.track(t).audio_sample_index();
            jt["drum_type"] = (int)engine.track(t).drum_type();
            jt["instrument_index"] = engine.get_instrument_index(engine.track(t).instrument());
            jt["volume"] = engine.track(t).volume();
            jt["pan"] = engine.track(t).get_pan();
            jt["output_bus"] = engine.track(t).output_bus();
            jt["muted"] = engine.track(t).muted();
            jt["solo"] = engine.track(t).solo();
            jt["minimized"] = engine.track(t).is_minimized();
            jt["record_armed"] = engine.track(t).record_armed();
            
            int in_l, in_r;
            engine.track(t).get_audio_input(in_l, in_r);
            jt["audio_input_l"] = in_l;
            jt["audio_input_r"] = in_r;
            jt["input_delay_ms"] = engine.track(t).input_delay();
            jt["humanize_vel"]    = engine.track(t).humanize_vel();
            jt["humanize_timing"] = engine.track(t).humanize_timing();
            
            json jchain;
            engine.track(t).chain().to_json(&jchain);
            jt["dsp_chain"] = jchain;

            j["tracks"].push_back(jt);
        }

        j["buses"] = json::array();
        // Skip master bus (index 0) when saving - only save user buses
        for (size_t b = 1; b < engine.bus_count(); ++b) {
            json jb;
            jb["name"] = engine.bus(b).name();
            jb["volume"] = engine.bus(b).volume();
            jb["pan"] = engine.bus(b).pan();
            jb["muted"] = engine.bus(b).muted();

            json jchain;
            engine.bus(b).chain().to_json(&jchain);
            jb["dsp_chain"] = jchain;

            j["buses"].push_back(jb);
        }

        j["patterns"] = json::array();
        for (size_t p = 0; p < engine.pattern_count(); ++p)
        {
            const Pattern& pat = engine.pattern(p);
            j["patterns"].push_back(pat.to_json());
        }

        ::std::ofstream file(base_path / "song.json");
        if (!file) return false;
        file << j.dump(2);
        return true;
    }

    bool SongSerializer::load(Engine& engine, const ::std::string& folder)
    {
        fs::path base_path(folder);
        ::std::ifstream file(base_path / "song.json");
        if (!file) return false;

        json j;
        file >> j;

        engine.new_project(); 

        engine.set_tempo(j["tempo"]);
        engine.set_lpb(j["lpb"]);
        const ::std::vector<size_t> loaded_order =
            j.contains("order") ? j["order"].get<::std::vector<size_t>>() : ::std::vector<size_t>{0};

        if (j.contains("metadata")) {
            auto& jm = j["metadata"];
            engine.set_project_title(jm.value("title", "Untitled Project"));
            engine.set_project_artist(jm.value("artist", "Unknown Artist"));
            engine.set_project_album(jm.value("album", ""));
            engine.set_project_year(jm.value("year", ""));
        }

        if (j.contains("project_settings")) {
            auto& js = j["project_settings"];
            engine.set_base_octave(js.value("base_octave", 4));
            engine.set_step_size(js.value("step_size", 1u));
            engine.set_metronome_enabled(js.value("metronome_enabled", false));
            engine.set_metronome_volume(js.value("metronome_volume", 0.4f));
            engine.set_record_preroll_bars(js.value("record_preroll_bars", engine.record_preroll_bars()));
            engine.set_export_lead_in_bars(js.value("export_lead_in_bars", engine.export_lead_in_bars()));
        }

        if (j.contains("master")) {
            engine.set_master_gain(j["master"].value("gain", 1.0f));
            engine.m_master.set_pan(j["master"].value("pan", 0.0f));
            engine.m_master.set_mute(j["master"].value("muted", false));
            if (j["master"].contains("filter")) {
                engine.m_master.mastering_filter().set_state(j["master"]["filter"]);
            }
            if (j["master"].contains("styles")) {
                engine.m_master.mastering_styles().set_state(j["master"]["styles"]);
            }
            if (j["master"].contains("reference_matcher")) {
                engine.m_master.reference_matcher().set_state(j["master"]["reference_matcher"]);
            }
        }

        if (j.contains("instruments")) {
            engine.m_instruments.clear();
            for (auto& ji : j["instruments"]) {
                InstrumentType type = (InstrumentType)ji["type"];
                if (type == InstrumentType::Plugin ||
                    type == InstrumentType::Midi ||
                    type == InstrumentType::Voice ||
                    type == InstrumentType::XRNI) {
                    type = InstrumentType::None;
                }
                engine.add_instrument();
                size_t idx = engine.instrument_count() - 1;
                engine.set_instrument_type(idx, type);
                Instrument& inst = engine.instrument(idx);
                inst.set_name(ji["name"]);

                if (type == InstrumentType::Sampler && ji.contains("samples")) {
                    SampleInstrument& sampler = static_cast<SampleInstrument&>(inst);
                    for (auto& js : ji["samples"]) {
                        std::shared_ptr<SampleData> sd = std::make_shared<SampleData>();
                        fs::path sample_path = base_path / js["file"].get<std::string>();
                        uint32_t rate;
                        if (AudioFile::load_audio(sample_path.string(), sd->left, sd->right, rate)) {
                            sd->sample_rate = rate;
                            sampler.add_sample(js["name"], sd);
                        }
                    }
                } else if (type == InstrumentType::SoundFont && ji.contains("soundfont")) {
                    SoundFontInstrument& sf = static_cast<SoundFontInstrument&>(inst);
                    fs::path sf_path = base_path / ji["soundfont"].get<std::string>();
                    if (sf.load_soundfont(sf_path.string())) {
                        sf.set_preset(ji.value("preset", 0));
                    }
                } else if (type == InstrumentType::SFZ && ji.contains("sfz_path")) {
                    SfzInstrument& sfz = static_cast<SfzInstrument&>(inst);
                    // SFZ path is stored as absolute — load directly
                    if (sfz.load_sfz(ji["sfz_path"].get<std::string>())) {
                        sfz.set_group(ji.value("sfz_group", -1));
                        sfz.set_volume(ji.value("sfz_volume", 1.0f));
                    }
                }
            }
        }

        if (j.contains("buses")) {
            // Don't clear buses - master bus (index 0) must be preserved
            // Only remove user buses (1+)
            while (engine.bus_count() > 1) {
                engine.remove_bus(engine.bus_count() - 1);
            }
            
            for (auto& jb : j["buses"]) {
                engine.add_bus();
                MixerBus& bus = engine.bus(engine.bus_count() - 1);
                bus.set_name(jb["name"]);
                bus.set_volume(jb["volume"]);
                bus.set_pan(jb["pan"]);
                // Don't load output_bus - buses always output to master
                bus.set_mute(jb.value("muted", false));
                if (jb.contains("dsp_chain")) {
                    bus.chain().from_json(&jb["dsp_chain"]);
                }
            }
        }

        if (j.contains("tracks")) {
            engine.m_tracks.clear();
            for (auto& jt : j["tracks"]) {
                engine.add_track();
                Track& track = engine.track(engine.track_count() - 1);
                track.set_name(jt["name"]);
                track.set_kind((TrackKind)jt.value("kind", (int)TrackKind::Note));
                track.set_linked_track(jt.value("linked_track", -1));
                track.set_audio_sample_index((uint8_t)jt.value("audio_sample_index", 1));
                track.set_drum_type((DrumType)jt.value("drum_type", (int)DrumType::None));
                track.set_volume(jt["volume"]);
                track.set_pan(jt["pan"]);
                track.set_output_bus(jt.value("output_bus", -1));
                track.set_mute(jt.value("muted", false));
                track.set_solo(jt.value("solo", false));
                track.set_minimized(jt.value("minimized", false));
                track.set_record_armed(jt.value("record_armed", false));
                track.set_audio_input(jt.value("audio_input_l", -1), jt.value("audio_input_r", -1));
                track.set_input_delay(jt.value("input_delay_ms", 0.0f), engine.sample_rate());
                track.set_humanize_vel   (jt.value("humanize_vel",    (int)0));
                track.set_humanize_timing(jt.value("humanize_timing", (int)0));

                if (jt.contains("dsp_chain")) {
                    track.chain().from_json(&jt["dsp_chain"]);
                }
                int inst_idx = jt["instrument_index"];
                if (inst_idx >= 0 && inst_idx < (int)engine.instrument_count()) {
                    track.set_instrument(&engine.instrument(inst_idx));
                }
            }
        }

        auto& patterns = j["patterns"];
        engine.m_patterns.clear();
        for (size_t p = 0; p < patterns.size(); ++p)
        {
            size_t rows = patterns[p]["rows"];
            engine.m_patterns.push_back(std::make_unique<Pattern>(rows, engine.track_count()));
            Pattern& pat = *engine.m_patterns.back();

            auto& jtracks = patterns[p]["tracks"];
            for (size_t t = 0; t < jtracks.size() && t < pat.track_count(); ++t)
            {
                size_t cols = jtracks[t]["cols"];
                pat.set_column_count(t, cols);
                auto& jdata = jtracks[t]["data"];
                
                for (size_t r = 0; r < jdata.size() && r < pat.row_count(); ++r)
                {
                    auto& jrow = jdata[r];
                    for (size_t c = 0; c < jrow.size() && c < pat.column_count(t); ++c) {
                        auto& jev = jrow[c];
                        auto& ev = pat.event(t, r, c);
                        ev.note       = jev["note"];
                        ev.sample_idx = jev["sample"];
                        ev.volume     = jev["volume"];
                        ev.effect1    = jev["fx1"];
                        ev.param1     = jev["p1"];
                        ev.effect2    = jev["fx2"];
                        ev.param2     = jev["p2"];
                    }
                }
            }
        }
        engine.ensure_tempo_track();
        engine.ensure_pilot_track();
        engine.set_order(loaded_order);
        engine.sync_single_pattern_to_longest_audio_track();
        engine.m_order_pos.store(0);
        engine.m_edit_order_pos.store(0);
        return true;
    }

} // namespace tpanar_ns
