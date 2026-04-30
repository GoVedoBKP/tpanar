/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "cmd_track_silence.h"
#include "../mixer/track.h"
#include "../instrument/sample_instrument.h"

namespace tpanar_ns {

TrackSilenceCommand::TrackSilenceCommand(Track& track, size_t start_sample, size_t end_sample)
    : m_track(track), m_start_sample(start_sample), m_end_sample(end_sample)
{
    if (start_sample >= end_sample) {
        m_end_sample = start_sample;
    }
}

void TrackSilenceCommand::apply() {
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

    m_saved_left.clear();
    m_saved_right.clear();

    m_saved_left = std::vector<float>(sample.data->left.begin() + m_start_sample, sample.data->left.begin() + end);
    m_has_stereo = !sample.data->right.empty();
    if (m_has_stereo) {
        m_saved_right = std::vector<float>(sample.data->right.begin() + m_start_sample, sample.data->right.begin() + end);
    }

    std::fill(sample.data->left.begin() + m_start_sample, sample.data->left.begin() + end, 0.0f);
    if (m_has_stereo) {
        std::fill(sample.data->right.begin() + m_start_sample, sample.data->right.begin() + end, 0.0f);
    }
}

void TrackSilenceCommand::undo() {
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

    if (m_start_sample >= sample.data->left.size()) {
        return;
    }

    size_t end = std::min(m_end_sample, sample.data->left.size());

    std::copy(m_saved_left.begin(), m_saved_left.end(), sample.data->left.begin() + m_start_sample);
    if (m_has_stereo) {
        std::copy(m_saved_right.begin(), m_saved_right.end(), sample.data->right.begin() + m_start_sample);
    }
}

} // namespace tpanar_ns
