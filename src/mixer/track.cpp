/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "track.h"
#include "../instrument/instrument.h"
#include <cmath> // Added for powf and fabs
#include <algorithm>

namespace tpanar_ns
{

namespace {

const std::array<DrumTypeInfo, 23> kDrumTypeInfos{{
    {DrumType::None,             "Unspecified",                 36,    0.0f,     0.0f},
    {DrumType::AcousticBassDrum, "Acoustic Bass Drum (B0)",    35,   35.0f,    95.0f},
    {DrumType::BassDrum1,        "Bass Drum 1 (C1)",           36,   45.0f,   110.0f},
    {DrumType::SideStick,        "Side Stick (C#1)",           37,  800.0f,  3500.0f},
    {DrumType::AcousticSnare,    "Acoustic Snare (D1)",        38,  150.0f,   550.0f},
    {DrumType::HandClap,         "Hand Clap (D#1)",            39, 1000.0f,  4000.0f},
    {DrumType::ElectricSnare,    "Electric Snare (E1)",        40,  180.0f,   700.0f},
    {DrumType::LowFloorTom,      "Low Floor Tom (F1)",         41,   60.0f,   140.0f},
    {DrumType::ClosedHiHat,      "Closed Hi-Hat (F#1)",        42, 3000.0f, 12000.0f},
    {DrumType::HighFloorTom,     "High Floor Tom (G1)",        43,   80.0f,   180.0f},
    {DrumType::PedalHiHat,       "Pedal Hi-Hat (G#1)",         44, 2500.0f,  9000.0f},
    {DrumType::LowTom,           "Low Tom (A1)",               45,   90.0f,   190.0f},
    {DrumType::OpenHiHat,        "Open Hi-Hat (A#1)",          46, 2500.0f, 14000.0f},
    {DrumType::LowMidTom,        "Low-Mid Tom (B1)",           47,  110.0f,   240.0f},
    {DrumType::HiMidTom,         "Hi-Mid Tom (C2)",            48,  140.0f,   320.0f},
    {DrumType::CrashCymbal1,     "Crash Cymbal 1 (C#2)",       49, 3000.0f, 16000.0f},
    {DrumType::HighTom,          "High Tom (D2)",              50,  170.0f,   380.0f},
    {DrumType::RideCymbal1,      "Ride Cymbal 1 (D#2)",        51, 2500.0f, 14000.0f},
    {DrumType::RideBell,         "Ride Bell (F2)",             53, 1800.0f,  9000.0f},
    {DrumType::CrashCymbal2,     "Crash Cymbal 2 (B2)",        57, 3500.0f, 17000.0f},
    {DrumType::RideCymbal2,      "Ride Cymbal 2 (D#3)",        59, 3000.0f, 15000.0f},
    {DrumType::AnyHiHat,         "Any Hi-Hat",                 42, 2500.0f, 14000.0f},
    {DrumType::AnyRide,          "Any Ride",                   51, 1800.0f, 15000.0f},
}};

} // namespace

const std::array<DrumTypeInfo, 23>& drum_type_infos()
{
    return kDrumTypeInfos;
}

const DrumTypeInfo& drum_type_info(DrumType type)
{
    for (const auto& info : kDrumTypeInfos) {
        if (info.type == type) return info;
    }
    return kDrumTypeInfos.front();
}

tpanar_ns::Track::Track()
    : m_volume(0.5f), m_meter_l(0.0f), m_meter_r(0.0f), m_current_freq(440.0f), m_name("New Track"), m_output_bus(-1)
{
}

tpanar_ns::Track::Track(Track&& other) noexcept
    : m_fx_state(other.m_fx_state),
      m_frequency(other.m_frequency),
      pan(other.pan),
      last_volume(other.last_volume),
      m_volume(other.m_volume),
      m_mute(other.m_mute),
      m_solo(other.m_solo),
      m_output_bus(other.m_output_bus),
      m_name(std::move(other.m_name)),
      m_kind(other.m_kind),
      m_linked_track(other.m_linked_track),
      m_audio_sample_index(other.m_audio_sample_index),
      m_drum_type(other.m_drum_type),
      m_engine_rate(other.m_engine_rate),
      m_instrument(other.m_instrument),
      m_chain(std::move(other.m_chain)),
      m_audio_sample_data(std::move(other.m_audio_sample_data)),
      m_audio_voice(std::move(other.m_audio_voice)),
      m_record_armed(other.m_record_armed),
      m_capture_sample_data(std::move(other.m_capture_sample_data)),
      m_capture_sample_ptr(other.m_capture_sample_ptr.load()),
      m_capture_write_pos(other.m_capture_write_pos.load()),
      m_capture_stereo(other.m_capture_stereo),
      m_meter_l(other.m_meter_l.load()),
      m_meter_r(other.m_meter_r.load()),
      m_current_freq(other.m_current_freq)
{
}

Track& tpanar_ns::Track::operator=(Track&& other) noexcept
{
    if (this != &other) {
        m_fx_state = other.m_fx_state;
        m_frequency = other.m_frequency;
        pan = other.pan;
        last_volume = other.last_volume;
        m_volume = other.m_volume;
        m_mute = other.m_mute;
        m_solo = other.m_solo;
        m_output_bus = other.m_output_bus;
        m_name = std::move(other.m_name);
        m_kind = other.m_kind;
        m_linked_track = other.m_linked_track;
        m_audio_sample_index = other.m_audio_sample_index;
        m_drum_type = other.m_drum_type;
        m_engine_rate = other.m_engine_rate;
        m_instrument = other.m_instrument;
        m_chain = std::move(other.m_chain);
        m_audio_sample_data = std::move(other.m_audio_sample_data);
        m_audio_voice = std::move(other.m_audio_voice);
        m_record_armed = other.m_record_armed;
        m_capture_sample_data = std::move(other.m_capture_sample_data);
        m_capture_sample_ptr.store(other.m_capture_sample_ptr.load());
        m_capture_write_pos.store(other.m_capture_write_pos.load());
        m_capture_stereo = other.m_capture_stereo;
        m_meter_l.store(other.m_meter_l.load());
        m_meter_r.store(other.m_meter_r.load());
        m_current_freq = other.m_current_freq;
    }
    return *this;
}

void tpanar_ns::Track::set_instrument(tpanar_ns::Instrument* inst)
{
    m_instrument = inst;
}

tpanar_ns::Instrument* tpanar_ns::Track::instrument()
{
    return m_instrument;
}

const tpanar_ns::Instrument* tpanar_ns::Track::instrument() const
{
    return m_instrument;
}

void tpanar_ns::Track::set_name(const std::string& name)
{
    m_name = name.substr(0, 32);
}

const std::string& tpanar_ns::Track::name() const
{
    return m_name;
}

void tpanar_ns::Track::set_audio_sample_data(std::shared_ptr<SampleData> data)
{
    m_audio_sample_data = std::move(data);
    if (!m_audio_sample_data) {
        stop_audio_sample();
        return;
    }

    if (m_audio_voice) {
        m_audio_voice->set_sample(m_audio_sample_data);
        m_audio_voice->set_engine_rate(m_engine_rate);
    }
}

void tpanar_ns::Track::start_audio_sample(size_t offset_samples)
{
    if (!m_audio_sample_data) return;

    if (!m_audio_voice) {
        m_audio_voice = std::make_unique<SampleVoice>(m_audio_sample_data, m_engine_rate);
    } else {
        m_audio_voice->panic();
        m_audio_voice->set_sample(m_audio_sample_data);
        m_audio_voice->set_engine_rate(m_engine_rate);
    }

    m_audio_voice->set_adsr(0.001f, 0.001f, 1.0f, 0.001f);
    m_audio_voice->start(69, 127, 440.0f, offset_samples);
}

void tpanar_ns::Track::stop_audio_sample()
{
    if (m_audio_voice) {
        m_audio_voice->panic();
    }
}

void tpanar_ns::Track::set_engine_rate(double sr)
{
    m_engine_rate = sr;
    if (m_audio_voice) {
        m_audio_voice->set_engine_rate(sr);
    }
}

void tpanar_ns::Track::process(float* out_l,
                    float* out_r,
                    size_t nframes,
                    const float* const* in_bufs)
{
    std::fill(out_l, out_l + nframes, 0.f);
    std::fill(out_r, out_r + nframes, 0.f);

    if (m_audio_voice && m_audio_voice->active()) {
        m_audio_voice->process(out_l, out_r, nframes);
    }

    if (m_instrument)
        m_instrument->process(out_l, out_r, nframes);

    if (in_bufs && m_audio_input_l >= 0) {
        const float* in_l = in_bufs[m_audio_input_l];
        // If m_audio_input_r is valid, use it; otherwise, use in_l for mono.
        const float* in_r = (m_audio_input_r >= 0) ? in_bufs[m_audio_input_r] : in_l;

        if (m_input_delay_samples > 0 && !m_delay_buf_l.empty()) {
            size_t max_delay = m_delay_buf_l.size();
            for (size_t i = 0; i < nframes; ++i) {
                m_delay_buf_l[m_delay_ptr] = in_l[i];
                m_delay_buf_r[m_delay_ptr] = in_r[i];

                size_t read_ptr = (m_delay_ptr + max_delay - m_input_delay_samples) % max_delay;
                out_l[i] += m_delay_buf_l[read_ptr];
                out_r[i] += m_delay_buf_r[read_ptr];

                m_delay_ptr = (m_delay_ptr + 1) % max_delay;
            }
        } else {
            for (size_t i = 0; i < nframes; ++i) {
                out_l[i] += in_l[i];
                out_r[i] += in_r[i];
            }
        }
    }

    m_chain.process(out_l, out_r, nframes);

    // Apply volume + pan; measure peak in the same pass.
    const float vol      = volume();
    const float gain_l   = vol * (pan <= 0.f ? 1.0f : 1.0f - pan);
    const float gain_r   = vol * (pan >= 0.f ? 1.0f : 1.0f + pan);

    float peak_l = 0.f;
    float peak_r = 0.f;

    for (size_t i = 0; i < nframes; ++i) {
        out_l[i] *= gain_l;
        out_r[i] *= gain_r;
        float al = std::fabs(out_l[i]);
        float ar = std::fabs(out_r[i]);
        if (al > peak_l) peak_l = al;
        if (ar > peak_r) peak_r = ar;
    }

    // simple smoothing
    float prev_l = m_meter_l.load();
    float smoothed_l = 0.8f * prev_l + 0.2f * peak_l;
    m_meter_l.store(smoothed_l);

    float prev_r = m_meter_r.load();
    float smoothed_r = 0.8f * prev_r + 0.2f * peak_r;
    m_meter_r.store(smoothed_r);
}

float tpanar_ns::Track::note_to_frequency(uint8_t note) // Added namespace and class prefix
{
    return 440.0f * powf(2.0f, (int(note) - 69) / 12.0f);
}


void tpanar_ns::Track::note_on(uint8_t note, uint8_t velocity, size_t column_index, size_t offset_samples, uint8_t sample_index) // Added namespace and class prefix
{
    if (!m_instrument)
        return;

    cancel_pending_notes(column_index);

    float freq = note_to_frequency(note);

    if (m_fx_state.porta_active)
    {
        m_fx_state.porta_target = freq;
    }
    else
    {
        m_instrument->set_pitch(freq);
        m_instrument->note_on(note, velocity, column_index, offset_samples, sample_index);
        m_current_freq = freq; // Update m_current_freq on note_on
    }
}

void tpanar_ns::Track::note_off(size_t column_index)
{
    cancel_pending_notes(column_index);

    if (m_instrument)
        m_instrument->note_off(column_index);
}

void tpanar_ns::Track::panic()
{
    if (m_instrument)
        m_instrument->panic();
    stop_audio_sample();
    
    // Reset FX states
    m_fx_state.vol_slide_up = 0;
    m_fx_state.vol_slide_down = 0;
    m_fx_state.porta_active = false;
    m_fx_state.retrig_ticks = 0;
    m_fx_state.note_cut_tick = -1;
}

size_t tpanar_ns::Track::total_latency() const
{
    size_t sum = 0;

    for (const auto& fx : m_chain.effects()) // Corrected m_fx_chain to m_chain
        sum += fx->latency();

    return sum;
}

void tpanar_ns::Track::set_volume(float v)
{
    m_volume = v;
}

float tpanar_ns::Track::volume() const
{
    return m_volume;
}

void tpanar_ns::Track::set_pan(float p)
{
    pan = std::max(-1.0f, std::min(1.0f, p));
}

float tpanar_ns::Track::get_pan() const
{
    return pan;
}

void tpanar_ns::Track::set_mute(bool m)
{
    m_mute = m;
}

bool tpanar_ns::Track::muted() const
{
    return m_mute;
}


void tpanar_ns::Track::process_tick(uint32_t engine_current_tick) // Added namespace and class prefix, updated parameter
{
    // --- volume slide ---
    if (m_fx_state.vol_slide_up > 0)
        m_volume += m_fx_state.vol_slide_up * 0.01f;

    if (m_fx_state.vol_slide_down > 0)
        m_volume -= m_fx_state.vol_slide_down * 0.01f;

    if (m_volume < 0.f) m_volume = 0.f;
    if (m_volume > 1.f) m_volume = 1.f;

    // --- portamento ---
    if (m_fx_state.porta_active)
    {
        float current = m_current_freq;

        if (current < m_fx_state.porta_target)
        {
            current += m_fx_state.porta_speed;
            if (current > m_fx_state.porta_target)
                current = m_fx_state.porta_target;
        }
        else
        {
            current -= m_fx_state.porta_speed;
            if (current < m_fx_state.porta_target)
                current = m_fx_state.porta_target;
        }

        m_current_freq = current;
        m_instrument->set_pitch(current);
    }


    // --- retrig ---
    if (m_fx_state.retrig_ticks > 0)
    {
        m_fx_state.retrig_counter++;

        if (m_fx_state.retrig_counter >=
            m_fx_state.retrig_ticks)
        {
            retrigger_note();
            m_fx_state.retrig_counter = 0;
        }
    }

    // --- note cut ---
    if (m_fx_state.note_cut_tick >= 0)
    {
        if (engine_current_tick == // Corrected from m_engine_current_tick
            m_fx_state.note_cut_tick)
        {
            note_off();
            m_fx_state.note_cut_tick = -1;
        }
    }
}

void tpanar_ns::Track::retrigger_note()
{
    if (m_instrument)
    {
        // Re-trigger the current note, assuming velocity of 100
        // A more robust solution might store the last note/velocity
        m_instrument->note_on(0, 100); // Placeholder, ideally should re-trigger last note
        m_instrument->set_pitch(m_current_freq);
    }
}

float tpanar_ns::Track::meter_level_l() const
{
    return m_meter_l.load();
}

float tpanar_ns::Track::meter_level_r() const
{
    return m_meter_r.load();
}

bool tpanar_ns::Track::solo() const
{
    return m_solo;
}

void tpanar_ns::Track::set_solo(bool s)
{
    m_solo = s;
}

void tpanar_ns::Track::set_output_bus(int bus_idx)
{
    m_output_bus = bus_idx;
}

int tpanar_ns::Track::output_bus() const
{
    return m_output_bus;
}

void tpanar_ns::Track::set_effect(size_t index, ::std::unique_ptr<tpanar_ns::DSP> dsp)
{
    m_chain.set(index, std::move(dsp));
}

void tpanar_ns::Track::enable_effect(size_t index, bool en)
{
    m_chain.enable(index, en);
}

void tpanar_ns::Track::move_effect_up(size_t index)
{
    m_chain.move_up(index);
}

void tpanar_ns::Track::move_effect_down(size_t index)
{
    m_chain.move_down(index);
}

void tpanar_ns::Track::remove_effect(size_t index)
{
    m_chain.remove(index);
}

void tpanar_ns::Track::load_effect_chain(const std::string& path)
{
    m_chain.load_chain(path);
}

void tpanar_ns::Track::save_effect_chain(const std::string& path)
{
    m_chain.save_chain(path);
}

tpanar_ns::DSP* tpanar_ns::Track::get_effect(size_t index) const
{
    if (index >= tpanar_ns::MAX_INSERTS) return nullptr;
    return m_chain.effects()[index].get();
}

bool tpanar_ns::Track::is_effect_enabled(size_t index) const
{
    // Need to add m_enabled to effects access in DSPChain if we want to check it easily,
    // or we assume it's always enabled if set for now.
    return true; 
}

void tpanar_ns::Track::set_audio_input(int channel_l, int channel_r)
{
    m_audio_input_l = channel_l;
    m_audio_input_r = channel_r;
}

void tpanar_ns::Track::get_audio_input(int& channel_l, int& channel_r) const
{
    channel_l = m_audio_input_l;
    channel_r = m_audio_input_r;
}

void tpanar_ns::Track::set_input_delay(float ms, uint32_t sample_rate)
{
    m_input_delay_ms = ms;
    m_input_delay_samples = (size_t)(ms * 0.001f * sample_rate);
    
    // Resize delay buffers to accommodate up to 1 second of delay
    if (m_delay_buf_l.size() < (size_t)sample_rate) {
        m_delay_buf_l.resize(sample_rate, 0.0f);
        m_delay_buf_r.resize(sample_rate, 0.0f);
        m_delay_ptr = 0;
    }
}

float tpanar_ns::Track::input_delay() const
{
    return m_input_delay_ms;
}

void tpanar_ns::Track::begin_audio_capture(uint32_t sample_rate)
{
    auto sd = std::make_shared<SampleData>();
    sd->sample_rate = (int)sample_rate;
    const size_t reserve_frames = (size_t)sample_rate * 60 * 10;
    sd->left.resize(reserve_frames);
    m_capture_stereo = m_audio_input_r >= 0;
    if (m_capture_stereo) {
        sd->right.resize(reserve_frames);
    }
    m_capture_sample_data = sd;
    m_capture_write_pos.store(0, std::memory_order_release);
    m_capture_sample_ptr.store(sd.get(), std::memory_order_release);
}

std::shared_ptr<SampleData> tpanar_ns::Track::finish_audio_capture()
{
    m_capture_sample_ptr.store(nullptr, std::memory_order_release);
    const size_t recorded = m_capture_write_pos.load(std::memory_order_acquire);
    if (!m_capture_sample_data) return nullptr;

    m_capture_sample_data->left.resize(recorded);
    if (m_capture_stereo) {
        m_capture_sample_data->right.resize(recorded);
    } else {
        m_capture_sample_data->right.clear();
    }
    auto result = m_capture_sample_data;
    m_capture_sample_data.reset();
    return result;
}

void tpanar_ns::Track::capture_audio_input(const float* const* in_bufs, uint32_t num_ins, size_t frames)
{
    if (!in_bufs) return;
    SampleData* sd = m_capture_sample_ptr.load(std::memory_order_acquire);
    if (!sd || m_audio_input_l < 0 || (uint32_t)m_audio_input_l >= num_ins || !in_bufs[m_audio_input_l]) return;

    const size_t wp = m_capture_write_pos.load(std::memory_order_relaxed);
    const size_t cap = sd->left.size();
    if (wp >= cap) return;

    const size_t to_write = std::min(frames, cap - wp);
    const float* in_l = in_bufs[m_audio_input_l];
    if (m_capture_stereo && m_audio_input_r >= 0 && (uint32_t)m_audio_input_r < num_ins && in_bufs[m_audio_input_r]) {
        const float* in_r = in_bufs[m_audio_input_r];
        for (size_t i = 0; i < to_write; ++i) {
            sd->left[wp + i] = in_l[i];
            sd->right[wp + i] = in_r[i];
        }
    } else {
        for (size_t i = 0; i < to_write; ++i) {
            sd->left[wp + i] = in_l[i];
        }
    }
    m_capture_write_pos.fetch_add(to_write, std::memory_order_release);
}

void tpanar_ns::Track::schedule_note_on(uint8_t note, uint8_t velocity,
                                           size_t column, uint8_t sample_idx,
                                           size_t samples_per_row)
{
    cancel_pending_notes(column);

    // Apply velocity humanization first (always; independent of timing).
    int vel = velocity;
    if (m_humanize_vel > 0) {
        int spread = m_humanize_vel;
        int delta  = (int)(rng_next() % (2 * spread + 1)) - spread;
        vel = std::max(1, std::min(127, vel + delta));
    }

    // If timing humanization is disabled, fire immediately.
    if (m_humanize_timing == 0) {
        note_on(note, (uint8_t)vel, column, 0, sample_idx);
        return;
    }

    // Compute random delay in samples (0 … max_ms * sr / 1000).
    // samples_per_row is passed as a proxy for sample_rate context;
    // cap delay to at most half a row so notes never arrive late for the next row.
    int32_t max_delay = (int32_t)((m_humanize_timing / 1000.0) * 48000.0);
    int32_t delay     = (int32_t)(rng_next() % (uint32_t)(max_delay + 1));
    delay = std::min(delay, (int32_t)(samples_per_row / 2));

    // Find a free slot in the pending queue.
    for (size_t s = 0; s < MAX_PENDING; ++s) {
        if (!m_pending[s].active) {
            m_pending[s] = {true, note, (uint8_t)vel, sample_idx, column, delay};
            return;
        }
    }
    // No free slot — fire immediately rather than dropping the note.
    note_on(note, (uint8_t)vel, column, 0, sample_idx);
}

void tpanar_ns::Track::fire_pending_notes(size_t frames)
{
    for (size_t s = 0; s < MAX_PENDING; ++s) {
        if (!m_pending[s].active) continue;
        m_pending[s].delay_samples -= (int32_t)frames;
        if (m_pending[s].delay_samples <= 0) {
            note_on(m_pending[s].note, m_pending[s].velocity,
                    m_pending[s].column, 0, m_pending[s].sample_idx);
            m_pending[s].active = false;
        }
    }
}

void tpanar_ns::Track::cancel_pending_notes(size_t column)
{
    for (size_t s = 0; s < MAX_PENDING; ++s) {
        if (m_pending[s].active && m_pending[s].column == column) {
            m_pending[s].active = false;
        }
    }
}

} // namespace tpanar_ns
