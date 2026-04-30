/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once

#include "edit_command.h"
#include <vector>

namespace tpanar_ns {

class Track;
class Engine;

class TrackCopyCommand : public EditCommand {
public:
    TrackCopyCommand(Track& track, Engine& engine, size_t start_sample, size_t end_sample);
    ~TrackCopyCommand() override = default;

    void apply() override;
    void undo() override;

    const char* name() const { return "Track Copy"; }

private:
    Track& m_track;
    Engine& m_engine;
    size_t m_start_sample;
    size_t m_end_sample;
};

} // namespace tpanar_ns
