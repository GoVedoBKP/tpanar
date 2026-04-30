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

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>

#include "../sequencer/timing.h"
#include "../sequencer/pattern.h"
#include "../sequencer/note.h"
#include "../mixer/track.h"
#include "../mixer/mixer_bus.h"
#include "../mixer/track_clipboard.h"
#include "../instrument/instrument.h"
#include "../audio/audio_backend.h"
#include "../audio/sample_data.h"
#include "../audio/sample_voice.h"
#include "undo_stack.h"
#include "transport.h"
#include "key_bindings.h"
#include "../gui/theme.h"
#include "../midi/midi_queue.h"
#include "audio_thread_pool.h"
#include "../midi/midi_input.h"
#include "metronome.h"
#include "masterbus.h"
#include "../util/ringbuffer.h"
#include "engine_command.h"

namespace tpanar_ns
{

struct BlockClipboard
{
    struct Cell {
        int rel_track;
        int rel_row;
        int abs_field; // Normalized field index within a track row
        uint8_t value;
    };
    std::vector<Cell> cells;
    int width_tracks = 0;
    int height_rows = 0;
};

struct SampleClipboard
{
    std::shared_ptr<SampleData> data;
};

class Transport;
class AudioBackend;

class Engine
{
public:
    static constexpr size_t TEMPO_TRACK_INDEX = 0;

    Engine();
    ~Engine();

    bool initialize();
    void shutdown();
    void save_config();
    void load_config();  // Re-apply config fields to engine (no audio reinit)
    void new_project();
    void reinitialize_audio(uint32_t num_ins = 2, uint32_t num_outs = 2,
                            uint32_t num_midi_ins = 1, uint32_t num_midi_outs = 1);
    bool audio_active() const;
    void set_audio_backend_type(AudioBackendType type) { m_audio_backend_type = type; }
    AudioBackendType configured_audio_backend_type() const { return m_audio_backend_type; }
    AudioBackendType active_audio_backend_type() const;

    // Set the number of worker threads for parallel track processing.
    // 0 = single-threaded (off), 1-N = N workers, 255 = auto (hardware_concurrency - 1).
    // Safe to call from the GUI thread when the engine is stopped.
    void set_worker_threads(uint32_t n);

    void start();
    void stop();
    void panic();
    void play();
    void play_song();
    void play_pattern();
    void play_from_position(size_t row);
    void auto_seek();

    tpanar_ns::Transport& transport();
    const tpanar_ns::Transport& transport() const;

    void process_audio(const float* const* in_bufs, uint32_t num_ins, float** out_bufs, uint32_t num_outs, size_t nframes);
    void process_audio(const float* const* in_bufs, uint32_t num_ins, float* out_l, float* out_r, size_t nframes);

    double tempo() const;
    uint32_t lpb() const;

    void set_tempo(double);
    void set_lpb(uint32_t);

    const tpanar_ns::Pattern& pattern(size_t) const;
    tpanar_ns::Pattern& pattern(size_t);

    size_t pattern_count() const;
    void clear_patterns() { m_patterns.clear(); }
    void add_pattern(::std::unique_ptr<tpanar_ns::Pattern> pat) { m_patterns.push_back(std::move(pat)); }
    size_t create_pattern();
    size_t copy_pattern(size_t index);
    void resize_pattern(size_t index, size_t new_rows);
    void process_commands();

    ::std::vector<size_t> order_list() const;
    void set_order(const ::std::vector<size_t>&);

    size_t add_pattern_to_order();
    void remove_pattern_from_order(size_t order_pos);
    size_t copy_pattern_in_order(size_t order_pos);

    size_t m_samples_until_next_tick = 0;
    int    m_current_tick = 0;
    size_t m_current_row = 0;
    ::std::atomic<size_t> m_order_pos{0};
    ::std::atomic<size_t> m_edit_order_pos{0};
    ::std::atomic<size_t> m_order_start{0};
    ::std::atomic<size_t> m_order_end{0};

    void handle_effect_row_start(size_t track_index, const tpanar_ns::TrackEvent& ev);

    size_t current_row() const;
    bool   is_playing() const;
    void set_active_pattern(size_t index);
    size_t active_pattern() const;
    void preview_note(size_t track, uint8_t note, size_t column = 0);
    void stop_preview(size_t track, size_t column = 0);

    void start_sample_preview(std::shared_ptr<SampleData> data, int via_track = -1,
                               size_t sel_start = 0, size_t sel_end = 0, bool loop = false);
    void stop_sample_preview();
    int64_t preview_playback_pos() const { return m_preview_playback_pos.load(); }

    tpanar_ns::Pattern& pattern();

