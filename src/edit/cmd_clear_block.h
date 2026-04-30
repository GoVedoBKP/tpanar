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

    class CmdClearBlock : public tpanar_ns::EditCommand
    {
    public:
        CmdClearBlock(tpanar_ns::Pattern& pat,
                      size_t track_start,
                      size_t track_end,
                      size_t row_start,
                      size_t row_end)
        : m_pattern(pat),
        m_t0(track_start),
        m_t1(track_end),
        m_r0(row_start),
        m_r1(row_end)
        {
            for (size_t t = m_t0; t <= m_t1; ++t)
            {
                for (size_t r = m_r0; r <= m_r1; ++r)
                {
                    m_old_values.push_back(
                        pat.event(t, r, 0).note); // Changed from pat.track(t).row(r).note
                }
            }
        }

        void apply() override
        {
            size_t idx = 0;

            for (size_t t = m_t0; t <= m_t1; ++t)
            {
                for (size_t r = m_r0; r <= m_r1; ++r)
                {
                    m_pattern.event(t, r, 0).note = // Changed from m_pattern.track(t).row(r).note
                    tpanar_ns::NOTE_EMPTY;
                    idx++;
                }
            }
        }

        void undo() override
        {
            size_t idx = 0;

            for (size_t t = m_t0; t <= m_t1; ++t)
            {
                for (size_t r = m_r0; r <= m_r1; ++r)
                {
                    m_pattern.event(t, r, 0).note = // Changed from m_pattern.track(t).row(r).note
                    m_old_values[idx++];
                }
            }
        }

    private:
        tpanar_ns::Pattern& m_pattern;

        size_t m_t0, m_t1;
        size_t m_r0, m_r1;

        ::std::vector<uint8_t> m_old_values;
    };

} // namespace tpanar_ns
