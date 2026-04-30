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

#include "voice_instrument.h"
#include "voice_synthesis_worker.h"
#include "../core/engine.h"
#include <cstdlib>
#include <cstring>
#include <sndfile.h>
#include <algorithm>
#include <cmath>
#include <samplerate.h>
#include <set>
#include <sys/stat.h>
#include <sys/types.h>
#include <espeak-ng/speak_lib.h>
#ifdef HAVE_FESTIVAL
#include <festival/festival.h>
#endif
#include <thread>

namespace tpanar_ns {

static std::mutex g_espeak_mutex;
static int g_espeak_refcount = 0;
static int g_espeak_sample_rate = 22050; // Default fallback

#ifdef HAVE_FESTIVAL
static std::mutex g_festival_mutex;
static int g_festival_refcount = 0;
#endif

// Context for espeak callback
struct EspeakContext {
    std::vector<float>* left;
    std::vector<float>* right;
    bool completed = false;
};

static int espeak_callback(short* wav, int numsamples, espeak_EVENT* events) {
    // Find the context from user_data in the first event that has it
    EspeakContext* ctx = nullptr;
    
    // Scan events for user_data
    espeak_EVENT* ev = events;
    while (ev->type != espeakEVENT_LIST_TERMINATED) {
        if (ev->user_data) {
            ctx = static_cast<EspeakContext*>(ev->user_data);
        }
        if (ev->type == espeakEVENT_MSG_TERMINATED) {
            if (ctx) ctx->completed = true;
        }
        ev++;
    }

    if (wav && numsamples > 0 && ctx) {
        // espeak-ng usually produces mono at 22050Hz or 44100Hz
        // We assume mono here and duplicate to stereo as before
        for (int i = 0; i < numsamples; ++i) {
            float s = wav[i] / 32768.0f;
            ctx->left->push_back(s);
            ctx->right->push_back(s);
        }
    }
    
    return 0; // continue synthesis
}

VoiceInstrument::VoiceInstrument(Engine* engine)
    : m_engine(engine)
{
    set_type(InstrumentType::Voice); // Changed from None to Voice
    set_name("Voice");

    std::lock_guard<std::mutex> lock(g_espeak_mutex);
    if (g_espeak_refcount == 0) {
        // Initialize espeak-ng
        // AUDIO_OUTPUT_RETRIEVAL means it will call our callback instead of playing
        int rate = espeak_Initialize(AUDIO_OUTPUT_RETRIEVAL, 500, nullptr, 0);
        if (rate > 0) {
            g_espeak_sample_rate = rate;
            espeak_SetSynthCallback(espeak_callback);
        }
    }
    g_espeak_refcount++;

#ifdef HAVE_FESTIVAL
    {
        std::lock_guard<std::mutex> lock(g_festival_mutex);
        if (g_festival_refcount == 0) {
            // Initialize Festival
            // 2100000 is a standard heap size for festival
            festival_initialize(1, 2100000);
        }
        g_festival_refcount++;
    }
#endif
}

VoiceInstrument::~VoiceInstrument() {
    {
        std::lock_guard<std::mutex> lock(g_espeak_mutex);
        g_espeak_refcount--;
        if (g_espeak_refcount == 0) {
            espeak_Terminate();
        }
    }

#ifdef HAVE_FESTIVAL
    {
        std::lock_guard<std::mutex> lock(g_festival_mutex);
        g_festival_refcount--;
        // Festival doesn't have a standard terminate() that works reliably across calls
    }
#endif
}

void VoiceInstrument::note_on(uint8_t note, uint8_t velocity, size_t column_index, size_t offset_samples, uint8_t sample_index) {
    if (velocity == 0) {
        note_off(column_index);
        return;
    }
    
    // Stop last note on same column
    note_off(column_index);
    m_last_note[column_index % 16] = note;

    // Get text for this note from the specified phrase index (sample_index)
    std::string text;
    {
        // Actually phrases aren't protected by mutex yet, but they are mostly changed from GUI thread.
        // For robustness we should probably mutex phrases too if they change often.
        text = m_phrases[sample_index];
    }
    
    // Convert MIDI note to frequency (A4 = 440 Hz, note 69)
    float note_freq = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
    
    if (!text.empty()) {
        // Check if already in cache to avoid synthesis in audio thread
        float pitch_factor = note_freq / 440.0f;
        std::string cache_key = make_cache_key(text, pitch_factor);
        
        std::shared_ptr<CachedAudio> cached;
        {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            auto it = m_audio_cache.find(cache_key);
            if (it != m_audio_cache.end()) {
                cached = it->second;
                update_lru(cache_key);
            }
        }

        if (cached) {
            m_active_audio = cached;
            m_current_text = text;
            m_current_pitch = pitch_factor; // Store the pitch factor of the cached audio
            m_playback_pos = 0.0;
            m_playback_increment = 1.0;
            m_playing = true;
        } else {
            // NOT IN CACHE - synthesis here will block audio thread!
            // But we must do it if we want sound now. 
            // The "Render" button is supposed to avoid this.
            if (synthesize_text(text, note_freq, true)) {
                m_current_pitch = pitch_factor; // Store the pitch factor of the cached audio
                m_playback_pos = 0.0;
                m_playback_increment = 1.0;
                m_playing = true;
            }
        }
    } else if (!m_current_text.empty() && m_playing) {
        // No text: pitch shift the current phrase to this note
        // (This still might trigger synthesis/pitch-shift if not cached)
        if (synthesize_text(m_current_text, note_freq, true)) {
            m_playback_pos = 0.0;
            m_playback_increment = 1.0;
        }
    }
}

void VoiceInstrument::note_off(size_t column_index) {
    (void)column_index;
    m_playing = false;
    m_playback_pos = 0.0;
}

void VoiceInstrument::panic() {
    m_playing = false;
    m_playback_pos = 0.0;
    m_current_text.clear();
}

void VoiceInstrument::set_volume(float vol) {
}

void VoiceInstrument::set_pitch(float freq) {
    // Store base pitch for pitch shifting
    float target_pitch = freq / 440.0f;
    target_pitch = std::max(0.01f, std::min(100.0f, target_pitch));
    
    // If we have active audio, calculate playback increment relative to the cached pitch
    if (m_active_audio && !m_current_text.empty()) {
        // We know what pitch the current cache is (it was synthesized at some base_freq)
        // But we don't store that base_freq explicitly in VoiceInstrument playback state.
        // For now, let's assume m_current_pitch represents the pitch of the cached audio.
        // Actually, m_current_pitch is updated here.
        
        // A better way: calculate increment based on target freq / cached freq.
        // But let's just use real-time resampling for small deviations from cached pitch.
        m_playback_increment = target_pitch / m_current_pitch;
    } else {
        m_current_pitch = target_pitch;
    }
}

void VoiceInstrument::set_text(const std::string& text, uint8_t phrase_index) {
    m_phrases[phrase_index] = text;
}

std::string VoiceInstrument::get_text(uint8_t phrase_index) const {
    return m_phrases[phrase_index];
}

void VoiceInstrument::process(float* l, float* r, size_t nframes) {
    std::fill(l, l + nframes, 0.0f);
    std::fill(r, r + nframes, 0.0f);
    
    // Local copy of shared_ptr for thread safety
    auto audio = m_active_audio;
    
    if (!m_playing || !audio || audio->left.empty()) {
        return;
    }
    
    size_t audio_len = audio->left.size();
    
    // Playback with linear interpolation for pitch shifting/slides
    for (size_t i = 0; i < nframes; ++i) {
        if (m_playback_pos >= (double)(audio_len - 1)) {
            m_playing = false;
            break;
        }
        
        size_t idx = (size_t)m_playback_pos;
        float frac = (float)(m_playback_pos - (double)idx);
        
        // Left channel interpolation
        float l0 = audio->left[idx];
        float l1 = audio->left[idx + 1];
        l[i] = (l0 + (l1 - l0) * frac) * m_volume;
        
        // Right channel interpolation
        float r0 = audio->right[idx];
        float r1 = audio->right[idx + 1];
        r[i] = (r0 + (r1 - r0) * frac) * m_volume;
        
        m_playback_pos += m_playback_increment;
    }
}

bool VoiceInstrument::synthesize_text(const std::string& text, float base_freq, bool update_active) {
    if (text.empty()) {
        fprintf(stderr, "[VoiceInst] synthesize_text FAILED: empty text\n");
        return false;
    }
    
    // Calculate pitch factor (relative to 440 Hz)
    float pitch_factor = base_freq / 440.0f;
    pitch_factor = std::max(0.01f, std::min(100.0f, pitch_factor)); // Widened range
    
    // Create cache key with pitch
    std::string cache_key = make_cache_key(text, pitch_factor);
    
    {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        // Increment lookup counter
        m_perf_metrics.total_lookups++;
        
        // Check memory cache first
        auto cache_it = m_audio_cache.find(cache_key);
        if (cache_it != m_audio_cache.end()) {
            fprintf(stderr, "[VoiceInst] Memory cache hit for: \"%s\" (pitch %.2f)\n", text.c_str(), pitch_factor);
            if (update_active) {
                m_active_audio = cache_it->second;
                m_current_text = text;
            }
            update_lru(cache_key);
            m_perf_metrics.cache_hits++;
            return true;
        }
        
        // Check disk cache
        std::shared_ptr<CachedAudio> disk_audio;
        if (load_from_disk_cache(cache_key, disk_audio)) {
            fprintf(stderr, "[VoiceInst] Disk cache hit for: \"%s\" (pitch %.2f)\n", text.c_str(), pitch_factor);
            size_t audio_size = disk_audio->size_bytes();
            evict_lru_if_needed(audio_size);
            m_audio_cache[cache_key] = disk_audio;
            m_memory_used += audio_size;
            update_lru(cache_key);
            if (update_active) {
                m_active_audio = disk_audio;
                m_current_text = text;
            }
            m_perf_metrics.disk_cache_hits++;
            return true;
        }
    }
    
    fprintf(stderr, "[VoiceInst] Synthesizing: \"%s\" (pitch %.2f, mode %s)\n", 
            text.c_str(), pitch_factor, m_tts_mode == TTSMode::RealTimeEspeak ? "eSpeak" : "Festival");
    
    // Synthesize base text (at 440 Hz)
    m_perf_metrics.total_synthesized++;  // Count TTS synthesis
    std::vector<float> out_l, out_r;
    int source_rate = 22050; // default
    
    switch (m_tts_mode) {
        case TTSMode::RealTimeEspeak:
            if (!synthesize_with_espeak(text, out_l, out_r, source_rate)) {
                fprintf(stderr, "[VoiceInst] eSpeak synthesis FAILED\n");
                return false;
            }
            break;
        case TTSMode::OfflineFestival:
#ifdef HAVE_FESTIVAL
            if (!synthesize_with_festival(text, out_l, out_r, source_rate)) {
                fprintf(stderr, "[VoiceInst] Festival synthesis FAILED\n");
                return false;
            }
#else
            fprintf(stderr, "[VoiceInst] Festival support not compiled in; falling back to eSpeak\n");
            if (!synthesize_with_espeak(text, out_l, out_r, source_rate)) {
                fprintf(stderr, "[VoiceInst] eSpeak synthesis FAILED\n");
                return false;
            }
#endif
            break;
    }
    
    if (out_l.empty()) {
        fprintf(stderr, "[VoiceInst] Synthesis FAILED: output buffer is empty\n");
        return false;
    }
    
    fprintf(stderr, "[VoiceInst] Synthesis OK: %zu frames at %d Hz\n", out_l.size(), source_rate);
    
    // Calculate total resampling ratio
    // 1.0/pitch_factor for the note pitch
    // target_rate / espeak_rate for the sample rate conversion
    float target_rate = m_engine ? (float)m_engine->sample_rate() : 44100.0f;
    float total_ratio = (1.0f / pitch_factor) * (target_rate / (float)source_rate);
    
    // Apply pitch shifting and resampling with libsamplerate
    // We always resample now because we need to match engine rate even if pitch is 1.0
    std::vector<float> pitched_l, pitched_r;
    if (!apply_libsamplerate_pitch(out_l, out_r, 1.0f / total_ratio, pitched_l, pitched_r)) {
        return false;
    }
    out_l = pitched_l;
    out_r = pitched_r;
    
    // Cache the pitch-shifted result in memory with LRU eviction
    auto final_audio = std::make_shared<CachedAudio>();
    final_audio->left = std::move(out_l);
    final_audio->right = std::move(out_r);
    size_t audio_size = final_audio->size_bytes();
    
    {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        evict_lru_if_needed(audio_size);
        m_audio_cache[cache_key] = final_audio;
        m_memory_used += audio_size;
        update_lru(cache_key);
    }

    if (update_active) {
        m_active_audio = final_audio;
        m_current_text = text;
    }
    
    // Save to disk cache for future sessions
    save_to_disk_cache(cache_key, final_audio);
    
    return true;
}

bool VoiceInstrument::synthesize_with_espeak(const std::string& text, std::vector<float>& out_l, std::vector<float>& out_r, int& out_rate) {
    if (text.empty()) return false;
    out_rate = g_espeak_sample_rate;

    // Select voice by language and gender using espeak_VOICE properties.
    // espeak-ng will pick the best matching installed voice.
    espeak_VOICE voice_spec = {};
    voice_spec.languages   = m_language.empty() ? "en" : m_language.c_str();
    voice_spec.gender      = (unsigned char)m_gender;  // 0=any, 1=male, 2=female
    voice_spec.variant     = (unsigned char)m_variant;
    espeak_SetVoiceByProperties(&voice_spec);

    // Set speed (espeak uses 80-450, default is 175)
    int speed = (int)(m_speed * 175.0f);
    espeak_SetParameter(espeakRATE, speed, 0);

    EspeakContext ctx;
    ctx.left = &out_l;
    ctx.right = &out_r;
    ctx.completed = false;

    // Use a lock to ensure only one thread synthesizes at a time
    // since espeak callback is global and we use user_data to distinguish messages,
    // but to be safe with shared resources we'll serialize.
    static std::mutex synth_mutex;
    std::lock_guard<std::mutex> lock(synth_mutex);

    unsigned int unique_id = 0;
    espeak_ERROR err = espeak_Synth(text.c_str(), text.size() + 1, 0, POS_CHARACTER, 0, 
                                    espeakCHARS_AUTO, &unique_id, &ctx);
    
    if (err != EE_OK) return false;

    // Synchronize blocks until all data is processed by the callback
    espeak_Synchronize();

    return !out_l.empty();
}

bool VoiceInstrument::synthesize_with_festival(const std::string& text, std::vector<float>& out_l, std::vector<float>& out_r, int& out_rate) {
#ifdef HAVE_FESTIVAL
    if (text.empty()) return false;

    std::lock_guard<std::mutex> lock(g_festival_mutex);

    // Map language + gender to a Festival voice command.
    // Festival primarily ships English diphone voices; fall back to default for other languages.
    // Known voices: kal_diphone (en, male), rms_diphone (en, male), awb_diphone (en-scot, male),
    //               slt_diphone (en, female), cmu_us_clb_arctic_multisyn (en, female).
    const bool is_english = m_language.empty() ||
                            m_language == "en" ||
                            m_language.substr(0, 3) == "en-";
    if (is_english) {
        if (m_gender == 2) {
            // Female: try slt first, fall back to default
            festival_eval_command(
                "(if (member 'slt_diphone (voice-list)) "
                "    (voice_slt_diphone) "
                "    (voice_default))");
        } else {
            // Male or default: kal_diphone is universally available
            festival_eval_command(
                "(if (member 'kal_diphone (voice-list)) "
                "    (voice_kal_diphone) "
                "    (voice_default))");
        }
    } else {
        festival_eval_command("(voice_default)");
    }

    // Set pitch accent/intonation
    if (m_pitch_accent != 0.5f) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "(set! (Parameter.evaluate par.pd_targets) %.2f)", 0.5f + m_pitch_accent);
        festival_eval_command(cmd);
    }

    // Set speech rate (duration stretch)
    if (m_speed != 1.0f) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "(set! (Parameter.evaluate Duration_Stretch) %.2f)", 1.0f / m_speed);
        festival_eval_command(cmd);
    }

    EST_Wave wave;
    if (!festival_text_to_wave(text.c_str(), wave)) {
        return false;
    }

    out_rate = wave.sample_rate();
    int n = wave.num_samples();
    
    out_l.reserve(n);
    out_r.reserve(n);
    for (int i = 0; i < n; ++i) {
        float s = (float)wave.a(i) / 32768.0f;
        out_l.push_back(s);
        out_r.push_back(s);
    }

    return !out_l.empty();
