/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once

#include "edit_command.h"

namespace tpanar_ns {

class Track;

class TrackInsertSilenceCommand : public EditCommand {
public:
    TrackInsertSilenceCommand(Track& track, size_t insert_sample, size_t duration_samples);
    ~TrackInsertSilenceCommand() override = default;

    void apply() override;
    void undo() override;

    const char* name() const { return "Insert Silence"; }

private:
    Track& m_track;
    size_t m_insert_sample;
    size_t m_duration_samples;
};

} // namespace tpanar_ns
