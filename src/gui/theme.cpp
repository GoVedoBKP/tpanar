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

#include "theme.h"
#include "../core/engine.h"
#include <wx/colour.h>

namespace tpanar_ns {

std::vector<Theme> ThemeManager::m_themes;
ThemeType ThemeManager::m_current_theme = ThemeType::Classic;
bool ThemeManager::m_initialized = false;

void ThemeManager::init_themes() {
    if (m_initialized) return;
    
    m_themes.resize(4);
    
    // Modern Dark
    m_themes[0].name = "Modern Dark";
    m_themes[0].background = 0x2D2D2DFF;
    m_themes[0].foreground = 0xDCDCDCFF;
    m_themes[0].selection = 0x4B4B4BFF;
    m_themes[0].inactive = 0x646464FF;
    m_themes[0].button_color = 0x3C3C3CFF;
    m_themes[0].input_background = 0x1E1E1EFF;
    m_themes[0].text_color = 0xDCDCDCFF;
    m_themes[0].label_color = 0xAAAAAAFF;
    m_themes[0].scheme = "none";
    m_themes[0].tracker_bg = 0x121212FF;
    m_themes[0].tracker_text = 0xB0B0B0FF;
    m_themes[0].tracker_cursor = 0x00A0FFFF;
    m_themes[0].tracker_row_highlight = 0x242424FF;
    m_themes[0].tracker_lpb_highlight = 0x1A1A1AFF;
    m_themes[0].tracker_note = 0x569CD6FF;
    m_themes[0].tracker_sample = 0x4EC9B0FF;
    m_themes[0].tracker_volume = 0xCE9178FF;
    m_themes[0].tracker_effect = 0xC586C0FF;
    m_themes[0].selection_color = 0x0078D4FF;
    m_themes[0].warning_color   = 0xFF5555FF;

    // Modern Light
    m_themes[1].name = "Modern Light";
    m_themes[1].background = 0xF3F3F3FF;
    m_themes[1].foreground = 0x333333FF;
    m_themes[1].selection = 0xADD6FFFF;
    m_themes[1].inactive = 0xCCCCCCFF;
    m_themes[1].button_color = 0xE5E5E5FF;
    m_themes[1].input_background = 0xFFFFFFFF;
    m_themes[1].text_color = 0x333333FF;
    m_themes[1].label_color = 0x666666FF;
    m_themes[1].scheme = "none";
    m_themes[1].tracker_bg = 0xFFFFFFFF;
    m_themes[1].tracker_text = 0x333333FF;
    m_themes[1].tracker_cursor = 0x007ACCFF;
    m_themes[1].tracker_row_highlight = 0xF0F7FFFF;
    m_themes[1].tracker_lpb_highlight = 0xF3F3F3FF;
    m_themes[1].tracker_note = 0x0000FFFF;
    m_themes[1].tracker_sample = 0x008000FF;
    m_themes[1].tracker_volume = 0xA31515FF;
    m_themes[1].tracker_effect = 0xAF5700FF;
    m_themes[1].selection_color = 0x007ACCFF;
    m_themes[1].warning_color   = 0xCC0000FF;

    // Classic (FastTracker II inspired)
    m_themes[2].name = "Classic";
    m_themes[2].background = 0xCCCCCCFF;
    m_themes[2].foreground = 0x000000FF;
    m_themes[2].selection = 0x808080FF;
    m_themes[2].inactive = 0xAAAAAAFF;
    m_themes[2].button_color = 0xCCCCCCFF;
    m_themes[2].input_background = 0xFFFFFFFF;
    m_themes[2].text_color = 0x000000FF;
    m_themes[2].label_color = 0x000000FF;
    m_themes[2].scheme = "none";
    m_themes[2].tracker_bg = 0x000000FF;
    m_themes[2].tracker_text = 0xC0C0C0FF;
    m_themes[2].tracker_cursor = 0xFFFF00FF;
    m_themes[2].tracker_row_highlight = 0x404040FF;
    m_themes[2].tracker_lpb_highlight = 0x202020FF;
    m_themes[2].tracker_note = 0xFFFFFFFF;
    m_themes[2].tracker_sample = 0x00FF00FF;
    m_themes[2].tracker_volume = 0x00FFFFFF;
    m_themes[2].tracker_effect = 0xFFFF00FF;
    m_themes[2].selection_color = 0x4040C0FF;
    m_themes[2].warning_color   = 0xCC0000FF;

    // Custom (Placeholder)
    m_themes[3] = m_themes[0];
    m_themes[3].name = "Custom";
    
    m_initialized = true;
}

void ThemeManager::apply_theme(ThemeType type) {
    init_themes();
    m_current_theme = type;
}

void ThemeManager::apply_theme(const Theme& theme) {
    init_themes();
    m_current_theme = ThemeType::Custom;
    m_themes[3] = theme;
    m_themes[3].name = "Custom";
}

void ThemeManager::apply_theme_and_settings(Engine& engine) {
    init_themes();
    ThemeType type = engine.m_gui_theme;
    m_current_theme = type;
    
    size_t idx = static_cast<size_t>(type);
    if (idx < m_themes.size()) {
        const Theme& theme = m_themes[idx];
        
        engine.m_gui_theme = type;
        engine.m_bg_color = theme.background;
        engine.m_fg_color = theme.foreground;
        engine.m_button_color = theme.button_color;
        
        engine.m_tracker_bg = theme.tracker_bg;
        engine.m_tracker_text = theme.tracker_text;
        engine.m_tracker_cursor = theme.tracker_cursor;
        engine.m_tracker_row_highlight = theme.tracker_row_highlight;
        engine.m_tracker_lpb_highlight = theme.tracker_lpb_highlight;
        engine.m_tracker_note = theme.tracker_note;
        engine.m_tracker_sample = theme.tracker_sample;
        engine.m_tracker_volume = theme.tracker_volume;
        engine.m_tracker_effect = theme.tracker_effect;

        engine.m_selection_color = theme.selection_color;
        engine.m_warning_color   = theme.warning_color;
        engine.m_input_bg_color  = theme.input_background;
        engine.m_label_color     = theme.label_color;
    }
}

wxColour ThemeManager::contrastColor(uint32_t bg) {
    uint8_t r = (bg >> 24) & 0xFF;
    uint8_t g = (bg >> 16) & 0xFF;
    uint8_t b = (bg >>  8) & 0xFF;
    // Perceived luminance (sRGB)
    double lum = 0.299 * r + 0.587 * g + 0.114 * b;
    return (lum >= 128.0) ? wxColour(0, 0, 0) : wxColour(255, 255, 255);
}

const std::vector<Theme>& ThemeManager::get_available_themes() {
    init_themes();
    return m_themes;
}

wxColour ThemeManager::toWxColour(uint32_t c) {
    uint8_t a = c & 0xFF;
    if (a == 0) a = 0xFF; // Force opaque if alpha is 0
    return wxColour((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, a);
}

} // namespace tpanar_ns
