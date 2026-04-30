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

#include "engine.h"
#include "../io/song_serializer.h"
#include "../io/project_archive.h"
#include "../io/audio_file.h"
#include "../instrument/sample_instrument.h"
#include "../instrument/soundfont_instrument.h"
#include <filesystem>
#include <iostream>
#include <chrono>

namespace fs = std::filesystem;

namespace tpanar_ns
{

namespace {

std::string import_display_name(const fs::path& path, const std::string& fallback)
{
    const std::string stem = path.stem().string();
    return stem.empty() ? fallback : stem;
}

bool can_reuse_default_import_slot(const Engine& engine)
{
    (void)engine;
    return false;
}

void restore_default_import_slot(Engine& engine)
{
    engine.set_instrument_type(0, InstrumentType::None);
    engine.instrument(0).set_name("New Instrument");
    engine.track(0).set_name("New Track");
    engine.track(0).set_instrument(nullptr);
}

} // namespace

void Engine::save_project(const ::std::string& path)
{
    try {
        fs::path tmp_dir = fs::temp_directory_path() / "tpanar_save_tmp";
        if (fs::exists(tmp_dir)) fs::remove_all(tmp_dir);
        fs::create_directories(tmp_dir);

        if (SongSerializer::save(*this, tmp_dir.string())) {
            if (ProjectArchive::save(path, tmp_dir.string())) {
                std::cout << "Project saved successfully to " << path << std::endl;
                mark_saved();
            } else {
                std::cerr << "Failed to archive project to " << path << std::endl;
            }
        } else {
            std::cerr << "Failed to serialize song to temporary directory" << std::endl;
        }

        fs::remove_all(tmp_dir);
    } catch (const std::exception& e) {
        std::cerr << "Exception during save: " << e.what() << std::endl;
    }
}

void Engine::load_project(const ::std::string& path)
{
    try {
        fs::path tmp_base = fs::temp_directory_path() / "tpanar_load";
        fs::create_directories(tmp_base);
        
        // Create a unique temporary directory for this load session
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        fs::path tmp_dir = tmp_base / std::to_string(now);
        
        if (fs::exists(tmp_dir)) fs::remove_all(tmp_dir);
        fs::create_directories(tmp_dir);

        if (ProjectArchive::extract(path, tmp_dir.string())) {
            if (SongSerializer::load(*this, tmp_dir.string())) {
                m_project_temp_dir = tmp_dir.string();
                mark_saved();
                std::cout << "Project loaded successfully from " << path << std::endl;
            } else {
                std::cerr << "Failed to deserialize song from " << path << std::endl;
                fs::remove_all(tmp_dir);
            }
        } else {
            std::cerr << "Failed to extract project archive from " << path << std::endl;
            fs::remove_all(tmp_dir);
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception during load: " << e.what() << std::endl;
    }
}

bool Engine::import_audio(const ::std::string& path)
{
    const fs::path import_path(path);
    if (!fs::exists(import_path)) {
        std::cerr << "Import failed: file does not exist: " << path << std::endl;
        return false;
    }

    const std::string extension = import_path.extension().string();
    const bool is_soundfont = extension == ".sf2" || extension == ".SF2" ||
                              extension == ".sf3" || extension == ".SF3";
    const bool reuse_default_slot = can_reuse_default_import_slot(*this);

    size_t instrument_index = 0;
    size_t track_index = 0;

    if (!is_soundfont) {
        auto sample_data = std::make_shared<SampleData>();
        uint32_t sample_rate = 0;
        if (!AudioFile::load_audio(path, sample_data->left, sample_data->right, sample_rate)) {
            std::cerr << "Import failed: unsupported or unreadable audio file: " << path << std::endl;
            return false;
        }
        sample_data->sample_rate = (int)sample_rate;

        if (reuse_default_slot) {
            set_instrument_type(0, InstrumentType::Sampler);
        } else {
            add_instrument();
            instrument_index = instrument_count() - 1;
            set_instrument_type(instrument_index, InstrumentType::Sampler);
            add_track();
            track_index = track_count() - 1;
        }

        Instrument& inst = instrument(instrument_index);
        auto& sampler = static_cast<SampleInstrument&>(inst);
        sampler.add_sample(import_path.filename().string(), sample_data);
        sampler.set_selected_sample(sampler.sample_count() - 1);

        const std::string name = import_display_name(import_path, "Imported Audio");
        inst.set_name(name);
        track(track_index).set_name(name);
        track(track_index).set_kind(TrackKind::Audio);
        track(track_index).set_linked_track(-1);
        track(track_index).set_audio_sample_index(1);
        track(track_index).set_instrument(&inst);
        mark_dirty();
        std::cout << "Imported audio file: " << path << std::endl;
        return true;
    }

    if (reuse_default_slot) {
        set_instrument_type(0, InstrumentType::SoundFont);
    } else {
        add_instrument();
        instrument_index = instrument_count() - 1;
        set_instrument_type(instrument_index, InstrumentType::SoundFont);
        add_track();
        track_index = track_count() - 1;
    }

    Instrument& inst = instrument(instrument_index);
    auto& soundfont = static_cast<SoundFontInstrument&>(inst);
    if (!soundfont.load_soundfont(path)) {
        if (reuse_default_slot) {
            restore_default_import_slot(*this);
        } else {
            remove_track(track_index);
            remove_instrument(instrument_index);
        }
        std::cerr << "Import failed: could not load soundfont: " << path << std::endl;
        return false;
    }

    const std::string name = import_display_name(import_path, "Imported SoundFont");
    inst.set_name(name);
    track(track_index).set_name(name);
    track(track_index).set_kind(TrackKind::Note);
    track(track_index).set_linked_track(-1);
    track(track_index).set_instrument(&inst);
    mark_dirty();
    std::cout << "Imported soundfont: " << path << std::endl;
    return true;
}

} // namespace tpanar_ns
