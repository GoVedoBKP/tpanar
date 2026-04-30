/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once
#include "edit_command.h"
#include <vector>

namespace tpanar_ns
{

    class GroupCommand : public tpanar_ns::EditCommand
    {
    public:
        GroupCommand(::std::vector<tpanar_ns::EditCommandPtr> cmds)
        : m_cmds(::std::move(cmds)) {}

        void apply() override
        {
            for (auto& c : m_cmds)
                c->apply();
        }

        void undo() override
        {
            for (auto it = m_cmds.rbegin();
                 it != m_cmds.rend(); ++it)
                 (*it)->undo();
        }

    private:
        ::std::vector<tpanar_ns::EditCommandPtr> m_cmds;
    };

    class UndoStack
    {

    };

} // namespace tpanar_ns
