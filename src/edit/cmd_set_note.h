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

namespace tpanar_ns
{

    class CmdSetNote : public tpanar_ns::EditCommand
    {
    public:
        CmdSetNote(tpanar_ns::Pattern& pat,
                   size_t track,
                   size_t row,
                   uint8_t new_note)
        : m_pattern(pat),
        m_track(track),
        m_row(row),
        m_new_note(new_note)
        {
            m_old_note =
            pat.event(track, row, 0).note; // Changed from pat.track(track).row(row).note
        }

        void apply() override
        {
            m_pattern.event(m_track, m_row, 0).note = // Changed from m_pattern.track(m_track).row(m_row).note
            m_new_note;
        }

        void undo() override
        {
            m_pattern.event(m_track, m_row, 0).note = // Changed from m_pattern.track(m_track).row(m_row).note
            m_old_note;
        }

    private:
        tpanar_ns::Pattern& m_pattern;
        size_t   m_track;
        size_t   m_row;

        uint8_t  m_old_note;
        uint8_t  m_new_note;
    };

} // namespace tpanar_ns
