/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "cmd_track_cut.h"
#include "../mixer/track.h"
#include "../mixer/track_clipboard.h"
#include "../instrument/sample_instrument.h"
#include "../core/engine.h"

namespace tpanar_ns {

TrackCutCommand::TrackCutCommand(Track& track, Engine& engine, size_t start_sample, size_t end_sample)
    : m_track(track), m_engine(engine), m_start_sample(start_sample), m_end_sample(end_sample)
{
    if (start_sample >= end_sample) {
        m_end_sample = start_sample;
    }
}

void TrackCutCommand::apply() {
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

    m_engine.track_clipboard().set_audio(m_saved_left, m_saved_right);

    std::vector<float> new_left;
    std::vector<float> new_right;

    new_left.insert(new_left.end(), sample.data->left.begin(), sample.data->left.begin() + m_start_sample);
    new_left.insert(new_left.end(), sample.data->left.begin() + end, sample.data->left.end());

    if (m_has_stereo) {
        new_right.insert(new_right.end(), sample.data->right.begin(), sample.data->right.begin() + m_start_sample);
        new_right.insert(new_right.end(), sample.data->right.begin() + end, sample.data->right.end());
    }

    sample.data->left = new_left;
    sample.data->right = new_right;
}

void TrackCutCommand::undo() {
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

    std::vector<float> restored_left;
    std::vector<float> restored_right;

    restored_left.insert(restored_left.end(), sample.data->left.begin(), sample.data->left.begin() + m_start_sample);
    restored_left.insert(restored_left.end(), m_saved_left.begin(), m_saved_left.end());
    restored_left.insert(restored_left.end(), sample.data->left.begin() + m_start_sample, sample.data->left.end());

    if (m_has_stereo) {
        restored_right.insert(restored_right.end(), sample.data->right.begin(), sample.data->right.begin() + m_start_sample);
        restored_right.insert(restored_right.end(), m_saved_right.begin(), m_saved_right.end());
        restored_right.insert(restored_right.end(), sample.data->right.begin() + m_start_sample, sample.data->right.end());
    }

    sample.data->left = restored_left;
    sample.data->right = restored_right;
}

} // namespace tpanar_ns
