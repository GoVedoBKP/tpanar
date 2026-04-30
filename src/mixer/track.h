/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once

#include "../dsp/dsp_chain.h"
#include "../audio/sample_data.h"
#include "../audio/sample_voice.h"
#include <array>
#include <vector>
#include <string>
#include <cstdint> // Added for uint8_t
#include <atomic>  // Added for std::atomic

namespace tpanar_ns
{

    struct TrackEffectState
    {
        // volume slide
        int vol_slide_up = 0;
        int vol_slide_down = 0;

        // portamento
        float porta_target = 0.f;
        float porta_speed = 0.f;
        bool  porta_active = false;

        // retrig
        int retrig_ticks = 0;
        int retrig_counter = 0;

        // note cut
        int note_cut_tick = -1;
    };

class Instrument;

enum class TrackKind {
    Note = 0,
    Audio = 1,
    Pilot = 2
};

enum class DrumType {
    None = 0,
    AcousticBassDrum,
    BassDrum1,
    SideStick,
    AcousticSnare,
    HandClap,
    ElectricSnare,
    LowFloorTom,
    ClosedHiHat,
    HighFloorTom,
    PedalHiHat,
    LowTom,
    OpenHiHat,
    LowMidTom,
    HiMidTom,
    CrashCymbal1,
    HighTom,
    RideCymbal1,
    RideBell,
    CrashCymbal2,
    RideCymbal2,
    AnyHiHat,
    AnyRide
};

struct DrumTypeInfo {
    DrumType type;
    const char* name;
    uint8_t midi_note;
    float min_hz;
    float max_hz;
};

const std::array<DrumTypeInfo, 23>& drum_type_infos();
const DrumTypeInfo& drum_type_info(DrumType type);

class Track
{
public:
    Track();
    Track(Track&& other) noexcept;
    Track& operator=(Track&& other) noexcept;

    // Delete copy operations as they are complex with atomic/dsp
    Track(const Track&) = delete;
    Track& operator=(const Track&) = delete;

    void process(float* out_l,
                 float* out_r,
                 size_t nframes,
                 const float* const* in_bufs = nullptr);

    void set_instrument(tpanar_ns::Instrument* inst);
    tpanar_ns::Instrument* instrument();
    const tpanar_ns::Instrument* instrument() const;

    void set_name(const std::string& name);
    const std::string& name() const;
    void set_kind(TrackKind kind) { m_kind = kind; }
    TrackKind kind() const { return m_kind; }
    void set_linked_track(int index) { m_linked_track = index; }
    int linked_track() const { return m_linked_track; }
    void set_audio_sample_index(uint8_t index) { m_audio_sample_index = index; }
    uint8_t audio_sample_index() const { return m_audio_sample_index; }
    void set_drum_type(DrumType type) { m_drum_type = type; }
    DrumType drum_type() const { return m_drum_type; }
    void set_audio_sample_data(std::shared_ptr<SampleData> data);
    void start_audio_sample(size_t offset_samples = 0);
    void stop_audio_sample();
    bool has_audio_sample() const { return (bool)m_audio_sample_data; }
    void set_engine_rate(double sr);

    TrackEffectState m_fx_state;

    float m_frequency = 440.0f;
    float pan    = 0.0f;  // -1 left, +1 right
    uint8_t last_volume = 127;

    void note_on(uint8_t note, uint8_t velocity, size_t column_index = 0, size_t offset_samples = 0, uint8_t sample_index = 0);
    void note_off(size_t column_index = 0);
    void panic();

    size_t total_latency() const; // Added const

    void set_volume(float v);
    float volume() const;

    void set_pan(float p);
    float get_pan() const;

    void set_mute(bool m);
    bool muted() const;

    float meter_level_l() const;
    float meter_level_r() const;
    bool solo() const;
    void set_solo(bool s); // Add this

    void set_output_bus(int bus_idx);
    int output_bus() const;

    float note_to_frequency(uint8_t note);
    void process_tick(uint32_t engine_current_tick);

    // --- Humanization (applies to SoundFont / Plugin / MIDI tracks) ---
    // Velocity spread: ± this many velocity units (0 = off, max 64)
    void set_humanize_vel(uint8_t v) { m_humanize_vel = v; }
    uint8_t humanize_vel() const { return m_humanize_vel; }

    // Timing jitter: max random onset delay in milliseconds (0 = off, max 100)
    void set_humanize_timing(uint8_t ms) { m_humanize_timing = ms; }
    uint8_t humanize_timing() const { return m_humanize_timing; }