#else
    (void)text; (void)out_l; (void)out_r; (void)out_rate;
    return false;
#endif
}

bool VoiceInstrument::load_wav_from_file(const std::string& filepath, std::vector<float>& out_l, std::vector<float>& out_r) {
    out_l.clear();
    out_r.clear();
    
    SF_INFO sf_info = {};
    SNDFILE* file = sf_open(filepath.c_str(), SFM_READ, &sf_info);
    if (!file) {
        return false;
    }
    
    // Read all samples
    std::vector<float> samples(sf_info.frames * sf_info.channels);
    sf_count_t read = sf_readf_float(file, samples.data(), sf_info.frames);
    sf_close(file);
    
    if (read != sf_info.frames) {
        return false;
    }
    
    // Separate channels
    if (sf_info.channels == 1) {
        // Mono: duplicate to stereo
        out_l = samples;
        out_r = samples;
    } else if (sf_info.channels >= 2) {
        // Stereo or multi-channel: use first two channels
        out_l.reserve(sf_info.frames);
        out_r.reserve(sf_info.frames);
        for (sf_count_t i = 0; i < sf_info.frames; ++i) {
            out_l.push_back(samples[i * sf_info.channels + 0]);
            out_r.push_back(samples[i * sf_info.channels + 1]);
        }
    } else {
        return false;
    }
    
    return true;
}

