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

#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <wx/colour.h>

namespace tpanar_ns {

enum class ThemeType {
    ModernDark,
    ModernLight,
    Classic,
    Custom
};

struct Theme {
    std::string name;
    uint32_t background;
    uint32_t foreground;
    uint32_t selection;
    uint32_t inactive;
    int boxtype;
    int button_boxtype;
    int label_boxtype;
    uint32_t button_color;
    uint32_t input_background;
    uint32_t text_color;
    uint32_t label_color;
    const char* scheme;

    uint32_t tracker_bg;
    uint32_t tracker_text;
    uint32_t tracker_cursor;
    uint32_t tracker_row_highlight;
    uint32_t tracker_lpb_highlight;
    uint32_t tracker_note;
    uint32_t tracker_sample;
    uint32_t tracker_volume;
    uint32_t tracker_effect;

    // UI accent / selection
    uint32_t selection_color; // active/focused item highlight
    uint32_t warning_color;   // destructive actions, error indicators
};

class ThemeManager {
public:
    static void apply_theme(ThemeType type);
    static void apply_theme(const Theme& theme);
    static void apply_theme_and_settings(class Engine& engine);
    static const std::vector<Theme>& get_available_themes();
    static ThemeType get_current_theme_type() { return m_current_theme; }
    static wxColour toWxColour(uint32_t c);

    // Returns wxBLACK or wxWHITE, whichever contrasts better against the given color.
    static wxColour contrastColor(uint32_t bg);

private:
    static void init_themes();
    static std::vector<Theme> m_themes;
    static ThemeType m_current_theme;
    static bool m_initialized;
};

} // namespace tpanar_ns
