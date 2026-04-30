/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once

#include "edit_command.h"
#include <vector>

namespace tpanar_ns {

class Track;

class TrackSilenceCommand : public EditCommand {
public:
    TrackSilenceCommand(Track& track, size_t start_sample, size_t end_sample);
    ~TrackSilenceCommand() override = default;

    void apply() override;
    void undo() override;

    const char* name() const { return "Track Silence"; }

private:
    Track& m_track;
    size_t m_start_sample;
    size_t m_end_sample;
    std::vector<float> m_saved_left;
    std::vector<float> m_saved_right;
    bool m_has_stereo = false;
};

} // namespace tpanar_ns