std::string VoiceInstrument::make_cache_key(const std::string& text, float pitch_factor) const {
    // Round pitch_factor to 2 decimals to avoid cache misses from float precision.
    // Include language and gender so different voice settings produce distinct cache entries.
    int pitch_int = (int)(pitch_factor * 100.0f);
    return text + "@" + std::to_string(pitch_int)
                + "@" + m_language
                + "@" + std::to_string(m_gender);
}

std::string VoiceInstrument::get_cache_file_path(const std::string& cache_key) const {
    if (m_cache_dir.empty()) {
        return "";
    }
    // Create safe filename from cache key (replace @ with -)
    std::string safe_key = cache_key;
    for (auto& c : safe_key) {
        if (c == '@' || c == '/' || c == '\\') {
            c = '-';
        }
    }
    return m_cache_dir + "/" + safe_key + ".wav";
}

bool VoiceInstrument::load_from_disk_cache(const std::string& cache_key, std::shared_ptr<CachedAudio>& out_audio) {
    if (m_cache_dir.empty()) {
        return false;
    }
    
    std::string path = get_cache_file_path(cache_key);
    if (path.empty()) {
        return false;
    }
    
    // Check if file exists
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) {
        return false;  // File doesn't exist
    }
    
    fprintf(stderr, "[VoiceInst] Loading from disk cache: %s\n", path.c_str());
    
    auto audio = std::make_shared<CachedAudio>();
    // Load WAV file
    if (load_wav_from_file(path, audio->left, audio->right)) {
        out_audio = audio;
        return true;
    }
    fprintf(stderr, "[VoiceInst] FAILED to load WAV from disk: %s\n", path.c_str());
    return false;
}

