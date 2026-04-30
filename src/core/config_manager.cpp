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

#include "config_manager.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace tpanar_ns {

namespace {

AudioBackendType parse_audio_backend(const json& audio)
{
    const std::string backend = audio.value("backend", "jack");
    if (backend == "oss") {
        return AudioBackendType::Oss;
    }
    return AudioBackendType::Jack;
}

} // namespace

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

ConfigManager::ConfigManager() {
}

std::string ConfigManager::get_config_path() {
    const char* home = getenv("HOME");
    if (!home) return "config.json"; // Fallback to current dir

    fs::path p(home);
    p /= ".config";
    p /= "tpanar";
    
    if (!fs::exists(p)) {
        fs::create_directories(p);
    }
    
    p /= "config.json";
    return p.string();
}

std::string ConfigManager::config_path() {
    return get_config_path();
}

void ConfigManager::read_from(const std::string& path) {
    try {
        std::ifstream f(path);
        json j;
        f >> j;

        if (j.contains("audio")) {
            auto& ja = j["audio"];
            m_config.audio_backend = parse_audio_backend(ja);
            m_config.num_audio_ins = ja.value("ins", 2u);
            m_config.num_audio_outs = ja.value("outs", 2u);
        }
        
        if (j.contains("midi")) {
            auto& jm = j["midi"];
            m_config.num_midi_ins = jm.value("ins", 1u);
            m_config.num_midi_outs = jm.value("outs", 1u);
        }

        if (j.contains("gui")) {
            auto& jg = j["gui"];
            m_config.gui_button_height = jg.value("button_height", 25);
            m_config.gui_font_size = jg.value("font_size", 12);
            m_config.gui_theme = (ThemeType)jg.value("theme", (int)ThemeType::Classic);
            m_config.waveform_color = jg.value("waveform_color", 0x00FF0000u);
            m_config.bg_color = jg.value("bg_color", 0xCCCCCC00u);
            m_config.fg_color = jg.value("fg_color", 0x00000000u);
            m_config.button_color = jg.value("button_color", 0xCCCCCC00u);
            m_config.boxtype = jg.value("boxtype", 2);
            m_config.btn_boxtype = jg.value("btn_boxtype", 2);
            m_config.label_boxtype = jg.value("label_boxtype", 0);
        }

        if (j.contains("tracker")) {
            auto& jt = j["tracker"];
            m_config.tracker_bg = jt.value("bg", 0x1E1E1E00u);
            m_config.tracker_text = jt.value("text", 0xC8C8C800u);
            m_config.tracker_cursor = jt.value("cursor", 0xFFFF0000u);
            m_config.tracker_row_highlight = jt.value("row_highlight", 0x3C3C5000u);
            m_config.tracker_lpb_highlight = jt.value("lpb_highlight", 0x2D2D3700u);
            m_config.tracker_note = jt.value("note", 0xB4B4FF00u);
            m_config.tracker_sample = jt.value("sample", 0xB4FFB400u);
            m_config.tracker_volume = jt.value("volume", 0xFFB4B400u);
            m_config.tracker_effect = jt.value("effect", 0xFFFFB400u);
        }

        if (j.contains("keyboard")) {
            m_config.keyboard_layout = j["keyboard"].value("layout", 0);
        }

        if (j.contains("threading")) {
            m_config.num_worker_threads = j["threading"].value("worker_threads", 0u);
        }

        if (j.contains("workflow")) {
            auto& jw = j["workflow"];
            m_config.record_preroll_bars = std::max(0, std::min(2, jw.value("record_preroll_bars", 1)));
            m_config.export_lead_in_bars = std::max(0, std::min(2, jw.value("export_lead_in_bars", 0)));
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading config from " << path << ": " << e.what() << std::endl;
    }
}

void ConfigManager::write_to(const std::string& path) {
    try {
        json j;
        
        j["audio"]["backend"] = audio_backend_type_id(m_config.audio_backend);
        j["audio"]["ins"] = m_config.num_audio_ins;
        j["audio"]["outs"] = m_config.num_audio_outs;
        
        j["midi"]["ins"] = m_config.num_midi_ins;
        j["midi"]["outs"] = m_config.num_midi_outs;

        j["gui"]["button_height"] = m_config.gui_button_height;
        j["gui"]["font_size"] = m_config.gui_font_size;
        j["gui"]["theme"] = (int)m_config.gui_theme;
        j["gui"]["waveform_color"] = m_config.waveform_color;
        j["gui"]["bg_color"] = m_config.bg_color;
        j["gui"]["fg_color"] = m_config.fg_color;
        j["gui"]["button_color"] = m_config.button_color;
        j["gui"]["boxtype"] = m_config.boxtype;
        j["gui"]["btn_boxtype"] = m_config.btn_boxtype;
        j["gui"]["label_boxtype"] = m_config.label_boxtype;

        j["tracker"]["bg"] = m_config.tracker_bg;
        j["tracker"]["text"] = m_config.tracker_text;
        j["tracker"]["cursor"] = m_config.tracker_cursor;
        j["tracker"]["row_highlight"] = m_config.tracker_row_highlight;
        j["tracker"]["lpb_highlight"] = m_config.tracker_lpb_highlight;
        j["tracker"]["note"] = m_config.tracker_note;
        j["tracker"]["sample"] = m_config.tracker_sample;
        j["tracker"]["volume"] = m_config.tracker_volume;
        j["tracker"]["effect"] = m_config.tracker_effect;

        j["keyboard"]["layout"] = m_config.keyboard_layout;

        j["threading"]["worker_threads"] = m_config.num_worker_threads;
        j["workflow"]["record_preroll_bars"] = m_config.record_preroll_bars;
        j["workflow"]["export_lead_in_bars"] = m_config.export_lead_in_bars;

        std::ofstream f(path);
        f << j.dump(4);
    } catch (const std::exception& e) {
        std::cerr << "Error saving config to " << path << ": " << e.what() << std::endl;
    }
}

void ConfigManager::load() {
    std::string path = get_config_path();
    if (!fs::exists(path)) {
        save(); // Save defaults if file doesn't exist yet
        return;
    }
    read_from(path);
}

void ConfigManager::load_from(const std::string& path) {
    read_from(path);
}

void ConfigManager::save() {
    write_to(get_config_path());
}

void ConfigManager::save_to(const std::string& path) {
    write_to(path);
}

} // namespace tpanar_ns
