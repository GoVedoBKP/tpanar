/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "cmd_track_copy.h"
#include "../mixer/track.h"
#include "../mixer/track_clipboard.h"
#include "../instrument/sample_instrument.h"
#include "../core/engine.h"

namespace tpanar_ns {

TrackCopyCommand::TrackCopyCommand(Track& track, Engine& engine, size_t start_sample, size_t end_sample)
    : m_track(track), m_engine(engine), m_start_sample(start_sample), m_end_sample(end_sample)
{
    if (start_sample >= end_sample) {
        m_end_sample = start_sample;
    }
}

void TrackCopyCommand::apply() {
    auto inst = m_track.instrument();
    if (!inst || inst->type() != InstrumentType::Sampler) {
        return;
    }

    auto sampler = static_cast<SampleInstrument*>(inst);
    size_t sel_idx = sampler->selected_sample();
    if (sel_idx >= sampler->sample_count()) {
        return;
    }

    auto& sample = sampler->get_sample(sel_idx);
    if (!sample.data) {
        return;
    }

    size_t sample_count = sample.data->left.size();
    if (m_start_sample >= sample_count) {
        return;
    }

    size_t end = std::min(m_end_sample, sample_count);

    std::vector<float> copy_left(sample.data->left.begin() + m_start_sample, sample.data->left.begin() + end);
    std::vector<float> copy_right;

    if (!sample.data->right.empty()) {
        copy_right = std::vector<float>(sample.data->right.begin() + m_start_sample, sample.data->right.begin() + end);
    }

    m_engine.track_clipboard().set_audio(copy_left, copy_right);
}

void TrackCopyCommand::undo() {
    m_engine.track_clipboard().clear();
}

} // namespace tpanar_ns
