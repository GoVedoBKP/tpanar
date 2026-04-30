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
#include "../audio/sample_voice.h"
#include "../audio/sample_data.h"
#include <vector>
#include <string>
#include <memory>
#include <mutex>

namespace tpanar_ns
{

// One parsed SFZ region (group defaults merged in)
struct SfzRegion {
    std::string sample;           // relative path to WAV

    int lokey          = 0;
    int hikey          = 127;
    int pitch_keycenter = 60;     // root MIDI note of sample

    int lovel = 0;
    int hivel = 127;

    float volume    = 0.0f;       // dB gain (0 = unity)
    float tune      = 0.0f;       // cents offset
    int   transpose = 0;          // semitone offset

    // Loop
    bool    loop_enabled = false;
    int64_t loop_start   = 0;
    int64_t loop_end     = 0;     // 0 = natural sample end

    // ADSR (seconds / 0–1)
    float ampeg_attack  = 0.005f;
    float ampeg_decay   = 0.05f;
    float ampeg_sustain = 0.9f;
    float ampeg_release = 0.2f;

    // Loaded sample data (shared across polyphonic voices)
    std::shared_ptr<SampleData> data;
};

// A polyphonic SFZ voice: one SampleVoice playing a specific region
struct SfzVoice {
    std::unique_ptr<SampleVoice> voice;
    int column = 0;
    uint8_t note = 0;
};

class SfzInstrument : public tpanar_ns::Instrument
{
public:
    explicit SfzInstrument(double sample_rate);
    ~SfzInstrument() override = default;

    void note_on(uint8_t note, uint8_t velocity, size_t column_index = 0,
                 size_t offset_samples = 0, uint8_t sample_index = 0) override;
    void note_off(size_t column_index = 0) override;
    void panic() override;
    void set_volume(float vol) override;
    float get_volume() const { return m_volume; }
    void set_pitch(float freq) override {}
    void process(float* l, float* r, size_t nframes) override;

    bool load_sfz(const std::string& path);
    const std::string& path() const { return m_path; }

    // Preset list: one entry per group name or index
    const std::vector<std::string>& group_names() const { return m_group_names; }
    int current_group() const { return m_selected_group; }
    void set_group(int idx);   // -1 = all regions

protected:
    std::unique_ptr<Voice> create_voice() override { return nullptr; }

private:
    // Parse helpers
    static int parse_key(const std::string& val);
    static float midi_note_to_hz(int note);

    double m_sample_rate;
    std::string m_path;         // absolute path to .sfz file
    std::string m_sfz_dir;      // directory containing the .sfz (for relative sample paths)

    std::vector<SfzRegion>  m_regions;
    std::vector<std::string> m_group_names; // one entry per <group> or named region group
    int m_selected_group = -1;  // -1 = all groups

    std::vector<SfzVoice> m_voices; // active polyphonic voices
    static constexpr size_t MAX_VOICES = 48;

    float m_volume = 1.0f;
};

} // namespace tpanar_ns
