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

#include <memory>
#include <utility>
#include <iterator> // Add this for std::make_move_iterator
#include "undo_stack.h" // Assuming this is needed for class definition

namespace tpanar_ns
{

void UndoStack::execute(::std::unique_ptr<tpanar_ns::Command> cmd)
{
    cmd->execute();
    m_undo.push_back(::std::move(cmd));
    m_redo.clear();
    ++m_generation;
}

void UndoStack::undo()
{
    if (m_undo.empty())
        return;

    auto cmd = ::std::move(m_undo.back());
    m_undo.pop_back();

    cmd->undo();
    m_redo.push_back(::std::move(cmd));
    ++m_generation;
}

void UndoStack::redo()
{
    if (m_redo.empty())
        return;

    auto cmd = ::std::move(m_redo.back());
    m_redo.pop_back();

    cmd->execute();
    m_undo.push_back(::std::move(cmd));
    ++m_generation;
}

void UndoStack::execute_group(::std::vector<EditCommandPtr> cmds)
{
    for (auto& cmd : cmds)
    {
        cmd->execute();
    }
    for (auto& cmd : cmds)
    {
        m_undo.push_back(static_cast<::std::unique_ptr<tpanar_ns::Command>>(::std::move(cmd)));
    }
    m_redo.clear();
    ++m_generation;
}

} // namespace tpanar_ns