bool VoiceInstrument::save_to_disk_cache(const std::string& cache_key, std::shared_ptr<CachedAudio> audio) {
    if (m_cache_dir.empty() || !audio) {
        return false;
    }
    
    // Create cache directory if needed
    mkdir(m_cache_dir.c_str(), 0755);
    
    std::string path = get_cache_file_path(cache_key);
    if (path.empty()) {
        return false;
    }
    
    fprintf(stderr, "[VoiceInst] Saving to disk cache: %s\n", path.c_str());
    
    // Save as WAV file
    SF_INFO sf_info = {};
    sf_info.samplerate = m_engine ? (int)m_engine->sample_rate() : 44100;
    sf_info.channels = 2;
    sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    
    SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &sf_info);
    if (!file) {
        fprintf(stderr, "[VoiceInst] FAILED to open for writing: %s (error %s)\n", path.c_str(), sf_strerror(NULL));
        return false;
    }
    
    // Interleave samples: L, R, L, R, ...
    std::vector<float> interleaved;
    interleaved.reserve(audio->left.size() * 2);
    for (size_t i = 0; i < audio->left.size(); ++i) {
        interleaved.push_back(audio->left[i]);
        interleaved.push_back(audio->right[i]);
    }
    
    sf_count_t written = sf_writef_float(file, interleaved.data(), audio->left.size());
    sf_close(file);
    
    if (written != (sf_count_t)audio->left.size()) {
        fprintf(stderr, "[VoiceInst] FAILED to write all samples to disk: %s\n", path.c_str());
    }
    
    return written == (sf_count_t)audio->left.size();
}

