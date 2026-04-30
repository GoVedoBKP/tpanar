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
#include "edit_command.h"
#include "../sequencer/pattern.h"
#include <vector>

namespace tpanar_ns
{
    class CmdEditBlock : public tpanar_ns::EditCommand
    {
    public:
        struct CellEdit {
            size_t track;
            size_t row;
            size_t field;
            uint8_t old_val;
            uint8_t new_val;
            
            CellEdit(size_t t, size_t r, size_t f, uint8_t ov, uint8_t nv)
                : track(t), row(r), field(f), old_val(ov), new_val(nv) {}
        };

        CmdEditBlock(tpanar_ns::Pattern& pat, const std::vector<CellEdit>& edits)
            : m_pattern(pat), m_edits(edits) {}

        void apply() override {
            for (const auto& edit : m_edits) {
                m_pattern.set_field(edit.track, edit.row, edit.field, edit.new_val);
            }
        }

        void undo() override {
            for (const auto& edit : m_edits) {
                m_pattern.set_field(edit.track, edit.row, edit.field, edit.old_val);
            }
        }

    private:
        tpanar_ns::Pattern& m_pattern;
        std::vector<CellEdit> m_edits;
    };
}
