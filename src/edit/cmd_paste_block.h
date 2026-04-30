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
#include "../core/engine.h"
#include <vector>

namespace tpanar_ns
{

    class CmdPasteBlock : public tpanar_ns::EditCommand
    {
    public:
        CmdPasteBlock(tpanar_ns::Pattern& pat,
                      const tpanar_ns::BlockClipboard& cb,
                      size_t dst_track,
                      size_t dst_row)
        : m_pattern(pat),
        m_clip(cb),
        m_dst_track(dst_track),
        m_dst_row(dst_row)
        {
            for (size_t t = 0; t < cb.width; ++t)
            {
                for (size_t r = 0; r < cb.height; ++r)
                {
                    size_t track = dst_track + t;
                    size_t row   = dst_row + r;

                    if (track < pat.track_count() &&
                        row   < pat.row_count())
                    {
                        m_old_values.push_back(
                            pat.event(track, row, 0).note); // Changed from pat.track(t).row(r).note
                    }
                    else
                    {
                        m_old_values.push_back(tpanar_ns::NOTE_EMPTY);
                    }
                }
            }
        }

        void apply() override
        {
            size_t idx = 0;

            for (size_t t = 0; t < m_clip.width; ++t)
            {
                for (size_t r = 0; r < m_clip.height; ++r)
                {
                    size_t track = m_dst_track + t;
                    size_t row   = m_dst_row + r;

                    if (track < m_pattern.track_count() &&
                        row   < m_pattern.row_count())
                    {
                        m_pattern.event(track, row, 0).note = // Changed from m_pattern.track(track).row(row).note
                        m_clip.notes[idx];
                    }

                    idx++;
                }
            }
        }

        void undo() override
        {
            size_t idx = 0;

            for (size_t t = 0; t < m_clip.width; ++t)
            {
                for (size_t r = 0; r < m_clip.height; ++r)
                {
                    size_t track = m_dst_track + t;
                    size_t row   = m_dst_row + r;

                    if (track < m_pattern.track_count() &&
                        row   < m_pattern.row_count())
                    {
                        m_pattern.event(track, row, 0).note = // Changed from m_pattern.track(track).row(row).note
                        m_old_values[idx];
                    }

                    idx++;
                }
            }
        }

    private:
        tpanar_ns::Pattern& m_pattern;
        tpanar_ns::BlockClipboard m_clip;

        size_t m_dst_track;
        size_t m_dst_row;

        ::std::vector<uint8_t> m_old_values;
    };

} // namespace tpanar_ns