void VoiceInstrument::update_lru(const std::string& cache_key) {
    // Remove from LRU if already present
    auto it = std::find(m_lru_order.begin(), m_lru_order.end(), cache_key);
    if (it != m_lru_order.end()) {
        m_lru_order.erase(it);
    }
    // Add to end (most recently used)
    m_lru_order.push_back(cache_key);
}

void VoiceInstrument::evict_lru_if_needed(size_t new_size) {
    // Check if we need to evict
    if (m_memory_used + new_size <= m_memory_limit) {
        return;
    }
    
    // Evict LRU entries until we have space
    while (!m_lru_order.empty() && m_memory_used + new_size > m_memory_limit) {
        std::string lru_key = m_lru_order.front();
        m_lru_order.pop_front();
        
        auto cache_it = m_audio_cache.find(lru_key);
        if (cache_it != m_audio_cache.end()) {
            m_memory_used -= cache_it->second->size_bytes();
            m_audio_cache.erase(cache_it);
            m_perf_metrics.evictions++;  // Count eviction
        }
    }
}


bool VoiceInstrument::apply_libsamplerate_pitch(const std::vector<float>& in_left, 
                                                const std::vector<float>& in_right,
                                                float pitch_factor,
                                                std::vector<float>& out_left,
                                                std::vector<float>& out_right) {
    out_left.clear();
    out_right.clear();
    
    if (in_left.empty()) {
        return false;
    }
    
    // Clamp pitch factor to reasonable range (widened to 0.05 - 20.0 for 5+ octaves)
    pitch_factor = std::max(0.01f, std::min(100.0f, pitch_factor));
    
    // If pitch is 1.0, just copy
    if (std::abs(pitch_factor - 1.0f) < 0.001f) {
        out_left = in_left;
        out_right = in_right;
        return true;
    }
    
    // Output length = input_length / pitch_factor
    // (higher pitch = shorter duration, lower pitch = longer duration)
    size_t out_frames = (size_t)(in_left.size() / pitch_factor);
    
    // Create SRC state for each channel
    int error = 0;
    SRC_STATE* src_l = src_new(SRC_SINC_BEST_QUALITY, 1, &error);  // Highest quality
    if (!src_l) {
        return false;
    }
    
    SRC_STATE* src_r = src_new(SRC_SINC_BEST_QUALITY, 1, &error);
    if (!src_r) {
        src_delete(src_l);
        return false;
    }
    
    // Prepare input/output data
    SRC_DATA src_data = {};
    src_data.data_in = const_cast<float*>(in_left.data());
    src_data.input_frames = in_left.size();
    src_data.src_ratio = 1.0 / pitch_factor;  // Resample ratio
    
    fprintf(stderr, "[VoiceInst] Resampling: in %zu, out %zu, ratio %.4f\n", 
            (size_t)src_data.input_frames, (size_t)out_frames, (double)src_data.src_ratio);

    // Allocate output buffer
    std::vector<float> out_buf_l(out_frames);
    std::vector<float> out_buf_r(out_frames);
    src_data.data_out = out_buf_l.data();
    src_data.output_frames = out_frames;
    
    // Process left channel
    error = src_process(src_l, &src_data);
    if (error) {
        src_delete(src_l);
        src_delete(src_r);
        return false;
    }
    
    // Process right channel
    src_data.data_in = const_cast<float*>(in_right.data());
    src_data.data_out = out_buf_r.data();
    src_data.output_frames = out_frames;
    error = src_process(src_r, &src_data);
    
    src_delete(src_l);
    src_delete(src_r);
    
    if (error) {
        return false;
    }
    
    // Copy to output
    out_left.assign(out_buf_l.begin(), out_buf_l.begin() + src_data.output_frames_gen);
    out_right.assign(out_buf_r.begin(), out_buf_r.begin() + src_data.output_frames_gen);
    
    return true;
}

