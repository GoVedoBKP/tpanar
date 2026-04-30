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
#include <memory>
#include "command.h" // Add this line
#include "../edit/edit_command.h" // Add this line

namespace tpanar_ns
{

    class UndoStack
    {
    public:
        void execute(::std::unique_ptr<tpanar_ns::Command> cmd);
        void undo();
        void redo();
        void execute_group(::std::vector<EditCommandPtr> cmds);

        size_t generation() const { return m_generation; }
        void   reset_generation()  { m_generation = 0; }

    private:
        ::std::vector<::std::unique_ptr<tpanar_ns::Command>> m_undo;
        ::std::vector<::std::unique_ptr<tpanar_ns::Command>> m_redo;
        size_t m_generation = 0;
    };

} // namespace tpanar_ns
