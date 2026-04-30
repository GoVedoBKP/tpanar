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

#ifndef TPANAR_CORE_CONFIG_MANAGER_H
#define TPANAR_CORE_CONFIG_MANAGER_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../audio/audio_backend.h"
#include "../gui/theme.h"

namespace tpanar_ns {

struct Config {
    // Audio/MIDI
    AudioBackendType audio_backend = AudioBackendType::Jack;
    uint32_t num_audio_ins = 2;
    uint32_t num_audio_outs = 2;
    uint32_t num_midi_ins = 1;
    uint32_t num_midi_outs = 1;

    // GUI
    int gui_button_height = 25;
    int gui_font_size = 12;
    ThemeType gui_theme = ThemeType::Classic;
    unsigned int waveform_color = 0x00FF0000;
    unsigned int bg_color = 0xCCCCCC00;
    unsigned int fg_color = 0x00000000;
    unsigned int button_color = 0xCCCCCC00;
    int boxtype = 2; // FL_UP_BOX
    int btn_boxtype = 2; // FL_UP_BOX
    int label_boxtype = 0; // FL_NO_BOX

    // Tracker
    unsigned int tracker_bg = 0x1E1E1E00;
    unsigned int tracker_text = 0xC8C8C800;
    unsigned int tracker_cursor = 0xFFFF0000;
    unsigned int tracker_row_highlight = 0x3C3C5000;
    unsigned int tracker_lpb_highlight = 0x2D2D3700;
    unsigned int tracker_note = 0xB4B4FF00;
    unsigned int tracker_sample = 0xB4FFB400;
    unsigned int tracker_volume = 0xFFB4B400;
    unsigned int tracker_effect = 0xFFFFB400;

    // Keyboard
    int keyboard_layout = 0; // Auto

    // Threading
    // 0 = off (single-threaded), 1-N = N worker threads, 255 = auto (hardware_concurrency - 1)
    uint32_t num_worker_threads = 0;

    // Workflow defaults
    int record_preroll_bars = 1;
    int export_lead_in_bars = 0;
};

class ConfigManager {
public:
    static ConfigManager& instance();

    void load();
    void save();
    void load_from(const std::string& path);
    void save_to(const std::string& path);

    std::string config_path();  // path to the default config file

    const Config& config() const { return m_config; }
    Config& config() { return m_config; }

private:
    ConfigManager();
    std::string get_config_path();
    void write_to(const std::string& path);
    void read_from(const std::string& path);

    Config m_config;
};

} // namespace tpanar_ns

#endif // TPANAR_CORE_CONFIG_MANAGER_H