void VoiceInstrument::clear_cache() {
    std::lock_guard<std::mutex> lock(m_cache_mutex);
    m_audio_cache.clear();
    m_lru_order.clear();
    m_memory_used = 0;
    m_current_text.clear();
    m_active_audio.reset();
    m_playback_pos = 0;
    m_playing = false;
}

size_t VoiceInstrument::cache_size() {
    std::lock_guard<std::mutex> lock(m_cache_mutex);
    return m_audio_cache.size();
}

size_t VoiceInstrument::get_memory_used() {
    std::lock_guard<std::mutex> lock(m_cache_mutex);
    return m_memory_used;
}

void VoiceInstrument::start_synthesis_worker() {
    if (!m_worker) {
        m_worker = new VoiceSynthesisWorker(this);
    }
    m_worker->start();
}

void VoiceInstrument::stop_synthesis_worker() {
    if (m_worker) {
        m_worker->stop();
        delete m_worker;
        m_worker = nullptr;
    }
}

std::vector<std::pair<std::string,std::string>> VoiceInstrument::list_espeak_languages() {
    // espeak_ListVoices returns the full installed voice list.
    // Each entry's `languages` field (for ListVoices) is: one byte priority + UTF-8 language string.
    // We deduplicate by language code and build a human-readable display label.
    std::vector<std::pair<std::string,std::string>> result;

    const espeak_VOICE** voices = espeak_ListVoices(nullptr);
    if (!voices) return result;

    std::set<std::string> seen;
    for (int i = 0; voices[i]; ++i) {
        const char* raw = voices[i]->languages;
        if (!raw || !*raw) continue;
        // Skip the priority byte and extract the first language entry.
        const char* lang = raw + 1;
        if (!*lang) continue;

        // Use only the primary language (before any second priority+lang pair).
        std::string lcode(lang);
        // Some entries list multiple languages; take up to the first \x00 or next priority byte.
        // The string is null-terminated, so lcode is already correct.

        if (seen.count(lcode)) continue;
        seen.insert(lcode);

        // Build a display name: voice name if available, otherwise just the language code.
        std::string display = lcode;
        if (voices[i]->name && *voices[i]->name)
            display = std::string(voices[i]->name) + " (" + lcode + ")";

        result.push_back({lcode, display});
    }

    // Sort by language code for a predictable order.
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    return result;
}

} // namespace tpanar_ns