    ::std::vector<::std::unique_ptr<tpanar_ns::Instrument>> m_instruments;

    tpanar_ns::Instrument& instrument(size_t index);
    const tpanar_ns::Instrument& instrument(size_t index) const;
    size_t instrument_count() const;
    int get_instrument_index(const tpanar_ns::Instrument* inst) const;
    void add_instrument();
    void add_instrument(::std::unique_ptr<tpanar_ns::Instrument> inst);
    void remove_instrument(size_t index);
    void set_instrument_type(size_t index, InstrumentType type);
    ::std::vector<size_t> m_order;

    void add_order(size_t pattern);
    size_t current_order_pos() const;
    tpanar_ns::UndoStack& undo_stack();
    tpanar_ns::BlockClipboard& clipboard();
    tpanar_ns::SampleClipboard& sample_clipboard() { return m_sample_clipboard; }
    
    // Track audio clipboard
    tpanar_ns::TrackClipboard& track_clipboard() { return m_track_clipboard; }

    enum class SampleRecordMode { Free, Synced };
    ::std::atomic<bool> m_is_recording_sample{false};
    ::std::atomic<SampleRecordMode> m_recording_sample_mode{SampleRecordMode::Free};
    ::std::atomic<bool> m_recording_synced_active{false};
    // Current pattern row during recording (updated by RT thread, read by GUI).
    ::std::atomic<size_t> m_recording_synced_row{0};
    // Number of complete pattern loops elapsed since synced recording became active.
    ::std::atomic<size_t> m_recording_loop_count{0};
    ::std::atomic<size_t> m_recording_write_pos{0};
    // Owned by the GUI thread; the RT thread uses m_recording_sample_ptr (atomic).
    ::std::shared_ptr<SampleData> m_recording_sample_data;
    ::std::atomic<SampleData*>    m_recording_sample_ptr{nullptr};
    uint32_t m_recording_input_channel = 0;
    bool m_recording_is_mono = false;

    void start_recording_sample(SampleRecordMode mode, uint32_t channel, bool mono);
    void stop_recording_sample();

    tpanar_ns::MidiQueue<tpanar_ns::MidiMessage, 1024> m_midi_queue;
    tpanar_ns::MidiQueue<tpanar_ns::MidiMessage, 1024> m_midi_out_queue;
    tpanar_ns::MidiInput m_midi;

    ::std::atomic<bool> m_record_enabled{false};
    size_t m_record_track{0};

    void enable_record(bool e);
    void set_audio_record_enabled(bool e) { m_audio_record_enabled.store(e, std::memory_order_relaxed); }
    bool audio_record_enabled() const { return m_audio_record_enabled.load(std::memory_order_relaxed); }
    size_t record_track() const { return m_record_track; }
    void set_record_track(size_t t);
    bool has_armed_audio_tracks() const;
    bool is_audio_track_armed(size_t track_index) const;
    void set_audio_track_armed(size_t track_index, bool armed);
    bool is_recording_audio_tracks() const {
        return m_is_recording_audio_tracks.load(std::memory_order_relaxed) &&
               m_audio_record_capture_enabled.load(std::memory_order_relaxed);
    }
    void record_note(uint8_t note, size_t column = 0);
    void set_current_instrument(size_t index);
    void toggle_metronome();
    void set_metronome_enabled(bool enabled);
    int record_preroll_bars() const { return m_record_preroll_bars; }
    void set_record_preroll_bars(int bars) { m_record_preroll_bars = std::max(0, std::min(2, bars)); mark_dirty(); }
    int export_lead_in_bars() const { return m_export_lead_in_bars; }
    void set_export_lead_in_bars(int bars) { m_export_lead_in_bars = std::max(0, std::min(2, bars)); mark_dirty(); }
    void ensure_tempo_track();
    void ensure_pilot_track();
    bool analyze_audio_track(size_t audio_track_index);
    bool quantize_note_track(size_t note_track_index, bool align_to_click);
    bool retrigger_stretch_audio_track(size_t audio_track_index,
                                       bool use_selection = false,
                                       size_t selection_start_row = 0,
                                       size_t selection_end_row = 0);
    float metronome_volume() const { return m_metronome_volume; }
    void set_metronome_volume(float v) { m_metronome_volume = v; m_metronome.set_volume(v); }
    tpanar_ns::TransportState transport_state() const;

    tpanar_ns::Metronome m_metronome;

    size_t m_samples_until_next_beat{0};
    ::std::atomic<bool> m_metronome_enabled{false};
    ::std::atomic<float> m_metronome_volume{0.4f};

