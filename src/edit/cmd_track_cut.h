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

class TrackCutCommand : public EditCommand {
public:
    TrackCutCommand(Track& track, Engine& engine, size_t start_sample, size_t end_sample);
    ~TrackCutCommand() override = default;

    void apply() override;
    void undo() override;

    const char* name() const { return "Track Cut"; }

private:
    Track& m_track;
    Engine& m_engine;
    size_t m_start_sample;
    size_t m_end_sample;
    std::vector<float> m_saved_left;
    std::vector<float> m_saved_right;
    bool m_has_stereo = false;
};

} // namespace tpanar_ns
