/*
 * TPanar - Digital Audio Workstation
 * Copyright (C) 2025  Miroslav Shaltev
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "engine.h"
#include "config_manager.h"
#include "../gui/theme.h"
#include "../audio/audio_backend.h"
#include "../audio/jack_backend.h"
#include "../audio/oss_backend.h"
#include "../audio/null_backend.h"
#include "../audio/timestretch.h"
#include "../sequencer/timing.h"
#include "../sequencer/pattern.h"
#include "../mixer/track.h"
#include "../instrument/sample_instrument.h"
#include "../instrument/soundfont_instrument.h"
#include "../instrument/sfz_instrument.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

namespace tpanar_ns {

namespace {

constexpr size_t kTempoTrackColumns = 0;
constexpr size_t kQuantizeNoteTrackMaxColumns = 3;

bool is_timeline_sample_track(const Engine& engine, size_t track_index)
{
    if (track_index >= engine.track_count() || engine.is_tempo_track(track_index)) return false;
    const Track& track = engine.track(track_index);
    return (track.kind() == TrackKind::Audio || track.kind() == TrackKind::Pilot) &&
           track.instrument() &&
           track.instrument()->type() == InstrumentType::Sampler;
}

size_t absolute_song_row(const Engine& engine, size_t order_pos, size_t row)
{
    size_t absolute = row;
    const auto order = engine.order_list();
    for (size_t i = 0; i < order_pos && i < order.size(); ++i) {
        absolute += engine.pattern(order[i]).row_count();
    }
    return absolute;
}

std::unique_ptr<AudioBackend> make_audio_backend(Engine *engine, AudioBackendType type,
                                                 uint32_t num_ins, uint32_t num_outs,
                                                 uint32_t num_midi_ins, uint32_t num_midi_outs)
{
    switch (type) {
        case AudioBackendType::Jack:
            return std::make_unique<JackBackend>(engine, num_ins, num_outs, num_midi_ins, num_midi_outs);
        case AudioBackendType::Oss:
            return std::make_unique<OssBackend>(engine, num_ins, num_outs, num_midi_ins, num_midi_outs);
        case AudioBackendType::Null:
            break;
    }
    return std::make_unique<NullBackend>();
}

std::string sanitize_export_stem_name(std::string name, size_t fallback_index = 0)
{
    for (char& c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            c = '_';
        }
    }
    if (name.empty()) {
        name = std::to_string(fallback_index);
    }
    return name;
}

std::shared_ptr<SampleData> resolve_audio_track_sample(const Engine& engine, size_t track_index)
{
    if (track_index >= engine.track_count()) return nullptr;

    const Track& audio_track = engine.track(track_index);
    if (!is_timeline_sample_track(engine, track_index)) return nullptr;

    if (!audio_track.instrument() || audio_track.instrument()->type() != InstrumentType::Sampler) {
        return nullptr;
    }

    const auto* sampler = static_cast<const SampleInstrument*>(audio_track.instrument());
    if (sampler->sample_count() == 0) return nullptr;

    size_t sample_index = audio_track.audio_sample_index();
    if (sample_index == 0) sample_index = 1;
    if (sample_index > sampler->sample_count()) return nullptr;

    const auto& sample = sampler->get_sample(sample_index - 1);
    return sample.data;
}

struct AnalysisBandpass {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

    void set_bandpass(float freq, float q, float sr)
    {
        float w0 = 2.0f * (float)M_PI * freq / sr;
        float alpha = std::sin(w0) / (2.0f * q);
        float cos_w0 = std::cos(w0);
        float a0 = 1.0f + alpha;
        b0 = alpha / a0;
        b1 = 0.0f;
        b2 = -alpha / a0;
        a1 = -2.0f * cos_w0 / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float process(float in)
    {
        float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = out;
        return out;
    }
};

struct DetectedHit {
    size_t absolute_row = 0;
    uint8_t volume = 0;
};

size_t total_song_rows(const Engine& engine)
{
    size_t total_rows = 0;
    for (size_t pat_idx : engine.order_list()) {
        if (pat_idx < engine.pattern_count()) total_rows += engine.pattern(pat_idx).row_count();
    }
    return total_rows;
}

bool detect_audio_track_hits(const Engine& engine, size_t audio_track_index, std::vector<DetectedHit>& hits)
{
    hits.clear();
    if (audio_track_index >= engine.track_count() || engine.is_tempo_track(audio_track_index)) return false;

    const Track& audio_track = engine.track(audio_track_index);
    if (audio_track.kind() != TrackKind::Audio) return false;

    auto sample = resolve_audio_track_sample(engine, audio_track_index);
    if (!sample || sample->left.empty()) return false;

    const DrumTypeInfo& drum = drum_type_info(audio_track.drum_type());
    const double samples_per_row = (engine.lpb() > 0 && engine.tempo() > 0.0)
        ? (((double)engine.sample_rate() * 60.0) / engine.tempo()) / (double)engine.lpb()
        : (double)engine.sample_rate();
    const double engine_rows_per_second = samples_per_row > 0.0
        ? (double)engine.sample_rate() / samples_per_row
        : 0.0;
    if (engine_rows_per_second <= 0.0) return false;

    const float sample_rate = sample->sample_rate > 0 ? (float)sample->sample_rate : (float)engine.sample_rate();
    const size_t sample_count = sample->left.size();
    const bool has_right = !sample->right.empty();

    const bool use_band = drum.min_hz > 0.0f && drum.max_hz > drum.min_hz;
    AnalysisBandpass bandpass;
    if (use_band) {
        const float center = std::sqrt(drum.min_hz * drum.max_hz);
        const float bandwidth = std::max(10.0f, drum.max_hz - drum.min_hz);
        const float q = std::clamp(center / bandwidth, 0.35f, 8.0f);
        bandpass.set_bandpass(center, q, sample_rate);
    }

    const size_t frame_size = std::clamp<size_t>((size_t)(sample_rate / 200.0f), 128, 2048);
    const size_t hop_size = std::max<size_t>(1, frame_size / 2);

    std::vector<float> energies;
    std::vector<size_t> centers;
    energies.reserve(sample_count / hop_size + 1);
    centers.reserve(sample_count / hop_size + 1);

    for (size_t start = 0; start < sample_count; start += hop_size) {
        const size_t end = std::min(sample_count, start + frame_size);
        if (end <= start) break;

        float sum_sq = 0.0f;
        for (size_t i = start; i < end; ++i) {
            float mono = sample->left[i];
            if (has_right && i < sample->right.size()) {
                mono = 0.5f * (mono + sample->right[i]);
            }
            const float filtered = use_band ? bandpass.process(mono) : mono;
            sum_sq += filtered * filtered;
        }

        energies.push_back(std::sqrt(sum_sq / (float)(end - start)));
        centers.push_back(start + (end - start) / 2);
    }

    if (energies.size() < 3) return false;

    const float peak_energy = *std::max_element(energies.begin(), energies.end());
    if (peak_energy < 1e-5f) return false;

    const bool fast_retrigger = drum.min_hz >= 2000.0f;
    const float cooldown_seconds = fast_retrigger ? 0.035f : 0.08f;
    const size_t cooldown_frames = std::max<size_t>(1, (size_t)std::lround((cooldown_seconds * sample_rate) / hop_size));
    const size_t baseline_window = 12;
    const size_t song_rows = total_song_rows(engine);

    size_t last_hit_frame = 0;
    bool have_last_hit = false;
    for (size_t i = 1; i + 1 < energies.size(); ++i) {
        float baseline = 0.0f;
        const size_t baseline_start = (i > baseline_window) ? (i - baseline_window) : 0;
        const size_t baseline_count = i - baseline_start;
        for (size_t j = baseline_start; j < i; ++j) baseline += energies[j];
        if (baseline_count > 0) baseline /= (float)baseline_count;

        const float local = energies[i];
        const float threshold = std::max(peak_energy * (fast_retrigger ? 0.08f : 0.12f),
                                         baseline * (fast_retrigger ? 1.45f : 1.75f));

        if (local < threshold) continue;
        if (local < energies[i - 1] || local < energies[i + 1]) continue;
        if (have_last_hit && i - last_hit_frame < cooldown_frames) continue;

        const double time_seconds = (double)centers[i] / sample_rate;
        const size_t absolute_row = (size_t)std::llround(time_seconds * engine_rows_per_second);
        if (absolute_row >= song_rows) continue;

        const float normalized = std::sqrt(std::clamp(local / peak_energy, 0.0f, 1.0f));
        const uint8_t volume = (uint8_t)std::clamp((int)std::lround(20.0f + normalized * 107.0f), 1, 127);
        hits.push_back({absolute_row, volume});
        last_hit_frame = i;
        have_last_hit = true;
    }

    return !hits.empty();
}

size_t sample_index_for_absolute_row(const Engine& engine,
                                     const SampleData& sample,
                                     size_t absolute_row)
{
    const double sample_rate = sample.sample_rate > 0 ? (double)sample.sample_rate : (double)engine.sample_rate();
    const double engine_samples_per_row = (engine.lpb() > 0 && engine.tempo() > 0.0)
        ? (((double)engine.sample_rate() * 60.0) / engine.tempo()) / (double)engine.lpb()
        : (double)engine.sample_rate();
    const double samples_per_row = engine_samples_per_row * (sample_rate / (double)engine.sample_rate());
    return std::min(sample.left.size(), (size_t)std::llround((double)absolute_row * samples_per_row));
}

std::vector<size_t> collect_note_rows(const Engine& engine,
                                      size_t note_track_index,
                                      size_t start_row,
                                      size_t end_row)
{
    std::vector<size_t> rows;
    if (note_track_index >= engine.track_count() || start_row >= end_row) return rows;

    size_t rows_done = 0;
    for (size_t pat_idx : engine.order_list()) {
        if (pat_idx >= engine.pattern_count()) continue;
        const Pattern& pat = engine.pattern(pat_idx);
        if (note_track_index >= pat.track_count()) {
            rows_done += pat.row_count();
            continue;
        }

        const size_t col_count = pat.column_count(note_track_index);
        for (size_t row = 0; row < pat.row_count(); ++row) {
            const size_t absolute_row = rows_done + row;
            if (absolute_row < start_row || absolute_row >= end_row) continue;

            bool has_note = false;
            for (size_t col = 0; col < col_count; ++col) {
                if (pat.event(note_track_index, row, col).note != NOTE_EMPTY) {
                    has_note = true;
                    break;
                }
            }
            if (has_note && (rows.empty() || rows.back() != absolute_row)) {
                rows.push_back(absolute_row);
            }
        }
        rows_done += pat.row_count();
    }

    return rows;
}

void fit_channel_length(std::vector<float>& channel, size_t target_length)
{
    if (channel.size() > target_length) {
        channel.resize(target_length);
    } else if (channel.size() < target_length) {
        channel.insert(channel.end(), target_length - channel.size(), 0.0f);
    }
}

bool stretch_channel_segment(const std::vector<float>& source,
                             size_t start_sample,
                             size_t end_sample,
                             size_t target_length,
                             uint32_t sample_rate,
                             std::vector<float>& output)
{
    output.clear();
    if (end_sample <= start_sample) return false;
    if (target_length == 0) return false;

    const std::vector<float> input(source.begin() + start_sample, source.begin() + end_sample);
    if (input.empty()) return false;

    if (input.size() == target_length) {
        output = input;
        return true;
    }

    const float tempo_ratio = (float)input.size() / (float)target_length;
    if (!TimeStretch::stretch(input, output, tempo_ratio, sample_rate)) {
        return false;
    }
    fit_channel_length(output, target_length);
    return true;
}

void materialize_unique_order_patterns(Engine& engine)
{
    if (engine.pattern_count() == 0) {
        engine.create_pattern();
    }

    std::unordered_set<size_t> seen;
    for (size_t order_pos = 0; order_pos < engine.m_order.size();) {
        size_t pat_idx = engine.m_order[order_pos];
        if (pat_idx >= engine.pattern_count()) {
            engine.m_order.erase(engine.m_order.begin() + order_pos);
            continue;
        }
        if (!seen.insert(pat_idx).second) {
            engine.m_order[order_pos] = engine.copy_pattern(pat_idx);
        }
        ++order_pos;
    }

    if (engine.m_order.empty()) {
        engine.m_order.push_back(0);
    }
}

void refresh_audio_track_sources(Engine& engine)
{
    for (size_t i = 0; i < engine.track_count(); ++i) {
        Track& track = engine.track(i);
        if (is_timeline_sample_track(engine, i)) {
            track.set_audio_sample_data(resolve_audio_track_sample(engine, i));
        } else {
            track.set_audio_sample_data(nullptr);
        }
    }
}

size_t audio_track_ordinal(const Engine& engine, size_t track_index)
{
    size_t ordinal = 0;
    for (size_t i = 0; i < engine.track_count() && i <= track_index; ++i) {
        const Track& track = engine.track(i);
        if (track.kind() == TrackKind::Audio) {
            if (i == track_index) return ordinal;
            ++ordinal;
        }
    }
    return ordinal;
}

} // namespace

Engine::Engine() : m_initialized(false), m_timing(44100) {
    m_transport = std::make_unique<Transport>(*this);
    // Backend is created in initialize() once the config is loaded.
    m_export_progress.store(0.0f);
    m_is_exporting.store(false);
}

Engine::~Engine() {
    shutdown();
}

bool Engine::initialize() {
    if (m_initialized) return true;
    
    ConfigManager::instance().load();
    const auto& conf = ConfigManager::instance().config();
    m_audio_backend_type = conf.audio_backend;
    m_num_ins = conf.num_audio_ins;
    m_num_outs = conf.num_audio_outs;
    m_num_midi_ins = conf.num_midi_ins;
    m_num_midi_outs = conf.num_midi_outs;
    m_gui_button_height = conf.gui_button_height;
    m_gui_font_size = conf.gui_font_size;
    m_gui_theme = conf.gui_theme;
    m_waveform_color = conf.waveform_color;
    m_bg_color = conf.bg_color;
    m_fg_color = conf.fg_color;
    m_button_color = conf.button_color;
    m_boxtype = conf.boxtype;
    m_btn_boxtype = conf.btn_boxtype;
    m_label_boxtype = conf.label_boxtype;

    m_tracker_bg = conf.tracker_bg;
    m_tracker_text = conf.tracker_text;
    m_tracker_cursor = conf.tracker_cursor;
    m_tracker_row_highlight = conf.tracker_row_highlight;
    m_tracker_lpb_highlight = conf.tracker_lpb_highlight;
    m_tracker_note = conf.tracker_note;
    m_tracker_sample = conf.tracker_sample;
    m_tracker_volume = conf.tracker_volume;
    m_tracker_effect = conf.tracker_effect;

    m_key_bindings.set_layout((KeyboardLayout)conf.keyboard_layout);
    ThemeManager::apply_theme_and_settings(*this);
    set_worker_threads(conf.num_worker_threads);

    // Create the backend with the configured port counts.
    m_backend = make_audio_backend(this, m_audio_backend_type, m_num_ins, m_num_outs, m_num_midi_ins, m_num_midi_outs);
    new_project();
    if (!m_backend->start()) {
        std::cerr << "Failed to start " << audio_backend_type_label(m_audio_backend_type)
                  << " backend, falling back to null backend." << std::endl;
        m_backend = std::make_unique<NullBackend>();
        m_backend->start();
    }
    propagate_sample_rate(m_backend->sample_rate());
    m_initialized = true;
    return true;
}

void Engine::load_config() {
    const auto& conf = ConfigManager::instance().config();
    m_audio_backend_type = conf.audio_backend;
    m_num_ins = conf.num_audio_ins;
    m_num_outs = conf.num_audio_outs;
    m_num_midi_ins = conf.num_midi_ins;
    m_num_midi_outs = conf.num_midi_outs;
    m_gui_button_height = conf.gui_button_height;
    m_gui_font_size = conf.gui_font_size;
    m_gui_theme = conf.gui_theme;
    m_waveform_color = conf.waveform_color;
    m_bg_color = conf.bg_color;
    m_fg_color = conf.fg_color;
    m_button_color = conf.button_color;
    m_boxtype = conf.boxtype;
    m_btn_boxtype = conf.btn_boxtype;
    m_label_boxtype = conf.label_boxtype;

    m_tracker_bg = conf.tracker_bg;
    m_tracker_text = conf.tracker_text;
    m_tracker_cursor = conf.tracker_cursor;
    m_tracker_row_highlight = conf.tracker_row_highlight;
    m_tracker_lpb_highlight = conf.tracker_lpb_highlight;
    m_tracker_note = conf.tracker_note;
    m_tracker_sample = conf.tracker_sample;
    m_tracker_volume = conf.tracker_volume;
    m_tracker_effect = conf.tracker_effect;
    m_record_preroll_bars = std::max(0, std::min(2, conf.record_preroll_bars));
    m_export_lead_in_bars = std::max(0, std::min(2, conf.export_lead_in_bars));

    m_key_bindings.set_layout((KeyboardLayout)conf.keyboard_layout);

    // Re-apply theme-derived accent colors
    ThemeManager::apply_theme_and_settings(*this);
}

void Engine::save_config() {
    auto& conf = ConfigManager::instance().config();
    conf.audio_backend = m_audio_backend_type;
    conf.num_audio_ins = m_num_ins;
    conf.num_audio_outs = m_num_outs;
    conf.num_midi_ins = m_num_midi_ins;
    conf.num_midi_outs = m_num_midi_outs;
    conf.gui_button_height = m_gui_button_height;
    conf.gui_font_size = m_gui_font_size;
    conf.gui_theme = m_gui_theme;
    conf.waveform_color = m_waveform_color;
    conf.bg_color = m_bg_color;
    conf.fg_color = m_fg_color;
    conf.button_color = m_button_color;
    conf.boxtype = m_boxtype;
    conf.btn_boxtype = m_btn_boxtype;
    conf.label_boxtype = m_label_boxtype;

    conf.tracker_bg = m_tracker_bg;
    conf.tracker_text = m_tracker_text;
    conf.tracker_cursor = m_tracker_cursor;
    conf.tracker_row_highlight = m_tracker_row_highlight;
    conf.tracker_lpb_highlight = m_tracker_lpb_highlight;
    conf.tracker_note = m_tracker_note;
    conf.tracker_sample = m_tracker_sample;
    conf.tracker_volume = m_tracker_volume;
    conf.tracker_effect = m_tracker_effect;
    conf.record_preroll_bars = m_record_preroll_bars;
    conf.export_lead_in_bars = m_export_lead_in_bars;

    conf.keyboard_layout = (int)m_key_bindings.get_layout();
    ConfigManager::instance().save();
}

void Engine::shutdown() {
    if (!m_initialized) return;
    save_config();
    m_backend->stop();
    m_initialized = false;
    if (!m_project_temp_dir.empty() && fs::exists(m_project_temp_dir)) {
        try { fs::remove_all(m_project_temp_dir); } catch (...) {}
    }
}

void Engine::new_project() {
    cancel_record_preroll();
    m_is_recording_audio_tracks.store(false, std::memory_order_release);
    m_audio_record_capture_enabled.store(false, std::memory_order_release);
    if (!m_project_temp_dir.empty() && fs::exists(m_project_temp_dir)) {
        try { fs::remove_all(m_project_temp_dir); } catch (...) {}
    }
    m_project_temp_dir = "";
    m_master.reset((float)m_sample_rate);

    m_tracks.clear();
    m_buses.clear();
    m_instruments.clear();
    m_patterns.clear();
    m_order.clear();
    
    // Add master bus as the first bus (index 0) - permanent, not removable
    m_buses.emplace_back();
    m_buses[0].set_name("Master");
    
    // Instrument 0 is reserved as the pilot sampler.
    add_instrument();
    set_instrument_type(0, InstrumentType::Sampler);
    instrument(0).set_name("Pilot Sampler");

    // Add the permanent tempo track followed by the permanent pilot track.
    add_track();
    configure_tempo_track(m_tracks[0]);
    add_track();
    configure_pilot_track(m_tracks[1]);
    
    // Add one default pattern
    create_pattern();
    ensure_tempo_track();
    ensure_pilot_track();
    
    // Initialize order
    m_order.push_back(0);
    m_order_start.store(0);
    m_order_end.store(0);
    
    m_active_pattern.store(0);
    m_current_row = 0;
    m_current_tick = 0;
    m_edit_order_pos.store(0);
    m_order_pos.store(0);
    set_record_track(default_record_track());

    m_project_title = "Untitled Project";
    m_project_artist = "Unknown Artist";
    m_project_album = "";
    m_project_year = "";
    m_record_preroll_bars = std::max(0, std::min(2, ConfigManager::instance().config().record_preroll_bars));
    m_export_lead_in_bars = std::max(0, std::min(2, ConfigManager::instance().config().export_lead_in_bars));
}

void Engine::reinitialize_audio(uint32_t num_ins, uint32_t num_outs, uint32_t num_midi_ins, uint32_t num_midi_outs) {
    m_backend->stop();
    m_num_ins = num_ins; m_num_outs = num_outs;
    m_num_midi_ins = num_midi_ins; m_num_midi_outs = num_midi_outs;
    
    m_backend = make_audio_backend(this, m_audio_backend_type, num_ins, num_outs, num_midi_ins, num_midi_outs);
    if (!m_backend->start()) {
        std::cerr << "Failed to restart " << audio_backend_type_label(m_audio_backend_type)
                  << " backend, falling back to null backend." << std::endl;
        m_backend = std::make_unique<NullBackend>();
        m_backend->start();
    }
    propagate_sample_rate(m_backend->sample_rate());
}

bool Engine::audio_active() const {
    return m_backend && m_backend->is_active() && m_backend->type() != AudioBackendType::Null;
}

AudioBackendType Engine::active_audio_backend_type() const
{
    return m_backend ? m_backend->type() : AudioBackendType::Null;
}

bool Engine::is_playing() const { return transport().is_playing(); }

void Engine::set_worker_threads(uint32_t n) {
    size_t count = 0;
    if (n == 255) {
        // Auto: use hardware_concurrency minus one (for the audio thread itself).
        unsigned hw = std::thread::hardware_concurrency();
        count = (hw > 1) ? hw - 1 : 0;
    } else {
        count = n;
    }

    if (!m_thread_pool) {
        m_thread_pool = std::make_unique<AudioThreadPool>(count);
    } else {
        m_thread_pool->resize(count);
    }
    ConfigManager::instance().config().num_worker_threads = n;
}

void Engine::propagate_sample_rate(uint32_t sr) {
    if (sr == 0) sr = 44100;
    m_sample_rate = sr;
    m_timing.set_sample_rate(sr);
    m_metronome.set_sample_rate((double)sr);
    for (auto& track : m_tracks) {
        track.chain().set_sample_rate((float)sr);
        track.set_engine_rate((double)sr);
    }
    for (auto& bus : m_buses) bus.chain().set_sample_rate((float)sr);
    m_master.set_sample_rate((float)sr);
    for (auto& inst : m_instruments) inst->set_sample_rate((double)sr);
}

void Engine::start() { 
    bool preroll_started = false;
    if (m_audio_record_enabled.load(std::memory_order_relaxed)) {
        start_armed_audio_recording();
        preroll_started = begin_record_preroll();
    }
    if (!preroll_started) {
        m_audio_record_capture_enabled.store(
            m_is_recording_audio_tracks.load(std::memory_order_relaxed),
            std::memory_order_release);
    }
    transport().play();
    EngineCommand cmd;
    cmd.type = EngineCommandType::Play;
    m_cmd_queue.push(cmd);
}
void Engine::stop() { 
    cancel_record_preroll();
    stop_armed_audio_recording(true);
    transport().stop();
    EngineCommand cmd;
    cmd.type = EngineCommandType::Stop;
    m_cmd_queue.push(cmd);
    panic();
}

void Engine::panic() {
    for (auto& track : m_tracks) {
        track.panic();
    }
}
void Engine::play() {
    bool preroll_started = false;
    if (m_audio_record_enabled.load(std::memory_order_relaxed)) {
        start_armed_audio_recording();
        preroll_started = begin_record_preroll();
    }
    if (!preroll_started) {
        m_audio_record_capture_enabled.store(
            m_is_recording_audio_tracks.load(std::memory_order_relaxed),
            std::memory_order_release);
    }
    transport().play();
}

void Engine::play_song() {
    stop();
    m_order_pos.store(0);
    if (!m_order.empty()) {
        set_active_pattern(m_order[0]);
    }
    m_current_row = 0;
    m_current_tick = 0;
    set_loop(false);
    auto_seek();
    start();
}

void Engine::play_pattern() {
    stop();
    m_current_row = 0;
    m_current_tick = 0;
    set_loop(true);
    auto_seek();
    start();
}

void Engine::play_from_position(size_t row) {
    stop();
    m_current_row = row;
    m_current_tick = 0;
    set_loop(true);
    auto_seek();
    start();
}

void Engine::set_play_position(size_t order_pos, size_t row) {
    bool was_playing = is_playing();
    bool was_looping = transport().m_loop_pattern.load();
    stop();
    if (order_pos < m_order.size()) {
        m_order_pos.store(order_pos);
        set_active_pattern(m_order[order_pos]);
    }
    m_current_row = row;
    m_current_tick = 0;
    if (was_playing) {
        transport().set_loop(was_looping);
        auto_seek();
        start();
    }
}

void Engine::auto_seek() {
    size_t pat_idx = m_active_pattern.load();
    if (pat_idx >= m_patterns.size()) return;
    
    Pattern& pat = *m_patterns[pat_idx];
    size_t target_row = m_current_row;

    for (size_t t = 0; t < m_tracks.size() && t < pat.track_count(); ++t) {
        if (is_timeline_sample_track(*this, t)) {
            continue;
        }
        size_t num_cols = pat.column_count(t);
        for (size_t c = 0; c < num_cols; ++c) {
            int found_row = -1;
            uint8_t found_note = 255;
            uint8_t found_vol = 255;

            for (int r = (int)target_row - 1; r >= 0; --r) {
                const auto& ev = pat.event(t, (size_t)r, c);
                if (ev.note == 254) break; 
                if (ev.note != 255) {
                    found_row = r;
                    found_note = ev.note;
                    found_vol = ev.volume;
                    break; 
                }
            }

            if (found_row != -1 && found_note != 255 && found_note != 254) {
                size_t row_diff = target_row - (size_t)found_row;
                size_t offset_samples = row_diff * m_timing.samples_per_row();
                m_tracks[t].note_on(found_note, found_vol == 255 ? 100 : found_vol, c, offset_samples);
            }
        }
    }

    refresh_audio_track_sources(*this);
    const size_t offset_samples =
        absolute_song_row(*this, m_order_pos.load(), target_row) * m_timing.samples_per_row();
    for (size_t t = 0; t < m_tracks.size(); ++t) {
        if (is_timeline_sample_track(*this, t) && m_tracks[t].has_audio_sample()) {
            m_tracks[t].start_audio_sample(offset_samples);
        }
    }
}

void Engine::preview_note(size_t t, uint8_t note, size_t column) {
    if (t < m_tracks.size() && !is_tempo_track(t) &&
        m_tracks[t].kind() != TrackKind::Audio &&
        m_tracks[t].kind() != TrackKind::Pilot) {
        if (note == 254) m_tracks[t].note_off(column);
        else m_tracks[t].note_on(note, 100, column);
    }
}

void Engine::stop_preview(size_t t, size_t column) {
    if (t < m_tracks.size() && !is_tempo_track(t) &&
        m_tracks[t].kind() != TrackKind::Audio &&
        m_tracks[t].kind() != TrackKind::Pilot) {
        m_tracks[t].note_off(column);
    }
}

void Engine::start_sample_preview(std::shared_ptr<SampleData> data, int via_track,
                                    size_t sel_start, size_t sel_end, bool loop) {
    std::lock_guard<std::mutex> lock(m_preview_mutex);
    m_preview_via_track = via_track;
    if (via_track >= 0) {
        m_preview_voice.reset();
        m_preview_playback_pos.store(-1);
    } else {
        m_preview_voice = std::make_unique<SampleVoice>(data, m_sample_rate);
        m_preview_voice->start(69, 100, 440.0f, sel_start);
        size_t data_size = data ? data->left.size() : 0;
        size_t end = (sel_end > sel_start && sel_end <= data_size) ? sel_end : 0;
        m_preview_voice->set_region(end, loop, sel_start);
    }
}

void Engine::stop_sample_preview() {
    std::lock_guard<std::mutex> lock(m_preview_mutex);
    if (m_preview_voice) {
        m_preview_voice->panic();
        m_preview_voice.reset();
    }
    m_preview_playback_pos.store(-1);
    m_preview_via_track = -1;
}

void Engine::record_note(uint8_t note, size_t column)
{
    size_t row = current_row();
    Pattern& current_pattern = pattern();
    if (m_record_track < current_pattern.track_count() && !is_tempo_track(m_record_track)) {
        current_pattern.event(m_record_track, row, column).note = note;
    }
}

void Engine::process_tick() 
{ 
    size_t pat_idx = m_active_pattern.load();
    if (pat_idx >= m_patterns.size()) return;
    Pattern& pat = *m_patterns[pat_idx];

    if (m_current_tick == 0) {
        size_t row = m_current_row;
        for (size_t t = 0; t < m_tracks.size() && t < pat.track_count(); ++t) {
            size_t num_cols = pat.column_count(t);
            if (num_cols == 0) {
                handle_effect_row_start(t, pat.event(t, row, 0));
            }
            if (is_timeline_sample_track(*this, t)) {
                if (num_cols > 0) {
                    handle_effect_row_start(t, pat.event(t, row, 0));
                }
                continue;
            }
            for (size_t c = 0; c < num_cols; ++c) {
                TrackEvent& ev = pat.event(t, row, c);
                if (ev.note == 254) {
                    m_tracks[t].note_off(c);
                } else if (ev.note != 255) {
                    m_tracks[t].schedule_note_on(ev.note, ev.volume == 255 ? 100 : ev.volume,
                                                 c, ev.sample_idx,
                                                 m_timing.samples_per_row());
                }
                if (c == 0) handle_effect_row_start(t, ev);
            }
        }
    }

    for (size_t t = 0; t < m_tracks.size(); ++t) {
        m_tracks[t].process_tick(m_current_tick);
    }

    m_current_tick++;
    if (m_current_tick >= m_timing.speed()) {
        m_current_tick = 0;
        m_current_row++;
        if (m_current_row >= pat.row_count()) {
            m_current_row = 0;
            bool restart_audio_tracks = false;
            size_t restart_order_pos = m_order_pos.load();
            if (!transport().m_loop_pattern.load()) {
                size_t next_order_pos = m_order_pos.load() + 1;
                size_t end_pos = m_order_end.load();
                if (end_pos == 0 && !m_order.empty()) end_pos = m_order.size() - 1;

                if (next_order_pos > end_pos) {
                    if (m_is_exporting.load()) {
                        transport().stop();
                        return;
                    }
                    next_order_pos = m_order_start.load();
                    restart_audio_tracks = true;
                }
                
                if (next_order_pos < m_order.size()) {
                    m_order_pos.store(next_order_pos);
                    set_active_pattern(m_order[next_order_pos]);
                    restart_order_pos = next_order_pos;
                }
            } else {
                restart_audio_tracks = true;
            }

            if (restart_audio_tracks) {
                refresh_audio_track_sources(*this);
                const size_t offset_samples =
                    absolute_song_row(*this, restart_order_pos, m_current_row) * m_timing.samples_per_row();
                for (size_t t = 0; t < m_tracks.size(); ++t) {
                    if (is_timeline_sample_track(*this, t) && m_tracks[t].has_audio_sample()) {
                        m_tracks[t].start_audio_sample(offset_samples);
                    }
                }
            }
        }
    }
}

void Engine::handle_effect_row_start(size_t t, const TrackEvent& ev)
{
    if (is_tempo_track(t)) {
        if (ev.param1 > 0) {
            m_timing.set_bpm(ev.param1);
        }
        return;
    }

    auto& track = m_tracks[t];
    auto process_fx = [&](uint8_t fx, uint8_t param) {
        if (fx == 0) return;
        switch (fx) {
            case 0x0F: if (param < 32) m_timing.set_speed(param); else m_timing.set_bpm(param); break;
            case 0x0C: track.set_volume(param / 64.f); break;
            case 0x0A: track.m_fx_state.vol_slide_up = (param >> 4); track.m_fx_state.vol_slide_down = (param & 0x0F); break;
            case 0x03: track.m_fx_state.porta_speed = param * 0.0005f; track.m_fx_state.porta_active = true; break;
            case 0x0E: { uint8_t sub = param >> 4; uint8_t val = param & 0x0F; if (sub == 0x9) { track.m_fx_state.retrig_ticks = val; track.m_fx_state.retrig_counter = 0; } else if (sub == 0xC) track.m_fx_state.note_cut_tick = val; break; }
        }
    };
    process_fx(ev.effect1, ev.param1);
    process_fx(ev.effect2, ev.param2);
}

void Engine::process_audio(const float* const* in_bufs, uint32_t num_ins, float** out_bufs, uint32_t num_outs, size_t nframes)
{
    MidiMessage msg;
    while (m_midi_queue.pop(msg)) {
        uint8_t status = msg.status & 0xF0;
        if (status == 0x90 || status == 0x80) {
            if (!m_tracks.empty() && m_record_track < m_tracks.size() && !is_tempo_track(m_record_track)) {
                m_tracks[m_record_track].note_on(msg.data1, msg.data2);
                if (m_record_enabled && transport().is_playing()) record_note(msg.data1);
            }
        }
    }

    process_commands();

    size_t processed = 0;
    while (processed < nframes) {
        size_t block = nframes - processed;
        if (transport().is_playing()) {
            if (m_samples_until_next_tick == 0) {
                if (!m_record_preroll_active.load(std::memory_order_acquire)) {
                    process_tick();
                }
                m_samples_until_next_tick = m_timing.samples_per_tick();
            }
            block = std::min(block, m_samples_until_next_tick);
        }
        block = std::min(block, MAX_BLOCK);

        const float* offset_in_bufs[64];
        for (uint32_t i = 0; i < num_ins && i < 64; ++i)
            offset_in_bufs[i] = in_bufs ? in_bufs[i] + processed : nullptr;

        float* offset_out_bufs[64];
        for (uint32_t i = 0; i < num_outs && i < 64; ++i)
            offset_out_bufs[i] = out_bufs[i] + processed;

        render_block_multi(offset_out_bufs, num_outs, block, in_bufs ? offset_in_bufs : nullptr);
        if (transport().is_playing()) {
            advance_record_preroll(block);
        }
        processed += block;
        if (transport().is_playing())
            m_samples_until_next_tick -= block;
    }

    // Master bus processing on hardware outputs 1-2 (out_bufs[0] and out_bufs[1])
    if (num_outs >= 2 && out_bufs[0] && out_bufs[1]) {
        m_master.process(out_bufs[0], out_bufs[1], nframes);
    }

    if (m_is_exporting.load()) {
        for (uint32_t j = 0; j < num_outs; ++j) {
            if (out_bufs[j]) {
                for (size_t i = 0; i < nframes; ++i) { out_bufs[j][i] = 0.f; }
            }
        }
    }

    if (num_outs >= 2 && out_bufs[0] && out_bufs[1]) {
        for (size_t i = 0; i < nframes; ++i) m_spectral_rb.push((out_bufs[0][i] + out_bufs[1][i]) * 0.5f);
    }

    if ((m_metronome_enabled || m_record_preroll_active.load(std::memory_order_acquire)) &&
        transport().state() != TransportState::Stopped && !m_is_exporting.load()) {
        if (num_outs >= 2 && out_bufs[0] && out_bufs[1]) {
            float met_l[MAX_BLOCK], met_r[MAX_BLOCK];
            size_t generated = 0;
            while (generated < nframes) {
                const size_t block = std::min(MAX_BLOCK, nframes - generated);
                for (size_t i = 0; i < block; ++i) {
                    met_l[i] = 0.f;
                    met_r[i] = 0.f;
                }
                m_metronome.process(met_l, met_r, block, m_samples_until_next_beat, m_timing.samples_per_beat());
                for (size_t i = 0; i < block; ++i) {
                    out_bufs[0][generated + i] += met_l[i];
                    out_bufs[1][generated + i] += met_r[i];
                }
                generated += block;
            }
        }
    }
}

void Engine::process_audio(const float* const* in_bufs, uint32_t num_ins, float* out_l, float* out_r, size_t nframes)
{
    float* out_bufs[2] = { out_l, out_r };
    process_audio(in_bufs, num_ins, out_bufs, 2, nframes);
}

void Engine::process_block(float* l, float* r, size_t nframes, const float* const* in_bufs) {
    size_t processed = 0;
    while (processed < nframes) {
        if (m_samples_until_next_tick == 0) {
            process_tick();
            m_samples_until_next_tick = m_timing.samples_per_tick();
        }
        size_t block = std::min(m_samples_until_next_tick, nframes - processed);
        block = std::min(block, MAX_BLOCK);
        
        const float* offset_in_bufs[64];
        for(uint32_t i = 0; i < m_num_ins && i < 64; ++i) {
            offset_in_bufs[i] = in_bufs ? in_bufs[i] + processed : nullptr;
        }

        render_block(l + processed, r + processed, block, in_bufs ? offset_in_bufs : nullptr);
        processed += block; m_samples_until_next_tick -= block;
    }
    m_master.process(l, r, nframes);
}

void Engine::render_block_multi(float** out_bufs, uint32_t num_outs, size_t frames, const float* const* in_bufs) {
    if (m_is_recording_audio_tracks.load(std::memory_order_acquire) &&
        m_audio_record_capture_enabled.load(std::memory_order_acquire) && in_bufs) {
        for (size_t t = 0; t < m_tracks.size(); ++t) {
            Track& track = m_tracks[t];
            if (track.kind() == TrackKind::Audio && track.record_armed()) {
                track.capture_audio_input(in_bufs, m_num_ins, frames);
            }
        }
    }

    if (m_is_recording_sample.load()) {
        bool should_record = false;
        if (m_recording_sample_mode.load() == SampleRecordMode::Free) {
            should_record = true;
        } else {
            // Detect pattern start: tick 0 of row 0, at the very first sample of that tick.
            if (m_current_row == 0 && m_current_tick == 0 && m_samples_until_next_tick == m_timing.samples_per_tick()) {
                if (!m_recording_synced_active.load()) {
                    // First pattern start: begin recording
                    m_recording_synced_active.store(true);
                    m_recording_loop_count.store(0);
                } else {
                    // Subsequent pattern starts: count a new loop
                    m_recording_loop_count.fetch_add(1);
                }
            }
            if (m_recording_synced_active.load()) {
                should_record = true;
            }
        }

        // Always expose the current row so the GUI can show progress/countdown.
        m_recording_synced_row.store(m_current_row);

        if (should_record && in_bufs) {
            SampleData* sd = m_recording_sample_ptr.load(std::memory_order_acquire);
            if (sd) {
                size_t wp = m_recording_write_pos.load(std::memory_order_relaxed);
                size_t cap = sd->left.size();
                uint32_t ch = m_recording_input_channel;

                if (m_recording_is_mono) {
                    if (ch < m_num_ins && in_bufs[ch] && wp < cap) {
                        size_t to_write = std::min(frames, cap - wp);
                        for (size_t i = 0; i < to_write; ++i)
                            sd->left[wp + i] = in_bufs[ch][i];
                        m_recording_write_pos.fetch_add(to_write, std::memory_order_release);
                    }
                } else {
                    if (ch < m_num_ins - 1 && in_bufs[ch] && in_bufs[ch+1] && wp < cap) {
                        size_t to_write = std::min(frames, cap - wp);
                        for (size_t i = 0; i < to_write; ++i) {
                            sd->left[wp + i] = in_bufs[ch][i];
                            sd->right[wp + i] = in_bufs[ch+1][i];
                        }
                        m_recording_write_pos.fetch_add(to_write, std::memory_order_release);
                    } else if (ch < m_num_ins && in_bufs[ch] && wp < cap) {
                        size_t to_write = std::min(frames, cap - wp);
                        for (size_t i = 0; i < to_write; ++i) {
                            sd->left[wp + i] = in_bufs[ch][i];
                            sd->right[wp + i] = in_bufs[ch][i];
                        }
                        m_recording_write_pos.fetch_add(to_write, std::memory_order_release);
                    }
                }
            }
        }
    }

    // Update per-channel input peak levels for GUI meters.
    if (in_bufs) {
        for (uint32_t ch = 0; ch < m_num_ins && ch < MAX_INS; ++ch) {
            float peak = 0.0f;
            if (in_bufs[ch]) {
                for (size_t i = 0; i < frames; ++i)
                    peak = std::max(peak, std::abs(in_bufs[ch][i]));
            }
            float prev = m_input_levels[ch].load(std::memory_order_relaxed);
            // Fast attack, ~1.5 s decay at 48 kHz / 256 frames (≈187 blocks/s).
            float next = (peak > prev) ? peak : prev * 0.9985f;
            m_input_levels[ch].store(next, std::memory_order_relaxed);
        }
    }

    // Initialize hardware outputs
    for (uint32_t j = 0; j < num_outs; ++j) {
        if (out_bufs[j]) std::fill(out_bufs[j], out_bufs[j] + frames, 0.f);
    }
    
    // Initialize bus buffers
    for (size_t b = 0; b < m_buses.size() && b < MAX_BUSES_INTERNAL; ++b) {
        std::fill(m_bus_l[b], m_bus_l[b] + frames, 0.f);
        std::fill(m_bus_r[b], m_bus_r[b] + frames, 0.f);
    }

    bool any_solo = false;
    for (size_t t = 0; t < m_tracks.size(); ++t) {
        if (m_tracks[t].solo()) { any_solo = true; break; }
    }

    // Cap to the number of pre-allocated per-track buffers.
    const size_t num_tracks = std::min(m_tracks.size(), MAX_TRACKS_INTERNAL);

    // Pass 1: fire notes and render all tracks. Track buffers (m_track_l/r[t])
    // are independent — parallelised when a thread pool is available.
    // Muted (or non-solo) tracks skip audio rendering to save CPU, but still
    // drain their pending note queue so events don't back up.
    auto process_track = [&](size_t t) {
        m_tracks[t].fire_pending_notes(frames);
        bool audible = any_solo ? m_tracks[t].solo() : !m_tracks[t].muted();
        if (audible) {
            m_tracks[t].process(m_track_l[t], m_track_r[t], frames, in_bufs);
        } else {
            std::fill(m_track_l[t], m_track_l[t] + frames, 0.f);
            std::fill(m_track_r[t], m_track_r[t] + frames, 0.f);
        }
    };

    if (m_thread_pool && m_thread_pool->size() > 0) {
        m_thread_pool->parallel_for(num_tracks, process_track);
    } else {
        for (size_t t = 0; t < num_tracks; ++t)
            process_track(t);
    }

    // Pass 1b: accumulate rendered track audio into buses (must be serial).
    for (size_t t = 0; t < num_tracks; ++t) {
        bool should_play = true;
        if (any_solo) {
            if (!m_tracks[t].solo()) should_play = false;
        } else {
            if (m_tracks[t].muted()) should_play = false;
        }

        if (should_play) {
            int out_idx = m_tracks[t].output_bus();
            if (out_idx >= 0 && (size_t)out_idx < m_buses.size() && (size_t)out_idx < MAX_BUSES_INTERNAL) {
                for (size_t i = 0; i < frames; ++i) {
                    m_bus_l[out_idx][i] += m_track_l[t][i];
                    m_bus_r[out_idx][i] += m_track_r[t][i];
                }
            } else if (out_idx == MixerBus::ROUTE_MASTER) {
                // To Master output (Hardware 1-2)
                if (num_outs >= 2 && out_bufs[0] && out_bufs[1]) {
                    for (size_t i = 0; i < frames; ++i) {
                        out_bufs[0][i] += m_track_l[t][i];
                        out_bufs[1][i] += m_track_r[t][i];
                    }
                }
            } else if (out_idx <= MixerBus::ROUTE_HW_STEREO_BASE && out_idx > MixerBus::ROUTE_HW_MONO_BASE) {
                int pair = MixerBus::ROUTE_HW_STEREO_BASE - out_idx;
                uint32_t ch_l = pair * 2;
                uint32_t ch_r = pair * 2 + 1;
                if (ch_r < num_outs && out_bufs[ch_l] && out_bufs[ch_r]) {
                    for (size_t i = 0; i < frames; ++i) {
                        out_bufs[ch_l][i] += m_track_l[t][i];
                        out_bufs[ch_r][i] += m_track_r[t][i];
                    }
                }
            } else if (out_idx <= MixerBus::ROUTE_HW_MONO_BASE) {
                uint32_t ch = MixerBus::ROUTE_HW_MONO_BASE - out_idx;
                if (ch < num_outs && out_bufs[ch]) {
                    for (size_t i = 0; i < frames; ++i) {
                        out_bufs[ch][i] += (m_track_l[t][i] + m_track_r[t][i]) * 0.5f;
                    }
                }
            }
        }
    }

    // Pass 2: Buses to other Buses or Hardware
    // We process buses in reverse order to allow simple hierarchical routing (higher index buses to lower index buses)
    // Bus 0 is Master Bus, but it's used as a regular bus in this loop if needed.
    for (int b = (int)m_buses.size() - 1; b >= 0; --b) {
        m_buses[b].process(m_bus_l[b], m_bus_r[b], frames);
        
        int out_idx = m_buses[b].output_bus();
        if (out_idx >= 0 && out_idx < b && out_idx < MAX_BUSES_INTERNAL) {
            // Route to a lower-indexed bus
            for (size_t i = 0; i < frames; ++i) {
                m_bus_l[out_idx][i] += m_bus_l[b][i];
                m_bus_r[out_idx][i] += m_bus_r[b][i];
            }
        } else if (out_idx == MixerBus::ROUTE_MASTER) {
            // Route to Master output (Hardware 1-2)
            if (num_outs >= 2 && out_bufs[0] && out_bufs[1]) {
                for (size_t i = 0; i < frames; ++i) {
                    out_bufs[0][i] += m_bus_l[b][i];
                    out_bufs[1][i] += m_bus_r[b][i];
                }
            }
        } else if (out_idx <= MixerBus::ROUTE_HW_STEREO_BASE && out_idx > MixerBus::ROUTE_HW_MONO_BASE) {
            int pair = MixerBus::ROUTE_HW_STEREO_BASE - out_idx;
            uint32_t ch_l = pair * 2;
            uint32_t ch_r = pair * 2 + 1;
            if (ch_r < num_outs && out_bufs[ch_l] && out_bufs[ch_r]) {
                for (size_t i = 0; i < frames; ++i) {
                    out_bufs[ch_l][i] += m_bus_l[b][i];
                    out_bufs[ch_r][i] += m_bus_r[b][i];
                }
            }
        } else if (out_idx <= MixerBus::ROUTE_HW_MONO_BASE) {
            uint32_t ch = MixerBus::ROUTE_HW_MONO_BASE - out_idx;
            if (ch < num_outs && out_bufs[ch]) {
                for (size_t i = 0; i < frames; ++i) {
                    out_bufs[ch][i] += (m_bus_l[b][i] + m_bus_r[b][i]) * 0.5f;
                }
            }
        } else if (out_idx >= b) {
            // Potential feedback loop, route to master instead or just drop
            if (num_outs >= 2 && out_bufs[0] && out_bufs[1]) {
                for (size_t i = 0; i < frames; ++i) {
                    out_bufs[0][i] += m_bus_l[b][i];
                    out_bufs[1][i] += m_bus_r[b][i];
                }
            }
        }
    }

    // Standalone preview voice (bypasses track/DSP)
    {
        std::unique_lock<std::mutex> lock(m_preview_mutex, std::try_to_lock);
        if (lock.owns_lock()) {
            if (m_preview_voice && m_preview_voice->active()) {
                float pv_l[MAX_BLOCK] = {}, pv_r[MAX_BLOCK] = {};
                m_preview_voice->process(pv_l, pv_r, frames);
                m_preview_playback_pos.store((int64_t)m_preview_voice->position());
                if (num_outs >= 2 && out_bufs[0] && out_bufs[1]) {
                    for (size_t i = 0; i < frames; ++i) {
                        out_bufs[0][i] += pv_l[i];
                        out_bufs[1][i] += pv_r[i];
                    }
                }
            } else if (m_preview_voice && !m_preview_voice->active()) {
                m_preview_playback_pos.store(-1);
            } else if (m_preview_via_track >= 0 && m_preview_via_track < (int)m_tracks.size()) {
                auto* inst = m_tracks[m_preview_via_track].instrument();
                if (inst && inst->type() == InstrumentType::Sampler) {
                    double pos = static_cast<SampleInstrument*>(inst)->voice_position();
                    m_preview_playback_pos.store(pos >= 0 ? (int64_t)pos : -1);
                }
            }
        }
    }
}

void Engine::render_block(float* out_l, float* out_r, size_t frames, const float* const* in_bufs) {
    float* out_bufs[2] = { out_l, out_r };
    render_block_multi(out_bufs, 2, frames, in_bufs);
}

void Engine::handle_midi(uint8_t* data, size_t size) {
    if (size < 3) return;
    MidiMessage msg; msg.status = data[0]; msg.data1 = data[1]; msg.data2 = data[2];
    m_midi_queue.push(msg);
}

void Engine::set_instrument_type(size_t index, InstrumentType type) {
    if (index >= m_instruments.size()) return;
    if (m_instruments[index]->type() == type) return;
    std::string name = m_instruments[index]->name();
    std::unique_ptr<Instrument> new_inst;
    InstrumentType effective_type = type;
    switch (type) {
        case InstrumentType::Sampler: new_inst = std::make_unique<SampleInstrument>((double)m_sample_rate); break;
        case InstrumentType::SoundFont: new_inst = std::make_unique<SoundFontInstrument>((double)m_sample_rate); break;
        case InstrumentType::SFZ:       new_inst = std::make_unique<SfzInstrument>((double)m_sample_rate); break;
        default:
            new_inst = std::make_unique<NoneInstrument>();
            effective_type = InstrumentType::None;
            break;
    }
    new_inst->set_name(name); new_inst->set_type(effective_type);
    for (auto& track : m_tracks) if (track.instrument() == m_instruments[index].get()) track.set_instrument(new_inst.get());
    m_instruments[index] = std::move(new_inst);
}

void Engine::add_track() { 
    m_tracks.emplace_back(); 
    for (auto& pat : m_patterns) {
        pat->resize_tracks(m_tracks.size());
    }
    if (m_tracks.size() == 1) {
        configure_tempo_track(m_tracks[0]);
    }
    set_record_track(default_record_track());
    mark_dirty();
}
void Engine::remove_track(size_t index) {
    if (index > TEMPO_TRACK_INDEX && index < m_tracks.size()) {
        m_tracks.erase(m_tracks.begin() + index);
        for (auto& pat : m_patterns) {
            pat->remove_track(index);
        }
        for (auto& track : m_tracks) {
            const int linked = track.linked_track();
            if (linked == (int)index) {
                track.set_linked_track(-1);
            } else if (linked > (int)index) {
                track.set_linked_track(linked - 1);
            }
        }
        set_record_track(default_record_track());
        mark_dirty();
    }
}
void Engine::move_track(size_t from, size_t to) {
    if (from > TEMPO_TRACK_INDEX && to > TEMPO_TRACK_INDEX && from < m_tracks.size() && to < m_tracks.size()) {
        Track moved = std::move(m_tracks[from]);
        m_tracks.erase(m_tracks.begin() + from);
        m_tracks.insert(m_tracks.begin() + to, std::move(moved));
        for (auto& pat : m_patterns) {
            pat->move_track(from, to);
        }
        for (auto& track : m_tracks) {
            const int linked = track.linked_track();
            if (linked == (int)from) {
                track.set_linked_track((int)to);
            } else if (from < to && linked > (int)from && linked <= (int)to) {
                track.set_linked_track(linked - 1);
            } else if (to < from && linked >= (int)to && linked < (int)from) {
                track.set_linked_track(linked + 1);
            }
        }
        set_record_track(default_record_track());
        mark_dirty();
    }
}
size_t Engine::bus_count() const { return m_buses.size(); }
MixerBus& Engine::bus(size_t index) { return m_buses[index]; }
const MixerBus& Engine::bus(size_t index) const { return m_buses[index]; }
void Engine::add_bus() { m_buses.emplace_back(); mark_dirty(); }
void Engine::remove_bus(size_t index) { 
    // Prevent removal of master bus (index 0)
    if (index > 0 && index < m_buses.size()) {
        m_buses.erase(m_buses.begin() + index);
        mark_dirty();
    }
}
void Engine::move_bus(size_t from, size_t to) {
    // Prevent moving master bus (index 0)
    if (from > 0 && to > 0 && from < m_buses.size() && to < m_buses.size()) {
        std::swap(m_buses[from], m_buses[to]);
    }
}

size_t Engine::track_count() const { return m_tracks.size(); }
Track& Engine::track(size_t index) { return m_tracks[index]; }
const Track& Engine::track(size_t index) const { return m_tracks[index]; }
bool Engine::is_tempo_track(size_t index) const { return index == TEMPO_TRACK_INDEX && index < m_tracks.size(); }

void Engine::configure_tempo_track(Track& track) {
    track.set_name("Tempo");
    track.set_kind(TrackKind::Pilot);
    track.set_linked_track(-1);
    track.set_audio_sample_index(0);
    track.set_audio_sample_data(nullptr);
    track.set_instrument(nullptr);
    track.set_output_bus(MixerBus::ROUTE_MASTER);
    track.set_record_armed(false);
    track.set_humanize_vel(0);
    track.set_humanize_timing(0);
}

void Engine::configure_pilot_track(Track& track) {
    if (m_instruments.empty() || m_instruments[0]->type() != InstrumentType::Sampler) {
        auto sampler = std::make_unique<SampleInstrument>((double)m_sample_rate);
        sampler->set_type(InstrumentType::Sampler);
        sampler->set_name("Pilot Sampler");
        m_instruments.insert(m_instruments.begin(), std::move(sampler));
    }
    track.set_name("Pilot");
    track.set_kind(TrackKind::Pilot);
    track.set_linked_track(-1);
    if (track.audio_sample_index() == 0) {
        track.set_audio_sample_index(1);
    }
    track.set_audio_sample_data(nullptr);
    track.set_instrument(m_instruments[0].get());
    track.set_output_bus(MixerBus::ROUTE_MASTER);
    track.set_record_armed(false);
    track.set_humanize_vel(0);
    track.set_humanize_timing(0);
}

size_t Engine::default_record_track() const {
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        if (!is_tempo_track(i)) return i;
    }
    return 0;
}

void Engine::ensure_tempo_track() {
    bool has_tempo_track = !m_tracks.empty();
    if (has_tempo_track) {
        has_tempo_track = m_tracks[0].kind() == TrackKind::Pilot && m_tracks[0].instrument() == nullptr;
        for (const auto& pat : m_patterns) {
            if (pat && pat->track_count() > 0 && pat->column_count(0) != kTempoTrackColumns) {
                has_tempo_track = false;
                break;
            }
        }
    }

    if (!has_tempo_track) {
        m_tracks.emplace(m_tracks.begin());
        configure_tempo_track(m_tracks[0]);

        for (size_t i = 1; i < m_tracks.size(); ++i) {
            const int linked = m_tracks[i].linked_track();
            if (linked >= 0) {
                m_tracks[i].set_linked_track(linked + 1);
            }
        }
        for (auto& pat : m_patterns) {
            if (pat) pat->insert_track(0, kTempoTrackColumns);
        }
    } else {
        configure_tempo_track(m_tracks[0]);
        for (auto& pat : m_patterns) {
            if (pat && pat->track_count() > 0) pat->set_column_count(0, kTempoTrackColumns);
        }
    }

    refresh_audio_track_sources(*this);
    set_record_track(default_record_track());
}

void Engine::ensure_pilot_track() {
    if (m_instruments.empty() || m_instruments[0]->type() != InstrumentType::Sampler) {
        auto sampler = std::make_unique<SampleInstrument>((double)m_sample_rate);
        sampler->set_type(InstrumentType::Sampler);
        sampler->set_name("Pilot Sampler");
        m_instruments.insert(m_instruments.begin(), std::move(sampler));
    }

    bool has_pilot_track = m_tracks.size() > 1 &&
                           m_tracks[1].kind() == TrackKind::Pilot &&
                           m_tracks[1].instrument() == m_instruments[0].get();

    if (!has_pilot_track) {
        m_tracks.emplace(m_tracks.begin() + std::min<size_t>(1, m_tracks.size()));
        configure_pilot_track(m_tracks[1]);

        for (size_t i = 0; i < m_tracks.size(); ++i) {
            if (i == 1) continue;
            const int linked = m_tracks[i].linked_track();
            if (linked >= 1) {
                m_tracks[i].set_linked_track(linked + 1);
            }
        }
        for (auto& pat : m_patterns) {
            if (pat) pat->insert_track(1, 1);
        }
    } else {
        configure_pilot_track(m_tracks[1]);
    }

    refresh_audio_track_sources(*this);
    set_record_track(default_record_track());
}

void Engine::add_instrument() { m_instruments.push_back(std::make_unique<NoneInstrument>()); mark_dirty(); }
void Engine::add_instrument(::std::unique_ptr<tpanar_ns::Instrument> inst) { m_instruments.push_back(std::move(inst)); mark_dirty(); }
void Engine::remove_instrument(size_t index) { if (index < m_instruments.size()) { m_instruments.erase(m_instruments.begin() + index); mark_dirty(); } }
Instrument& Engine::instrument(size_t index) { return *m_instruments[index]; }
const Instrument& Engine::instrument(size_t index) const { return *m_instruments[index]; }
size_t Engine::instrument_count() const { return m_instruments.size(); }
int Engine::get_instrument_index(const Instrument* inst) const {
    for (size_t i = 0; i < m_instruments.size(); ++i) if (m_instruments[i].get() == inst) return (int)i;
    return -1;
}

size_t Engine::current_row() const { return m_current_row; }
void Engine::set_active_pattern(size_t index) { if (index < m_patterns.size()) m_active_pattern.store(index); }
size_t Engine::active_pattern() const { return m_active_pattern.load(); }
Pattern& Engine::pattern() { return *m_patterns[m_active_pattern.load()]; }
Pattern& Engine::pattern(size_t index) { return *m_patterns[index]; }
const Pattern& Engine::pattern(size_t index) const { return *m_patterns[index]; }
size_t Engine::pattern_count() const { return m_patterns.size(); }

size_t Engine::create_pattern() {
    size_t rows = 64;
    if (!m_patterns.empty()) rows = pattern().row_count();
    m_patterns.push_back(std::make_unique<Pattern>(rows, m_tracks.size()));
    Pattern& p = *m_patterns.back();
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        p.set_column_count(i, is_tempo_track(i) ? kTempoTrackColumns : 1);
    }
    return m_patterns.size() - 1;
}

size_t Engine::copy_pattern(size_t index) {
    if (index >= m_patterns.size()) return create_pattern();
    m_patterns.push_back(std::make_unique<Pattern>(*m_patterns[index]));
    return m_patterns.size() - 1;
}

void Engine::resize_pattern(size_t index, size_t new_rows) {
    EngineCommand cmd;
    cmd.type = EngineCommandType::ResizePattern;
    cmd.index = index;
    cmd.value = (float)new_rows;
    m_cmd_queue.push(cmd);
    process_commands();
}

void Engine::process_commands() {
    EngineCommand cmd;
    while (m_cmd_queue.pop(cmd)) {
        switch (cmd.type) {
            case EngineCommandType::Play: transport().play(); break;
            case EngineCommandType::Stop: transport().stop(); break;
            case EngineCommandType::SetTempo: m_timing.set_bpm((int)cmd.value); break;
            case EngineCommandType::PlayPattern:
                m_current_row = 0;
                m_current_tick = 0;
                transport().set_loop(true);
                auto_seek();
                transport().play();
                break;
            case EngineCommandType::ResizePattern:
                if (cmd.index < m_patterns.size()) {
                    m_patterns[cmd.index]->resize_rows((size_t)cmd.value);
                    if (m_active_pattern.load() == cmd.index) {
                        if (m_current_row >= m_patterns[cmd.index]->row_count()) {
                            m_current_row = 0;
                        }
                    }
                }
                break;
        }
    }
}

std::vector<size_t> Engine::order_list() const {
    return m_order;
}
void Engine::set_order(const std::vector<size_t>& o) {
    m_order = o;
    materialize_unique_order_patterns(*this);

    const size_t last_order = m_order.empty() ? 0 : (m_order.size() - 1);
    m_order_pos.store(std::min(m_order_pos.load(), last_order));
    m_edit_order_pos.store(std::min(m_edit_order_pos.load(), last_order));
    if (!m_order.empty()) {
        const size_t edit_pos = std::min(m_edit_order_pos.load(), last_order);
        set_active_pattern(m_order[edit_pos]);
    }
    mark_dirty();
}
size_t Engine::add_pattern_to_order() { 
    size_t new_pat = create_pattern();
    m_order.push_back(new_pat);
    mark_dirty();
    return m_order.size() - 1; 
}
void Engine::remove_pattern_from_order(size_t pos) {
    if (pos >= m_order.size() || m_order.size() <= 1) {
        return;
    }

    erase_pattern(m_order[pos]);
    mark_dirty();
}
size_t Engine::copy_pattern_in_order(size_t pos) {
    if (pos < m_order.size()) { 
        size_t new_pat = copy_pattern(m_order[pos]);
        m_order.insert(m_order.begin() + pos + 1, new_pat); 
        mark_dirty();
        return pos + 1; 
    }
    return pos;
}

void Engine::erase_pattern(size_t pattern_index)
{
    if (pattern_index >= m_patterns.size()) {
        return;
    }

    m_patterns.erase(m_patterns.begin() + pattern_index);

    for (auto it = m_order.begin(); it != m_order.end();) {
        if (*it == pattern_index) {
            it = m_order.erase(it);
            continue;
        }
        if (*it > pattern_index) {
            --(*it);
        }
        ++it;
    }

    if (m_patterns.empty()) {
        create_pattern();
    }
    if (m_order.empty()) {
        m_order.push_back(0);
    }

    const size_t last_pattern = m_patterns.empty() ? 0 : (m_patterns.size() - 1);
    const size_t last_order = m_order.empty() ? 0 : (m_order.size() - 1);
    const size_t active = m_active_pattern.load();
    if (active > pattern_index) {
        m_active_pattern.store(active - 1);
    } else if (active == pattern_index) {
        m_active_pattern.store(std::min(pattern_index, last_pattern));
    }

    m_order_pos.store(std::min(m_order_pos.load(), last_order));
    m_edit_order_pos.store(std::min(m_edit_order_pos.load(), last_order));
    set_active_pattern(m_order[m_edit_order_pos.load()]);
}

double Engine::tempo() const { return m_timing.tempo(); }
void Engine::set_tempo(double bpm) { m_timing.set_bpm((int)bpm); mark_dirty(); }
uint32_t Engine::lpb() const { return m_timing.lpb(); }
void Engine::set_lpb(uint32_t l) { m_timing.set_lpb(l); mark_dirty(); }

void Engine::set_record_track(size_t t) {
    if (m_tracks.empty()) {
        m_record_track = 0;
        return;
    }
    if (t >= m_tracks.size() || is_tempo_track(t)) {
        m_record_track = default_record_track();
        return;
    }
    m_record_track = t;
}

bool Engine::has_armed_audio_tracks() const
{
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].kind() == TrackKind::Audio && m_tracks[i].record_armed()) {
            return true;
        }
    }
    return false;
}

bool Engine::is_audio_track_armed(size_t track_index) const
{
    return track_index < m_tracks.size() &&
           m_tracks[track_index].kind() == TrackKind::Audio &&
           m_tracks[track_index].record_armed();
}

void Engine::assign_default_audio_input(Track& track, size_t track_index)
{
    int in_l = -1, in_r = -1;
    track.get_audio_input(in_l, in_r);
    if (in_l >= 0 || in_r >= 0 || m_num_ins == 0) return;
    const size_t ordinal = audio_track_ordinal(*this, track_index);
    track.set_audio_input((int)(ordinal % m_num_ins), -1);
}

void Engine::set_audio_track_armed(size_t track_index, bool armed)
{
    if (track_index >= m_tracks.size() || m_tracks[track_index].kind() != TrackKind::Audio) return;
    m_tracks[track_index].set_record_armed(armed);
    if (armed) {
        assign_default_audio_input(m_tracks[track_index], track_index);
    }
    mark_dirty();
}

void Engine::start_armed_audio_recording()
{
    if (m_is_recording_audio_tracks.load(std::memory_order_acquire) || !has_armed_audio_tracks()) return;

    bool started_any = false;
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        Track& track = m_tracks[i];
        if (track.kind() != TrackKind::Audio || !track.record_armed()) continue;
        assign_default_audio_input(track, i);
        int in_l = -1, in_r = -1;
        track.get_audio_input(in_l, in_r);
        if (in_l < 0 || (uint32_t)in_l >= m_num_ins) continue;
        track.begin_audio_capture(m_sample_rate);
        started_any = true;
    }

    m_is_recording_audio_tracks.store(started_any, std::memory_order_release);
    m_audio_record_capture_enabled.store(false, std::memory_order_release);
}

void Engine::stop_armed_audio_recording(bool commit)
{
    m_audio_record_capture_enabled.store(false, std::memory_order_release);
    if (!m_is_recording_audio_tracks.exchange(false, std::memory_order_acq_rel)) return;

    bool changed = false;
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        Track& track = m_tracks[i];
        if (track.kind() != TrackKind::Audio || !track.record_armed() || !track.is_audio_capture_active()) continue;

        auto captured = track.finish_audio_capture();
        if (!commit || !captured || captured->left.empty()) continue;
        if (!track.instrument() || track.instrument()->type() != InstrumentType::Sampler) continue;

        auto* sampler = static_cast<SampleInstrument*>(track.instrument());
        size_t sample_index = track.audio_sample_index();
        if (sample_index == 0) sample_index = 1;

        if (sample_index > 0 && sample_index <= sampler->sample_count()) {
            sampler->push_undo(sample_index - 1);
            sampler->update_sample_data(sample_index - 1, captured);
        } else {
            sampler->add_sample("Recorded Take", captured);
            track.set_audio_sample_index((uint8_t)sampler->sample_count());
        }
        changed = true;
    }

    if (changed) {
        refresh_audio_track_sources(*this);
        mark_dirty();
    }
}

bool Engine::begin_record_preroll()
{
    if (!m_is_recording_audio_tracks.load(std::memory_order_acquire) || m_record_preroll_bars <= 0) {
        return false;
    }

    const size_t samples_per_beat = m_timing.samples_per_beat();
    if (samples_per_beat == 0) {
        return false;
    }

    const size_t total_samples = static_cast<size_t>(m_record_preroll_bars) * 4u * samples_per_beat;
    if (total_samples == 0) {
        return false;
    }

    m_metronome.reset();
    m_samples_until_next_beat = 0;
    m_samples_until_next_tick = 0;
    m_record_preroll_remaining_samples.store(total_samples, std::memory_order_release);
    m_record_preroll_active.store(true, std::memory_order_release);
    m_audio_record_capture_enabled.store(false, std::memory_order_release);
    return true;
}

void Engine::advance_record_preroll(size_t frames)
{
    if (!m_record_preroll_active.load(std::memory_order_acquire)) return;

    const size_t remaining = m_record_preroll_remaining_samples.load(std::memory_order_relaxed);
    if (frames >= remaining) {
        m_record_preroll_remaining_samples.store(0, std::memory_order_release);
        m_record_preroll_active.store(false, std::memory_order_release);
        m_audio_record_capture_enabled.store(
            m_is_recording_audio_tracks.load(std::memory_order_relaxed),
            std::memory_order_release);
        return;
    }

    m_record_preroll_remaining_samples.store(remaining - frames, std::memory_order_release);
}

void Engine::cancel_record_preroll()
{
    m_record_preroll_remaining_samples.store(0, std::memory_order_release);
    m_record_preroll_active.store(false, std::memory_order_release);
    m_audio_record_capture_enabled.store(false, std::memory_order_release);
}

void Engine::toggle_metronome() { m_metronome_enabled = !m_metronome_enabled; }
void Engine::set_metronome_enabled(bool e) { m_metronome_enabled = e; }
void Engine::enable_record(bool e)
{
    const bool was_enabled = m_record_enabled.exchange(e, std::memory_order_acq_rel);
    if (was_enabled == e) return;
}

bool Engine::analyze_audio_track(size_t audio_track_index)
{
    if (audio_track_index >= m_tracks.size() || is_tempo_track(audio_track_index)) return false;

    Track& audio_track = m_tracks[audio_track_index];
    if (audio_track.kind() != TrackKind::Audio) return false;

    const int linked_track_index = audio_track.linked_track();
    if (linked_track_index < 0 || (size_t)linked_track_index >= m_tracks.size()) return false;

    Track& note_track = m_tracks[(size_t)linked_track_index];
    if (note_track.kind() != TrackKind::Note) return false;

    std::vector<DetectedHit> hits;
    if (!detect_audio_track_hits(*this, audio_track_index, hits)) return false;

    const DrumTypeInfo& drum = drum_type_info(audio_track.drum_type());
    const uint8_t gm_note = drum.midi_note;
    materialize_unique_order_patterns(*this);

    for (size_t pat_idx : m_order) {
        if (pat_idx >= m_patterns.size()) continue;
        Pattern& pat = *m_patterns[pat_idx];
        if ((size_t)linked_track_index >= pat.track_count()) continue;
        pat.set_column_count((size_t)linked_track_index, 1);
        for (size_t row = 0; row < pat.row_count(); ++row) {
            pat.event((size_t)linked_track_index, row, 0) = TrackEvent();
        }
    }

    for (const auto& hit : hits) {
        size_t rows_done = 0;
        for (size_t pat_idx : m_order) {
            if (pat_idx >= m_patterns.size()) continue;
            Pattern& pat = *m_patterns[pat_idx];
            const size_t pat_rows = pat.row_count();
            if (hit.absolute_row < rows_done + pat_rows) {
                TrackEvent& ev = pat.event((size_t)linked_track_index, hit.absolute_row - rows_done, 0);
                if (ev.note == NOTE_EMPTY || ev.volume == 255 || hit.volume > ev.volume) {
                    ev.note = gm_note;
                    ev.sample_idx = 0;
                    ev.volume = hit.volume;
                    ev.effect1 = 0;
                    ev.param1 = 0;
                    ev.effect2 = 0;
                    ev.param2 = 0;
                }
                break;
            }
            rows_done += pat_rows;
        }
    }

    mark_dirty();
    return true;
}

bool Engine::retrigger_stretch_audio_track(size_t audio_track_index,
                                           bool use_selection,
                                           size_t selection_start_row,
                                           size_t selection_end_row)
{
    if (audio_track_index >= m_tracks.size() || is_tempo_track(audio_track_index)) return false;

    Track& audio_track = m_tracks[audio_track_index];
    if (audio_track.kind() != TrackKind::Audio) return false;
    if (!audio_track.instrument() || audio_track.instrument()->type() != InstrumentType::Sampler) return false;

    const int linked_track_index = audio_track.linked_track();
    if (linked_track_index < 0 || (size_t)linked_track_index >= m_tracks.size()) return false;

    Track& note_track = m_tracks[(size_t)linked_track_index];
    if (note_track.kind() != TrackKind::Note) return false;

    auto sample = resolve_audio_track_sample(*this, audio_track_index);
    if (!sample || sample->left.empty()) return false;

    const size_t song_rows = total_song_rows(*this);
    if (song_rows == 0) return false;

    size_t start_row = 0;
    size_t end_row = song_rows;
    if (use_selection) {
        start_row = std::min(selection_start_row, selection_end_row);
        end_row = std::max(selection_start_row, selection_end_row);
        if (end_row == start_row) ++end_row;
        if (start_row >= song_rows) return false;
        end_row = std::min(end_row, song_rows);
    }

    std::vector<DetectedHit> detected_hits;
    if (!detect_audio_track_hits(*this, audio_track_index, detected_hits)) return false;

    std::vector<size_t> source_rows;
    source_rows.reserve(detected_hits.size());
    for (const auto& hit : detected_hits) {
        if (hit.absolute_row >= start_row && hit.absolute_row < end_row) {
            source_rows.push_back(hit.absolute_row);
        }
    }

    std::vector<size_t> target_rows = collect_note_rows(*this, (size_t)linked_track_index, start_row, end_row);
    if (source_rows.empty() || target_rows.empty()) return false;

    const size_t paired_hits = std::min(source_rows.size(), target_rows.size());
    if (paired_hits == 0) return false;

    const size_t start_sample = sample_index_for_absolute_row(*this, *sample, start_row);
    const size_t end_sample = std::min(sample->left.size(), sample_index_for_absolute_row(*this, *sample, end_row));
    if (end_sample <= start_sample) return false;

    struct Anchor {
        size_t source_sample = 0;
        size_t target_sample = 0;
    };
    std::vector<Anchor> anchors;
    anchors.reserve(paired_hits + 2);
    anchors.push_back({start_sample, start_sample});

    for (size_t i = 0; i < paired_hits; ++i) {
        const size_t source_sample = std::clamp(sample_index_for_absolute_row(*this, *sample, source_rows[i]), start_sample, end_sample);
        const size_t target_sample = std::clamp(sample_index_for_absolute_row(*this, *sample, target_rows[i]), start_sample, end_sample);
        if (source_sample <= anchors.back().source_sample || target_sample <= anchors.back().target_sample) {
            continue;
        }
        anchors.push_back({source_sample, target_sample});
    }

    if (anchors.back().source_sample != end_sample || anchors.back().target_sample != end_sample) {
        anchors.push_back({end_sample, end_sample});
    }

    if (anchors.size() < 2) return false;

    SampleData replacement;
    replacement.sample_rate = sample->sample_rate;
    const bool has_right = !sample->right.empty();
    if (has_right) replacement.right.clear();

    for (size_t i = 0; i + 1 < anchors.size(); ++i) {
        const size_t source_segment_start = anchors[i].source_sample;
        const size_t source_segment_end = anchors[i + 1].source_sample;
        const size_t target_segment_start = anchors[i].target_sample;
        const size_t target_segment_end = anchors[i + 1].target_sample;
        if (source_segment_end <= source_segment_start || target_segment_end <= target_segment_start) {
            continue;
        }

        const size_t target_length = target_segment_end - target_segment_start;
        std::vector<float> left_segment;
        if (!stretch_channel_segment(sample->left,
                                     source_segment_start,
                                     source_segment_end,
                                     target_length,
                                     (uint32_t)std::max(1, sample->sample_rate),
                                     left_segment)) {
            return false;
        }
        replacement.left.insert(replacement.left.end(), left_segment.begin(), left_segment.end());

        if (has_right) {
            std::vector<float> right_segment;
            const std::vector<float>& source_right = sample->right.empty() ? sample->left : sample->right;
            if (!stretch_channel_segment(source_right,
                                         source_segment_start,
                                         source_segment_end,
                                         target_length,
                                         (uint32_t)std::max(1, sample->sample_rate),
                                         right_segment)) {
                return false;
            }
            replacement.right.insert(replacement.right.end(), right_segment.begin(), right_segment.end());
        }
    }

    const size_t target_selection_length = end_sample - start_sample;
    fit_channel_length(replacement.left, target_selection_length);
    if (has_right) {
        fit_channel_length(replacement.right, target_selection_length);
    }

    auto* sampler = static_cast<SampleInstrument*>(audio_track.instrument());
    size_t sample_index = audio_track.audio_sample_index();
    if (sample_index == 0) sample_index = 1;
    if (sample_index == 0 || sample_index > sampler->sample_count()) return false;

    sampler->push_undo(sample_index - 1);
    auto updated_sample = std::make_shared<SampleData>(*sample);
    updated_sample->cut(start_sample, end_sample);
    updated_sample->paste_at(start_sample, replacement);
    sampler->update_sample_data(sample_index - 1, updated_sample);

    refresh_audio_track_sources(*this);
    mark_dirty();
    return true;
}

bool Engine::quantize_note_track(size_t note_track_index, bool align_to_click)
{
    if (note_track_index >= m_tracks.size() || is_tempo_track(note_track_index)) return false;

    Track& note_track = m_tracks[note_track_index];
    if (note_track.kind() != TrackKind::Note) return false;

    struct QuantizedEvent {
        TrackEvent event;
        size_t source_column = 0;
    };

    size_t total_rows = 0;
    for (size_t pat_idx : m_order) {
        if (pat_idx < m_patterns.size()) total_rows += m_patterns[pat_idx]->row_count();
    }
    if (total_rows == 0) return false;

    std::vector<std::vector<QuantizedEvent>> buckets(total_rows);
    size_t rows_done = 0;
    for (size_t pat_idx : m_order) {
        if (pat_idx >= m_patterns.size()) continue;
        Pattern& pat = *m_patterns[pat_idx];
        if (note_track_index >= pat.track_count()) {
            rows_done += pat.row_count();
            continue;
        }

        const size_t col_count = pat.column_count(note_track_index);
        for (size_t row = 0; row < pat.row_count(); ++row) {
            for (size_t col = 0; col < col_count; ++col) {
                const TrackEvent& ev = pat.event(note_track_index, row, col);
                if (ev.note == NOTE_EMPTY) continue;

                size_t target_row = rows_done + row;
                if (align_to_click) {
                    const size_t click_rows = std::max<size_t>(1, lpb());
                    target_row = ((target_row + click_rows / 2) / click_rows) * click_rows;
                    if (target_row >= total_rows) {
                        target_row = ((total_rows - 1) / click_rows) * click_rows;
                    }
                }
                buckets[target_row].push_back({ev, col});
            }
        }
        rows_done += pat.row_count();
    }

    bool has_events = false;
    size_t required_columns = 1;
    for (auto& bucket : buckets) {
        if (bucket.empty()) continue;
        has_events = true;
        std::stable_sort(bucket.begin(), bucket.end(), [](const QuantizedEvent& lhs, const QuantizedEvent& rhs) {
            const int lhs_volume = lhs.event.volume == 255 ? -1 : (int)lhs.event.volume;
            const int rhs_volume = rhs.event.volume == 255 ? -1 : (int)rhs.event.volume;
            if (lhs_volume != rhs_volume) return lhs_volume > rhs_volume;
            return lhs.source_column < rhs.source_column;
        });
        if (bucket.size() > kQuantizeNoteTrackMaxColumns) {
            bucket.resize(kQuantizeNoteTrackMaxColumns);
        }
        required_columns = std::max(required_columns, bucket.size());
    }
    if (!has_events) return false;

    materialize_unique_order_patterns(*this);

    for (size_t pat_idx : m_order) {
        if (pat_idx >= m_patterns.size()) continue;
        Pattern& pat = *m_patterns[pat_idx];
        if (note_track_index >= pat.track_count()) continue;
        const size_t existing_columns = pat.column_count(note_track_index);
        for (size_t row = 0; row < pat.row_count(); ++row) {
            TrackEvent& row_event = pat.event(note_track_index, row, 0);
            const uint8_t effect1 = row_event.effect1;
            const uint8_t param1 = row_event.param1;
            const uint8_t effect2 = row_event.effect2;
            const uint8_t param2 = row_event.param2;
            for (size_t col = 0; col < existing_columns; ++col) {
                pat.event(note_track_index, row, col) = TrackEvent();
            }
            row_event.effect1 = effect1;
            row_event.param1 = param1;
            row_event.effect2 = effect2;
            row_event.param2 = param2;
        }
        pat.set_column_count(note_track_index, std::max<size_t>(1, std::min(required_columns, kQuantizeNoteTrackMaxColumns)));
    }

    rows_done = 0;
    for (size_t pat_idx : m_order) {
        if (pat_idx >= m_patterns.size()) continue;
        Pattern& pat = *m_patterns[pat_idx];
        if (note_track_index >= pat.track_count()) {
            rows_done += pat.row_count();
            continue;
        }

        for (size_t row = 0; row < pat.row_count(); ++row) {
            const size_t absolute_row = rows_done + row;
            if (absolute_row >= buckets.size()) continue;
            const auto& bucket = buckets[absolute_row];
            for (size_t col = 0; col < bucket.size(); ++col) {
                TrackEvent& ev = pat.event(note_track_index, row, col);
                ev.note = bucket[col].event.note;
                ev.sample_idx = bucket[col].event.sample_idx;
                ev.volume = bucket[col].event.volume;
            }
        }
        rows_done += pat.row_count();
    }

    mark_dirty();
    return true;
}

TransportState Engine::transport_state() const { return transport().state(); }
size_t Engine::current_order_pos() const { return m_order_pos.load(); }
void Engine::set_loop(bool e) { transport().set_loop(e); }

void Engine::set_master_gain(float g) { m_master.set_gain(g); }
float Engine::master_gain() const { return m_master.gain(); }
float Engine::master_meter_l() const { return m_master.meter_l(); }
float Engine::master_meter_r() const { return m_master.meter_r(); }

float Engine::input_level(uint32_t ch) const {
    if (ch < MAX_INS) return m_input_levels[ch].load(std::memory_order_relaxed);
    return 0.0f;
}
Transport& Engine::transport() { return *m_transport; }
const Transport& Engine::transport() const { return *m_transport; }

size_t Engine::total_song_samples() const { 
    size_t total_rows = 0;
    for (uint8_t pat_idx : m_order) {
        if (pat_idx < m_patterns.size()) {
            total_rows += m_patterns[pat_idx]->row_count();
        }
    }
    // BPM/Speed might change during playback, but this is a base estimate
    return total_rows * m_timing.samples_per_row(); 
}

bool Engine::render_to_wav(const std::string& path, const ExportOptions& opts) {
    if (m_order.empty()) return false;

    // Save current state
    bool was_playing = transport().is_playing();
    size_t old_order_pos = m_order_pos.load();
    size_t old_row = m_current_row;
    size_t old_tick = m_current_tick;
    bool old_loop = transport().m_loop_pattern.load();
    uint32_t old_sr = m_sample_rate;
    size_t old_order_start = m_order_start.load();
    size_t old_order_end   = m_order_end.load();

    m_is_exporting.store(true);
    m_export_progress.store(0.0f);
    m_master.m_export_mute.store(true);

    // Prepare for rendering
    stop();
    m_sample_rate = opts.sample_rate;
    m_timing.set_sample_rate(opts.sample_rate);

    // Export always covers the full song, ignoring any loop range
    m_order_start.store(0);
    m_order_end.store(0); // 0 = "end of order list" sentinel

    m_order_pos.store(0);
    set_active_pattern(m_order[0]);
    m_current_row = 0;
    m_current_tick = 0;
    m_samples_until_next_tick = 0;
    transport().set_loop(false);

    // Estimate length
    size_t total_frames = total_song_samples();
    const int lead_in_bars = std::max(0, std::min(2, opts.lead_in_bars));
    const size_t lead_in_frames = static_cast<size_t>(lead_in_bars) * 4u * m_timing.samples_per_beat();
    if (total_frames == 0) {
        m_order_start.store(old_order_start);
        m_order_end.store(old_order_end);
        m_sample_rate = old_sr;
        m_timing.set_sample_rate(old_sr);
        m_order_pos.store(old_order_pos);
        if (old_order_pos < m_order.size()) set_active_pattern(m_order[old_order_pos]);
        m_current_row = old_row;
        m_current_tick = (int)old_tick;
        transport().set_loop(old_loop);
        if (was_playing) start();
        m_is_exporting.store(false);
        m_master.m_export_mute.store(false);
        return false;
    }

    // Buffers for export
    std::vector<float> final_l, final_r;
    std::vector<std::vector<float>> tracks_l, tracks_r;
    
    if (opts.separate_tracks) {
        tracks_l.resize(m_tracks.size());
        tracks_r.resize(m_tracks.size());
    }

    if (lead_in_frames > 0) {
        final_l.resize(lead_in_frames, 0.0f);
        final_r.resize(lead_in_frames, 0.0f);
        if (opts.separate_tracks) {
            for (size_t t = 0; t < m_tracks.size(); ++t) {
                tracks_l[t].resize(lead_in_frames, 0.0f);
                tracks_r[t].resize(lead_in_frames, 0.0f);
            }
        }
    }

    size_t rendered = 0;
    const size_t block_size = 512;
    float bl[block_size], br[block_size];

    transport().play();

    while (rendered < total_frames && transport().is_playing()) {
        size_t to_render = std::min(block_size, total_frames - rendered);
        
        if (opts.realtime) {
            // Realtime export: let JACK drive the audio thread; stop when
            // transport halts (song-end sets it via tick-advance) or when
            // we've recorded at least total_frames samples as a safety cap.
            m_master.m_recorded_l.clear();
            m_master.m_recorded_r.clear();
            m_master.m_recorded_l.resize(total_frames);
            m_master.m_recorded_r.resize(total_frames);
            m_master.m_recorded_write_pos.store(0);
            m_master.m_is_recording.store(true);

            start();
            while (transport().is_playing() &&
                   m_master.m_recorded_write_pos.load() < total_frames) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            m_master.m_is_recording.store(false);
            stop();

            size_t final_recorded = m_master.m_recorded_write_pos.load();
            m_master.m_recorded_l.resize(final_recorded);
            m_master.m_recorded_r.resize(final_recorded);
            final_l = std::move(m_master.m_recorded_l);
            final_r = std::move(m_master.m_recorded_r);
            break;
        }

        process_block(bl, br, to_render, nullptr);
        
        for (size_t i = 0; i < to_render; ++i) {
            final_l.push_back(bl[i]);
            final_r.push_back(br[i]);
        }

        if (opts.separate_tracks) {
            for (size_t t = 0; t < m_tracks.size(); ++t) {
                for (size_t i = 0; i < to_render; ++i) {
                    tracks_l[t].push_back(m_track_l[t][i]);
                    tracks_r[t].push_back(m_track_r[t][i]);
                }
            }
        }

        rendered += to_render;
        m_export_progress.store((float)rendered / (float)total_frames);
        
        if (m_order_pos.load() >= m_order.size() || (m_order_pos.load() == m_order.size()-1 && m_current_row >= pattern().row_count())) {
             break;
        }
    }

    if (lead_in_frames > 0) {
        Metronome export_metronome;
        export_metronome.set_sample_rate((double)opts.sample_rate);
        export_metronome.set_volume(m_metronome_volume.load());
        export_metronome.reset();

        size_t click_samples_until_next_beat = 0;
        size_t generated = 0;
        const size_t samples_per_beat = m_timing.samples_per_beat();

        while (generated < lead_in_frames) {
            const size_t block = std::min(block_size, lead_in_frames - generated);
            export_metronome.process(final_l.data() + generated,
                                     final_r.data() + generated,
                                     block,
                                     click_samples_until_next_beat,
                                     samples_per_beat);
            generated += block;
        }
    }

    // Save to WAV
    bool success = write_wav(path, final_l, final_r, final_l.size(), opts.sample_rate);

    if (success && opts.separate_tracks) {
        fs::path p(path);
        std::string stem = p.stem().string();
        std::string ext = p.extension().string();
        fs::path parent = p.parent_path();
        
        for (size_t t = 0; t < m_tracks.size(); ++t) {
            std::string tname = sanitize_export_stem_name(track(t).name(), t);
            std::string filename = stem + "_" + tname + ext;
            fs::path track_path = parent / filename;
            write_wav(track_path.string(), tracks_l[t], tracks_r[t], tracks_l[t].size(), opts.sample_rate);
        }
    }

    if (success && opts.export_click) {
        std::vector<float> click_l(final_l.size(), 0.0f);
        std::vector<float> click_r(final_r.size(), 0.0f);

        if (!click_l.empty()) {
            Metronome export_metronome;
            export_metronome.set_sample_rate((double)opts.sample_rate);
            export_metronome.set_volume(m_metronome_volume.load());
            export_metronome.reset();

            size_t click_samples_until_next_beat = 0;
            size_t generated = 0;
            const size_t samples_per_beat = m_timing.samples_per_beat();

            while (generated < click_l.size()) {
                const size_t block = std::min(block_size, click_l.size() - generated);
                export_metronome.process(click_l.data() + generated,
                                         click_r.data() + generated,
                                         block,
                                         click_samples_until_next_beat,
                                         samples_per_beat);
                generated += block;
            }
        }

        fs::path p(path);
        const fs::path click_path = p.parent_path() / (p.stem().string() + "_click" + p.extension().string());
        success = write_wav(click_path.string(), click_l, click_r, click_l.size(), opts.sample_rate) && success;
    }

    // Restore state
    stop();
    m_sample_rate = old_sr;
    m_timing.set_sample_rate(old_sr);
    m_order_start.store(old_order_start);
    m_order_end.store(old_order_end);
    m_order_pos.store(old_order_pos);
    set_active_pattern(m_order[old_order_pos]);
    m_current_row = old_row;
    m_current_tick = (int)old_tick;
    transport().set_loop(old_loop);
    if (was_playing) start();

    m_is_exporting.store(false);
    m_master.m_export_mute.store(false);
    m_export_progress.store(1.0f);

    return success;
}
inline float Engine::soft_clip(float x) { const float limit = 0.95f; if (x > limit) return limit; if (x < -limit) return -limit; return x; }
UndoStack& Engine::undo_stack() { return m_undo; }
BlockClipboard& Engine::clipboard() { return m_clipboard; }

void Engine::start_recording_sample(SampleRecordMode mode, uint32_t channel, bool mono) {
    m_recording_sample_mode.store(mode);
    m_recording_input_channel = channel;
    m_recording_is_mono = mono;
    auto sd = std::make_shared<SampleData>();
    sd->sample_rate = (int)m_sample_rate;
    // Pre-reserve 10 minutes of audio to avoid RT-thread reallocations.
    // 48000 * 60 * 10 = 28.8M samples (~115MB per channel)
    size_t reserve_frames = (size_t)m_sample_rate * 60 * 10;
    sd->left.resize(reserve_frames);
    if (!mono) sd->right.resize(reserve_frames);
    m_recording_sample_data = sd;
    m_recording_synced_active.store(false);
    m_recording_synced_row.store(0);
    m_recording_loop_count.store(0);
    m_recording_write_pos.store(0);
    // Publish raw pointer BEFORE setting the recording flag so the RT thread
    // always sees a valid pointer when m_is_recording_sample is true.
    m_recording_sample_ptr.store(sd.get(), std::memory_order_release);
    m_is_recording_sample.store(true, std::memory_order_release);
}

void Engine::stop_recording_sample() {
    // Clear the flag first so the RT thread stops appending.
    m_is_recording_sample.store(false, std::memory_order_release);
    m_recording_synced_active.store(false);
    
    // Resize the vectors to the actual recorded number of samples.
    size_t recorded = m_recording_write_pos.load(std::memory_order_acquire);
    if (m_recording_sample_data) {
        m_recording_sample_data->left.resize(recorded);
        if (!m_recording_is_mono) {
            m_recording_sample_data->right.resize(recorded);
        }
    }
    
    // Null the raw pointer after stopping so there's no dangling access.
    m_recording_sample_ptr.store(nullptr, std::memory_order_release);
}

bool Engine::write_wav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, size_t frames, uint32_t sample_rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    struct {
        char chunkID[4]; uint32_t chunkSize; char format[4];
        char subchunk1ID[4]; uint32_t subchunk1Size; uint16_t audioFormat;
        uint16_t numChannels; uint32_t sampleRate; uint32_t byteRate;
        uint16_t blockAlign; uint16_t bitsPerSample;
        char subchunk2ID[4]; uint32_t subchunk2Size;
    } h;
    
    memcpy(h.chunkID, "RIFF", 4);
    h.chunkSize = 36 + (uint32_t)(frames * 2 * 2);
    memcpy(h.format, "WAVE", 4);
    memcpy(h.subchunk1ID, "fmt ", 4);
    h.subchunk1Size = 16; h.audioFormat = 1; h.numChannels = 2;
    h.sampleRate = sample_rate; h.byteRate = sample_rate * 2 * 2;
    h.blockAlign = 4; h.bitsPerSample = 16;
    memcpy(h.subchunk2ID, "data", 4);
    h.subchunk2Size = (uint32_t)(frames * 2 * 2);

    f.write(reinterpret_cast<char*>(&h), sizeof(h));

    for (size_t i = 0; i < frames; ++i) {
        float sl_f = std::max(-1.f, std::min(1.f, l[i]));
        float sr_f = std::max(-1.f, std::min(1.f, r[i]));
        int16_t sl = static_cast<int16_t>(sl_f * 32767.f);
        int16_t sr = static_cast<int16_t>(sr_f * 32767.f);
        f.write(reinterpret_cast<char*>(&sl), 2);
        f.write(reinterpret_cast<char*>(&sr), 2);
    }
    return true;
}

double Engine::get_current_time_seconds() const {
    size_t total_rows = 0;
    
    if (transport().m_loop_pattern.load()) {
        // Only current pattern time
        total_rows = m_current_row;
    } else {
        // Song time from beginning
        for (size_t i = 0; i < m_order_pos.load() && i < m_order.size(); ++i) {
            size_t pat_idx = m_order[i];
            if (pat_idx < m_patterns.size()) {
                total_rows += m_patterns[pat_idx]->row_count();
            }
        }
        total_rows += m_current_row;
    }
    
    double samples = (double)total_rows * m_timing.samples_per_row();
    samples += (double)m_current_tick * m_timing.samples_per_tick();
    
    // Add sub-tick progress if playing
    if (transport().is_playing()) {
        samples += (double)m_timing.samples_per_tick() - (double)m_samples_until_next_tick;
    }
    
    return samples / (double)m_sample_rate;
}

double Engine::get_time_at_row(size_t row) const {
    size_t total_rows = 0;
    
    // Use edit order pos for calculation when not playing
    size_t edit_pos = m_edit_order_pos.load();
    for (size_t i = 0; i < edit_pos && i < m_order.size(); ++i) {
        size_t pat_idx = m_order[i];
        if (pat_idx < m_patterns.size()) {
            total_rows += m_patterns[pat_idx]->row_count();
        }
    }
    total_rows += row;
    
    double samples = (double)total_rows * m_timing.samples_per_row();
    return samples / (double)m_sample_rate;
}

} // namespace tpanar_ns
