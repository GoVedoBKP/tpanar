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

#include <string>
#include <map>
#include <vector>

namespace tpanar_ns {

enum class Action {
    Play,
    PlaySong,
    PlayPattern,
    PlayFromPosition,
    Stop,
    Record,
    ToggleMetronome,
    Undo,
    Redo,
    Copy,
    Cut,
    Paste,
    Clear,
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    NoteOff,
    NoteC,
    NoteCs,
    NoteD,
    NoteDs,
    NoteE,
    NoteF,
    NoteFs,
    NoteG,
    NoteGs,
    NoteA,
    NoteAs,
    NoteB,
    NoteC2,
    NoteCs2,
    NoteD2,
    NoteDs2,
    NoteE2,
    NoteF2,
    NoteFs2,
    NoteG2,
    NoteGs2,
    NoteA2,
    NoteAs2,
    NoteB2,
    NoteC3,
    NextPattern,
    PrevPattern,
    OctaveUp,
    OctaveDown,
    NextOrderPos,
    PrevOrderPos,
    JumpToRow0,
    JumpToRow16,
    JumpToRow32,
    JumpToRow48,
    InsertRow,
    DeleteRow,
    InsertPattern,
    DeletePattern,
    DuplicatePattern,
    SelectAll,
    MuteTrack,
    SoloTrack,
    JumpToNextColumn,
    JumpToPrevColumn,
    IncPatternIndex,
    DecPatternIndex
};

enum class KeyboardLayout {
    Auto,
    QWERTY,
    QWERTZ
};

struct KeyCombo {
    int key;
    int modifiers; // FL_CTRL, FL_SHIFT, etc.

    bool operator<(const KeyCombo& other) const {
        if (key != other.key) return key < other.key;
        return modifiers < other.modifiers;
    }
};

class KeyBindings {
public:
    KeyBindings();

    void set_defaults();
    void set_layout(KeyboardLayout layout);
    KeyboardLayout get_layout() const { return m_layout; }
    KeyboardLayout detect_layout();

    Action get_action(int key, int modifiers) const;
    std::string get_action_name(Action action) const;
    std::string get_key_name(Action action) const;
    void assign(Action action, int key, int modifiers);

    // Returns all known actions in display order (for building shortcut lists)
    static const std::vector<Action>& all_actions();

private:
    std::map<Action, KeyCombo> m_action_to_key;
    std::map<KeyCombo, Action> m_key_to_action;
    KeyboardLayout m_layout = KeyboardLayout::Auto;
};

} // namespace tpanar_ns
