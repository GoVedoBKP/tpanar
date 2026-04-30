/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once
#include <memory>
#include "../core/command.h"

namespace tpanar_ns
{

    class EditCommand : public tpanar_ns::Command
    {
    public:
        virtual ~EditCommand() = default;

        virtual void apply() = 0;
        virtual void undo() override = 0;
        void execute() override { apply(); }
    };

    using EditCommandPtr =
    ::std::unique_ptr<tpanar_ns::EditCommand>;

} // namespace tpanar_ns