    // Called by the engine's process_tick to queue or immediately fire a note.
    void schedule_note_on(uint8_t note, uint8_t velocity, size_t column,
                          uint8_t sample_idx, size_t samples_per_row);

    // Called at the start of every audio block to fire any pending delayed notes.
    void fire_pending_notes(size_t frames);

    // Track minimization
    void set_minimized(bool min) { m_minimized = min; }
    bool is_minimized() const { return m_minimized; }

    // DSP Chain management
    void set_effect(size_t index, ::std::unique_ptr<tpanar_ns::DSP> dsp);
    void enable_effect(size_t index, bool en);
    void move_effect_up(size_t index);
    void move_effect_down(size_t index);
    void remove_effect(size_t index);
    tpanar_ns::DSP* get_effect(size_t index) const;
    bool is_effect_enabled(size_t index) const;

    const tpanar_ns::DSPChain& chain() const { return m_chain; }
    tpanar_ns::DSPChain& chain() { return m_chain; }

    void save_effect_chain(const std::string& path);
    void load_effect_chain(const std::string& path);

    // Audio Input for MIDI instruments / external hardware
    void set_audio_input(int channel_l, int channel_r);
    void get_audio_input(int& channel_l, int& channel_r) const;
    void set_input_delay(float ms, uint32_t sample_rate);
    float input_delay() const;
    void set_record_armed(bool armed) { m_record_armed = armed; }
    bool record_armed() const { return m_record_armed; }
    void begin_audio_capture(uint32_t sample_rate);
    std::shared_ptr<SampleData> finish_audio_capture();
    void capture_audio_input(const float* const* in_bufs, uint32_t num_ins, size_t frames);
    bool is_audio_capture_active() const { return m_capture_sample_ptr.load(std::memory_order_acquire) != nullptr; }

private:
    void cancel_pending_notes(size_t column);

    float m_volume = 0.5f;
    bool  m_mute   = false;
    bool  m_solo   = false;
    bool  m_minimized = false;  // Track minimization state
    int   m_output_bus = -1; // -1 = Master
    std::string m_name;
    TrackKind m_kind = TrackKind::Note;
    int m_linked_track = -1;
    uint8_t m_audio_sample_index = 1;
    DrumType m_drum_type = DrumType::None;
    double m_engine_rate = 44100.0;

    tpanar_ns::Instrument* m_instrument = nullptr;
    tpanar_ns::DSPChain    m_chain;
    std::shared_ptr<SampleData> m_audio_sample_data;
    std::unique_ptr<SampleVoice> m_audio_voice;
    
    // Audio Input & Delay Compensation
    int m_audio_input_l = -1;
    int m_audio_input_r = -1;
    float m_input_delay_ms = 0.0f;
    size_t m_input_delay_samples = 0;
    std::vector<float> m_delay_buf_l;
    std::vector<float> m_delay_buf_r;
    size_t m_delay_ptr = 0;
    bool m_record_armed = false;
    std::shared_ptr<SampleData> m_capture_sample_data;
    std::atomic<SampleData*> m_capture_sample_ptr {nullptr};
    std::atomic<size_t> m_capture_write_pos {0};
    bool m_capture_stereo = false;

    // Humanization
    uint8_t m_humanize_vel    = 0; // ± velocity units
    uint8_t m_humanize_timing = 0; // max delay in ms

    struct PendingNote {
        bool    active     = false;
        uint8_t note       = 0;
        uint8_t velocity   = 0;
        uint8_t sample_idx = 0;
        size_t  column     = 0;
        int32_t delay_samples = 0; // counts down; fires when <= 0
    };
    static constexpr size_t MAX_PENDING = 16;
    PendingNote m_pending[MAX_PENDING]{};

    // Tiny xorshift RNG — lock-free, no heap, safe for the RT thread.
    mutable uint32_t m_rng_state = 0x12345678u;
    uint32_t rng_next() const {
        m_rng_state ^= m_rng_state << 13;
        m_rng_state ^= m_rng_state >> 17;
        m_rng_state ^= m_rng_state << 5;
        return m_rng_state;
    }

    ::std::atomic<float> m_meter_l {0.0f};
    ::std::atomic<float> m_meter_r {0.0f};
    float m_current_freq = 0.0f;
    void retrigger_note(); // Added
};

} // namespace tpanar_ns
