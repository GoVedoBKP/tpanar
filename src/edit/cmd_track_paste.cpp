/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "cmd_track_paste.h"
#include "../mixer/track.h"
#include "../mixer/track_clipboard.h"
#include "../instrument/sample_instrument.h"
#include "../core/engine.h"

namespace tpanar_ns {

TrackPasteCommand::TrackPasteCommand(Track& track, Engine& engine, size_t insert_sample)
    : m_track(track), m_engine(engine), m_insert_sample(insert_sample)
{
}

void TrackPasteCommand::apply() {
    if (!m_engine.track_clipboard().has_content()) {
        return;
    }

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

    std::vector<float> clip_left, clip_right;
    m_engine.track_clipboard().get_audio(clip_left, clip_right);

    if (clip_left.empty()) {
        return;
    }

    m_inserted_count = clip_left.size();
    size_t insert_pos = m_insert_sample;
    size_t sample_size = sample.data->left.size();

    if (insert_pos > sample_size) {
        insert_pos = sample_size;
    }

    std::vector<float> new_left;
    new_left.insert(new_left.end(), sample.data->left.begin(), sample.data->left.begin() + insert_pos);
    new_left.insert(new_left.end(), clip_left.begin(), clip_left.end());
    new_left.insert(new_left.end(), sample.data->left.begin() + insert_pos, sample.data->left.end());

    std::vector<float> new_right;
    bool is_stereo = !sample.data->right.empty();
    if (is_stereo) {
        new_right.insert(new_right.end(), sample.data->right.begin(), sample.data->right.begin() + insert_pos);
        if (!clip_right.empty()) {
            new_right.insert(new_right.end(), clip_right.begin(), clip_right.end());
        } else {
            new_right.insert(new_right.end(), m_inserted_count, 0.0f);
        }
        new_right.insert(new_right.end(), sample.data->right.begin() + insert_pos, sample.data->right.end());
    }

    sample.data->left = new_left;
    sample.data->right = new_right;
}

void TrackPasteCommand::undo() {
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

    if (m_insert_sample >= sample.data->left.size()) {
        return;
    }

    size_t end_pos = m_insert_sample + m_inserted_count;
    if (end_pos > sample.data->left.size()) {
        end_pos = sample.data->left.size();
    }

    std::vector<float> restored_left;
    restored_left.insert(restored_left.end(), sample.data->left.begin(), sample.data->left.begin() + m_insert_sample);
    restored_left.insert(restored_left.end(), sample.data->left.begin() + end_pos, sample.data->left.end());

    std::vector<float> restored_right;
    bool is_stereo = !sample.data->right.empty();
    if (is_stereo) {
        restored_right.insert(restored_right.end(), sample.data->right.begin(), sample.data->right.begin() + m_insert_sample);
        restored_right.insert(restored_right.end(), sample.data->right.begin() + end_pos, sample.data->right.end());
    }

    sample.data->left = restored_left;
    sample.data->right = restored_right;
}

} // namespace tpanar_ns