    tpanar_ns::MasterBus m_master;
    tpanar_ns::RingBuffer<float, 4096> m_spectral_rb;

    void set_master_gain(float g);
    float master_gain() const;
    float master_meter_l() const;
    float master_meter_r() const;
    float input_level(uint32_t ch) const;

    // Returns the live recording buffer during recording, nullptr otherwise.
    std::shared_ptr<SampleData> recording_sample_data() const { return m_recording_sample_data; }
    bool is_recording_sample() const { return m_is_recording_sample.load(std::memory_order_relaxed); }

    struct ExportOptions {
        uint32_t sample_rate = 44100;
        bool separate_tracks = false;
        bool export_click = false;
        bool realtime = false;
        int lead_in_bars = 0;
    };

    std::atomic<float> m_export_progress{0.0f};
    std::atomic<bool>  m_is_exporting{false};

    bool render_to_wav(const ::std::string& path, const ExportOptions& options);
    void process_block(float* l, float* r, size_t nframes, const float* const* in_bufs = nullptr);
    void save_project(const ::std::string& path);
    void load_project(const ::std::string& path);

    // Returns true if the project has unsaved changes.
    bool is_dirty() const {
        return m_dirty_extra || (m_undo.generation() != m_saved_generation);
    }
    // Call after save or load to clear the dirty flag.
    void mark_saved() {
        m_saved_generation = m_undo.generation();
        m_dirty_extra = false;
    }
    // Call for non-undoable changes (mixer settings, track names, etc.).
    void mark_dirty() { m_dirty_extra = true; }
    bool import_audio(const ::std::string& path);
    void handle_midi(uint8_t* data, size_t size);

    void toggle_play();
    void set_loop(bool enable);
    void set_play_position(size_t pattern, size_t row);
    tpanar_ns::Track& track(size_t index);
    const tpanar_ns::Track& track(size_t index) const;
    size_t track_count() const;
    bool is_tempo_track(size_t index) const;
    void add_track();
    void remove_track(size_t index);
    void move_track(size_t from, size_t to);

    size_t bus_count() const;
    tpanar_ns::MixerBus& bus(size_t index);
    const tpanar_ns::MixerBus& bus(size_t index) const;
    void add_bus();
    void remove_bus(size_t index);
    void move_bus(size_t from, size_t to);

    uint32_t m_num_ins = 2;
    uint32_t m_num_outs = 2;
    uint32_t m_num_midi_ins = 1;
    uint32_t m_num_midi_outs = 1;

    // Per-channel input peak levels — updated by the RT thread, read by the GUI.
    static constexpr uint32_t MAX_INS = 16;
    std::atomic<float> m_input_levels[MAX_INS]{};

    int m_gui_button_height = 25;
    int m_gui_font_size = 12;
    ThemeType m_gui_theme = ThemeType::Classic;
    unsigned int m_waveform_color = 0x00FF00FF;
    unsigned int m_bg_color = 0xCCCCCCFF; // Standard gray
    unsigned int m_fg_color = 0x000000FF; // Black
    unsigned int m_button_color = 0xCCCCCCFF;
    int m_boxtype = 2; // FL_UP_BOX
    int m_btn_boxtype = 2; // FL_UP_BOX
    int m_label_boxtype = 0; // FL_NO_BOX

    // Tracker colors
    unsigned int m_tracker_bg = 0x1E1E1EFF;
    unsigned int m_tracker_text = 0xC8C8C8FF;
    unsigned int m_tracker_cursor = 0xFFFF00FF;
    unsigned int m_tracker_row_highlight = 0x3C3C50FF;
    unsigned int m_tracker_lpb_highlight = 0x2D2D37FF;
    unsigned int m_tracker_note = 0xB4B4FFFF;
    unsigned int m_tracker_sample = 0xB4FFB4FF;
    unsigned int m_tracker_volume = 0xFFB4B4FF;
    unsigned int m_tracker_effect = 0xFFFFB4FF;

    // UI accent colors
    unsigned int m_selection_color = 0x0078D4FF;  // active item highlight
    unsigned int m_warning_color   = 0xFF4444FF;  // destructive actions / error indicators
    unsigned int m_input_bg_color  = 0xFFFFFFFF;  // text input backgrounds
    unsigned int m_label_color     = 0x666666FF;  // secondary labels / captions

    KeyBindings m_key_bindings;

    uint32_t sample_rate() const { return m_sample_rate; }
    int  base_octave() const { return m_base_octave; }
    void set_base_octave(int oct) { m_base_octave = std::max(0, std::min(9, oct)); }
    uint32_t step_size() const { return m_step_size; }
    void set_step_size(uint32_t s) { m_step_size = std::max(1u, std::min(64u, s)); }

