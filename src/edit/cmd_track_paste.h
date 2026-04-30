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

class TrackPasteCommand : public EditCommand {
public:
    TrackPasteCommand(Track& track, Engine& engine, size_t insert_sample);
    ~TrackPasteCommand() override = default;

    void apply() override;
    void undo() override;

    const char* name() const { return "Track Paste"; }

private:
    Track& m_track;
    Engine& m_engine;
    size_t m_insert_sample;
    std::vector<float> m_saved_left;
    std::vector<float> m_saved_right;
    size_t m_inserted_count = 0;
};

} // namespace tpanar_ns
