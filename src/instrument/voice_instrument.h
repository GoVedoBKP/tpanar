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
#include "instrument.h"
#include <map>
#include <vector>
#include <memory>
#include <list>
#include <mutex>

namespace tpanar_ns {

class Engine;
class VoiceSynthesisWorker;

enum class TTSMode {
    RealTimeEspeak,  // Fast, ~100-200ms per phrase
    OfflineFestival  // Better quality, ~500ms-2s per phrase
};

class VoiceInstrument : public Instrument {
public:
    VoiceInstrument(Engine* engine = nullptr);
    ~VoiceInstrument() override;

    void note_on(uint8_t note, uint8_t velocity, size_t column_index = 0, size_t offset_samples = 0, uint8_t sample_index = 0) override;
    void note_off(size_t column_index = 0) override;
    void panic() override;
    void set_volume(float vol) override;
    void set_pitch(float freq) override;
    void process(float* l, float* r, size_t nframes) override;

    // Voice-specific methods
    void set_text(const std::string& text, uint8_t phrase_index = 0);
    std::string get_text(uint8_t phrase_index = 0) const;
    
    void set_tts_mode(TTSMode mode) { m_tts_mode = mode; }
    TTSMode tts_mode() const { return m_tts_mode; }
    
    // Language / gender selection
    void set_language(const std::string& lang) { m_language = lang; }
    const std::string& get_language() const { return m_language; }
    
    void set_gender(int gender) { m_gender = std::max(0, std::min(2, gender)); } // 0=default,1=male,2=female
    int  get_gender() const { return m_gender; }
    
    // Variant selects a specific voice variant when multiple match the language+gender.
    void set_variant(int v) { m_variant = std::max(0, std::min(10, v)); }
    int  get_variant() const { return m_variant; }
    
    // Return a deduplicated list of {language-code, display-name} pairs from
    // installed espeak-ng voices.  Populated once on first call.
    static std::vector<std::pair<std::string,std::string>> list_espeak_languages();
    
    void set_speed(float speed) { m_speed = std::max(0.5f, std::min(2.0f, speed)); }  // 0.5x to 2.0x
    float get_speed() const { return m_speed; }
    
    void set_pitch_accent(float accent) { m_pitch_accent = std::max(0.0f, std::min(1.0f, accent)); }  // 0.0 to 1.0
    float get_pitch_accent() const { return m_pitch_accent; }
    
    // Synthesis
    bool synthesize_text(const std::string& text, float base_freq, bool update_active = true);
    void clear_cache();
    size_t cache_size();
    size_t get_memory_used();
    
    // Memory limit with LRU eviction
    void set_memory_limit(size_t bytes) { m_memory_limit = bytes; }
    size_t get_memory_limit() const { return m_memory_limit; }
    
    // Disk cache for persistence across sessions
    void set_cache_dir(const std::string& dir) { m_cache_dir = dir; }
    std::string get_cache_dir() const { return m_cache_dir; }
    
    // Performance metrics
    struct PerfMetrics {
        size_t total_synthesized = 0;  // Total TTS synthesis calls
        size_t cache_hits = 0;          // Memory cache hits
        size_t disk_cache_hits = 0;     // Disk cache hits
        size_t total_lookups = 0;       // Total cache lookups
        size_t evictions = 0;           // LRU evictions
        
        double cache_hit_rate() const {
            if (total_lookups == 0) return 0.0;
            return 100.0 * (cache_hits + disk_cache_hits) / total_lookups;
        }
    };
    
    const PerfMetrics& get_perf_metrics() const { return m_perf_metrics; }
    void reset_perf_metrics() { m_perf_metrics = PerfMetrics(); }
    
    // Worker thread for batch synthesis
    VoiceSynthesisWorker* get_worker() { return m_worker; }
    void start_synthesis_worker();
    void stop_synthesis_worker();

protected:
    std::unique_ptr<Voice> create_voice() override { return nullptr; }

private:
    Engine* m_engine;
    TTSMode m_tts_mode = TTSMode::RealTimeEspeak;
    float m_volume = 1.0f;
    float m_current_pitch = 1.0f;
    
    // TTS voice selection
    std::string m_language = "en";   // espeak-ng language code, e.g. "en", "de", "fr"
    int m_gender  = 0;               // 0=default, 1=male, 2=female (maps to espeak_VOICE.gender)
    int m_variant = 0;               // variant index within matching voices (0=first match)
    float m_speed = 1.0f;            // 0.5–2.0 playback speed
    float m_pitch_accent = 0.5f;     // 0.0–1.0 pitch accent (for Festival)
    
    // Worker thread (raw pointer, created/destroyed in start/stop)
    VoiceSynthesisWorker* m_worker = nullptr;
    
    // Phrase storage (up to 256 phrases)
    std::array<std::string, 256> m_phrases;
    
    // Audio cache: "{text}@{pitch_factor}" → {samples_left, samples_right}
    // Using pitch in cache key allows pre-shifted audio to be reused
    struct CachedAudio {
        std::vector<float> left;
        std::vector<float> right;
        
        // Get size in bytes
        size_t size_bytes() const {
            return (left.size() + right.size()) * sizeof(float);
        }
    };
    std::map<std::string, std::shared_ptr<CachedAudio>> m_audio_cache;
    std::list<std::string> m_lru_order;  // LRU ordering (oldest at front)
    size_t m_memory_used = 0;
    size_t m_memory_limit = 50 * 1024 * 1024;  // 50 MB default
    
    mutable std::mutex m_cache_mutex;
    
    // Memory management with LRU eviction
    void update_lru(const std::string& cache_key);
    void evict_lru_if_needed(size_t new_size);
    
    // Helper to generate cache key including pitch
    std::string make_cache_key(const std::string& text, float pitch_factor) const;
    
    // Disk cache helpers
    std::string get_cache_file_path(const std::string& cache_key) const;
    bool load_from_disk_cache(const std::string& cache_key, std::shared_ptr<CachedAudio>& out_audio);
    bool save_to_disk_cache(const std::string& cache_key, std::shared_ptr<CachedAudio> audio);
    
    // High-quality pitch shifting using libsamplerate
    bool apply_libsamplerate_pitch(const std::vector<float>& in_left, 
                                   const std::vector<float>& in_right,
                                   float pitch_factor,
                                   std::vector<float>& out_left,
                                   std::vector<float>& out_right);
    
    // Playback state
    std::string m_current_text;
    std::shared_ptr<CachedAudio> m_active_audio;
    double m_playback_pos = 0.0;
    double m_playback_increment = 1.0;
    bool m_playing = false;
    int m_last_note[16]{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::string m_cache_dir;  // Disk cache directory
    
    // Performance metrics
    PerfMetrics m_perf_metrics;
    
    // TTS synthesis helpers
    bool synthesize_with_espeak(const std::string& text, std::vector<float>& out_l, std::vector<float>& out_r, int& out_rate);
    bool synthesize_with_festival(const std::string& text, std::vector<float>& out_l, std::vector<float>& out_r, int& out_rate);
    bool load_wav_from_file(const std::string& filepath, std::vector<float>& out_l, std::vector<float>& out_r);
};

} // namespace tpanar_ns