    const std::string& project_title() const { return m_project_title; }
    void set_project_title(const std::string& t) { m_project_title = t; }
    const std::string& project_artist() const { return m_project_artist; }
    void set_project_artist(const std::string& a) { m_project_artist = a; }
    const std::string& project_album() const { return m_project_album; }
    void set_project_album(const std::string& a) { m_project_album = a; }
    const std::string& project_year() const { return m_project_year; }
    void set_project_year(const std::string& y) { m_project_year = y; }

    double get_current_time_seconds() const;
    double get_time_at_row(size_t row) const;

    friend class SongSerializer;

private:
    void start_armed_audio_recording();
    void stop_armed_audio_recording(bool commit);
    void assign_default_audio_input(tpanar_ns::Track& track, size_t track_index);
    bool begin_record_preroll();
    void advance_record_preroll(size_t frames);
    void cancel_record_preroll();
    void configure_tempo_track(tpanar_ns::Track& track);
    void configure_pilot_track(tpanar_ns::Track& track);
    size_t default_record_track() const;
    void propagate_sample_rate(uint32_t sr);

    std::mutex m_preview_mutex;
    std::unique_ptr<SampleVoice> m_preview_voice;
    std::atomic<int64_t> m_preview_playback_pos{-1};
    int m_preview_via_track = -1;

    tpanar_ns::UndoStack m_undo;
    size_t m_saved_generation = 0;
    bool   m_dirty_extra      = false;
    bool m_initialized;
    AudioBackendType m_audio_backend_type = AudioBackendType::Jack;
    ::std::unique_ptr<tpanar_ns::AudioBackend> m_backend;
    tpanar_ns::BlockClipboard m_clipboard;
    tpanar_ns::SampleClipboard m_sample_clipboard;
    tpanar_ns::TrackClipboard m_track_clipboard;
    ::std::atomic<bool> m_loop_pattern{false};

    ::std::unique_ptr<tpanar_ns::Transport> m_transport;
    ::std::atomic<bool> m_is_recording_audio_tracks{false};
    ::std::atomic<bool> m_audio_record_enabled{false};
    ::std::atomic<bool> m_audio_record_capture_enabled{false};
    ::std::atomic<bool> m_record_preroll_active{false};
    ::std::atomic<size_t> m_record_preroll_remaining_samples{0};

    tpanar_ns::Timing m_timing;

    ::std::vector<tpanar_ns::Track> m_tracks;
    ::std::vector<tpanar_ns::MixerBus> m_buses;

    ::std::unique_ptr<AudioThreadPool> m_thread_pool;

    static constexpr size_t MAX_BLOCK = 2048;
    static constexpr size_t MAX_TRACKS_INTERNAL = 64;
    static constexpr size_t MAX_BUSES_INTERNAL = 32;

    float m_mix_l[MAX_BLOCK];
    float m_mix_r[MAX_BLOCK];

    float m_track_l[MAX_TRACKS_INTERNAL][MAX_BLOCK];
    float m_track_r[MAX_TRACKS_INTERNAL][MAX_BLOCK];

    float m_bus_l[MAX_BUSES_INTERNAL][MAX_BLOCK];
    float m_bus_r[MAX_BUSES_INTERNAL][MAX_BLOCK];

    tpanar_ns::RingBuffer<tpanar_ns::EngineCommand, 128> m_cmd_queue;

    ::std::vector<::std::unique_ptr<tpanar_ns::Pattern>> m_patterns;
    ::std::atomic<size_t>  m_active_pattern{0};

    std::string m_project_temp_dir;

    bool write_wav(const ::std::string& path, const ::std::vector<float>& l, const ::std::vector<float>& r, size_t frames, uint32_t sample_rate);

    size_t total_song_samples() const;
    void reset_transport_to_start();
    size_t max_track_latency() const;
    void process_tick();
    void render_block_multi(float** out_bufs, uint32_t num_outs, size_t frames, const float* const* in_bufs = nullptr);
    void render_block(float* out_l, float* out_r, size_t frames, const float* const* in_bufs = nullptr);
    inline float soft_clip(float x);
    void handle_effect(const tpanar_ns::TrackEvent& ev);
    uint32_t m_sample_rate = 44100;
    int m_base_octave = 4;
    uint32_t m_step_size = 1;
    int m_record_preroll_bars = 1;
    int m_export_lead_in_bars = 0;

    std::string m_project_title = "Untitled Project";
    std::string m_project_artist = "Unknown Artist";
    std::string m_project_album = "";
    std::string m_project_year = "";
};

} // namespace tpanar_ns
