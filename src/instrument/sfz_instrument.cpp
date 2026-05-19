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

#include "sfz_instrument.h"
#include "../io/audio_file.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace tpanar_ns
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s)
{
    size_t l = s.find_first_not_of(" \t\r\n");
    size_t r = s.find_last_not_of(" \t\r\n");
    return (l == std::string::npos) ? "" : s.substr(l, r - l + 1);
}

static std::string to_lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Parse key name ("c4", "c#4", "60") → MIDI note number
int SfzInstrument::parse_key(const std::string& raw)
{
    if (raw.empty()) return 60;
    // Numeric
    if (std::isdigit((unsigned char)raw[0]) || raw[0] == '-') {
        try { return std::stoi(raw); } catch (...) { return 60; }
    }
    // Note name: [A-G][#b]?[0-9]
    static const int semitones[] = {9,11,0,2,4,5,7}; // A B C D E F G
    char letter = (char)std::toupper((unsigned char)raw[0]);
    if (letter < 'A' || letter > 'G') return 60;
    int semi = semitones[letter - 'A'];
    int pos = 1;
    if (pos < (int)raw.size() && raw[pos] == '#') { semi++; pos++; }
    else if (pos < (int)raw.size() && raw[pos] == 'b') { semi--; pos++; }
    int octave = 4;
    if (pos < (int)raw.size()) {
        try { octave = std::stoi(raw.substr(pos)); } catch (...) {}
    }
    return (octave + 1) * 12 + semi;
}

float SfzInstrument::midi_note_to_hz(int note)
{
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

// ---------------------------------------------------------------------------
// SfzInstrument
// ---------------------------------------------------------------------------

SfzInstrument::SfzInstrument(double sample_rate)
    : m_sample_rate(sample_rate)
{
    m_type = InstrumentType::SFZ;
}

bool SfzInstrument::load_sfz(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "SFZ: cannot open " << path << "\n";
        return false;
    }

    m_path    = path;
    m_sfz_dir = fs::path(path).parent_path().string();
    m_regions.clear();
    m_group_names.clear();
    m_selected_group = -1;

    // -----------------------------------------------------------------------
    // Tokenise: read full text, strip // and /* */ comments
    // -----------------------------------------------------------------------
    std::string full, line;
    while (std::getline(f, line)) {
        // Strip // comments
        auto cpos = line.find("//");
        if (cpos != std::string::npos) line.resize(cpos);
        full += " " + line;
    }
    // Strip /* */ block comments
    for (;;) {
        auto s = full.find("/*");
        if (s == std::string::npos) break;
        auto e = full.find("*/", s + 2);
        if (e == std::string::npos) { full.resize(s); break; }
        full.erase(s, e - s + 2);
    }

    // -----------------------------------------------------------------------
    // State machine: accumulate opcodes per region/group
    // -----------------------------------------------------------------------
    SfzRegion group_defaults;  // accumulated from <group>
    SfzRegion current_region;
    bool in_region = false;
    int  group_idx = -1;       // current group index (-1 = ungrouped)

    // We process tokens split by '<' and '>'
    // Replace < and > with sentinel to split
    std::string text = full;
    // Normalise: replace '=' with ' = ' to simplify opcode parsing later
    // We'll parse by finding headers <...> then key=value pairs

    size_t pos = 0;
    auto skip_ws = [&]() {
        while (pos < text.size() && std::isspace((unsigned char)text[pos])) pos++;
    };

    auto read_header = [&]() -> std::string { // reads up to next '>'
        size_t end = text.find('>', pos);
        if (end == std::string::npos) { pos = text.size(); return ""; }
        std::string h = trim(to_lower(text.substr(pos, end - pos)));
        pos = end + 1;
        return h;
    };

    // Collect all opcodes between current pos and the next '<' or end
    auto collect_opcodes = [&](SfzRegion& reg) {
        size_t next_hdr = text.find('<', pos);
        size_t end      = (next_hdr == std::string::npos) ? text.size() : next_hdr;
        std::string block = text.substr(pos, end - pos);
        pos = end;

        // Parse key=value pairs
        // Tokenise around '=' signs: find tokens and values
        std::istringstream ss(block);
        std::string token;
        while (ss >> token) {
            size_t eq = token.find('=');
            if (eq == std::string::npos) continue; // not a key=value pair at first glance

            std::string key = to_lower(token.substr(0, eq));
            std::string val;
            if (eq + 1 < token.size()) {
                val = token.substr(eq + 1);
            } else {
                // Value is the next token
                if (!(ss >> val)) continue;
            }

            // Some values can contain paths with spaces; sample= is a special case
            // We handle it below by checking for sample= specifically
            if (key == "sample") {
                // Path may contain spaces — read until next known opcode pattern
                // Heuristic: sample value ends at next whitespace-then-word-with-equals or '<'
                // We'll re-scan from the raw block for the sample= value
                auto spos = block.find("sample=");
                if (spos == std::string::npos) spos = block.find("Sample=");
                if (spos != std::string::npos) {
                    spos += 7; // skip "sample="
                    // Find end of value: next <, or next opcode (word=), or end
                    size_t vend = spos;
                    while (vend < block.size()) {
                        // check if we hit a pattern " word="
                        if (std::isspace((unsigned char)block[vend])) {
                            size_t j = vend + 1;
                            while (j < block.size() && std::isspace((unsigned char)block[j])) j++;
                            // Is the next non-space char the start of an opcode key=?
                            size_t ke = block.find('=', j);
                            size_t sp = block.find(' ', j);
                            size_t lt = block.find('<', j);
                            if (ke != std::string::npos && (sp == std::string::npos || ke < sp) &&
                                (lt == std::string::npos || ke < lt)) {
                                break;
                            }
                        }
                        if (block[vend] == '<') break;
                        vend++;
                    }
                    val = trim(block.substr(spos, vend - spos));
                    // Normalize path separators
                    std::replace(val.begin(), val.end(), '\\', '/');
                }
                reg.sample = val;
                continue;
            }

            auto fval = [&]() -> float {
                try { return std::stof(val); } catch (...) { return 0.0f; }
            };
            auto ival = [&]() -> int {
                try { return std::stoi(val); } catch (...) { return 0; }
            };

            if      (key == "lokey")           reg.lokey           = parse_key(val);
            else if (key == "hikey")           reg.hikey           = parse_key(val);
            else if (key == "key") {
                int k = parse_key(val);
                reg.lokey = reg.hikey = reg.pitch_keycenter = k;
            }
            else if (key == "pitch_keycenter") reg.pitch_keycenter = parse_key(val);
            else if (key == "lovel")           reg.lovel           = ival();
            else if (key == "hivel")           reg.hivel           = ival();
            else if (key == "seq_length")      reg.seq_length      = std::max(0, ival());
            else if (key == "seq_position")    reg.seq_position    = std::max(0, ival());
            else if (key == "volume")          reg.volume          = fval();
            else if (key == "tune")            reg.tune            = fval();
            else if (key == "transpose")       reg.transpose       = ival();
            else if (key == "loop_mode") {
                std::string lm = to_lower(val);
                reg.loop_enabled = (lm == "loop_continuous" || lm == "loop_sustain");
            }
            else if (key == "loop_start")      reg.loop_start      = (int64_t)fval();
            else if (key == "loop_end")        reg.loop_end        = (int64_t)fval();
            else if (key == "ampeg_attack")    reg.ampeg_attack    = std::max(0.001f, fval());
            else if (key == "ampeg_decay")     reg.ampeg_decay     = std::max(0.001f, fval());
            else if (key == "ampeg_sustain")   reg.ampeg_sustain   = fval() / 100.0f; // SFZ uses 0-100
            else if (key == "ampeg_release")   reg.ampeg_release   = std::max(0.001f, fval());
        }
    };

    // -----------------------------------------------------------------------
    // Main parsing loop
    // -----------------------------------------------------------------------
    auto flush_region = [&]() {
        if (in_region && !current_region.sample.empty()) {
            // Resolve sample path
            fs::path sp = fs::path(m_sfz_dir) / current_region.sample;
            // Load audio
            auto data = std::make_shared<SampleData>();
            uint32_t sr = (uint32_t)m_sample_rate;
            data->sample_rate = (int)m_sample_rate;
            if (AudioFile::load_audio(sp.string(), data->left, data->right, sr)
                && !data->left.empty()) {
                data->sample_rate = (int)sr;
                current_region.data = data;
            } else {
                std::cerr << "SFZ: could not load sample: " << sp << "\n";
            }
            current_region.data = current_region.data; // already set above
            m_regions.push_back(current_region);
        }
        in_region = false;
    };

    while (pos < text.size()) {
        skip_ws();
        if (pos >= text.size()) break;
        if (text[pos] == '<') {
            pos++;
            std::string hdr = read_header();
            if (hdr == "group") {
                flush_region();
                group_defaults = SfzRegion();
                collect_opcodes(group_defaults);
                group_idx = (int)m_group_names.size();
                m_group_names.push_back("Group " + std::to_string(group_idx + 1));
            } else if (hdr == "region") {
                flush_region();
                current_region = group_defaults; // inherit group defaults
                collect_opcodes(current_region);
                in_region = true;
            } else if (hdr == "control" || hdr == "curve" || hdr == "effect" || hdr == "master") {
                // Skip unknown sections
                size_t next = text.find('<', pos);
                pos = (next == std::string::npos) ? text.size() : next;
            } else {
                // Unknown header — skip its content
                size_t next = text.find('<', pos);
                pos = (next == std::string::npos) ? text.size() : next;
            }
        } else {
            pos++;
        }
    }
    flush_region();

    if (m_group_names.empty() && !m_regions.empty())
        m_group_names.push_back("(default)");

    std::cout << "SFZ: loaded " << m_regions.size() << " regions from " << path << "\n";
    return !m_regions.empty();
}

void SfzInstrument::set_group(int idx)
{
    m_selected_group = idx;
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void SfzInstrument::note_on(uint8_t note, uint8_t velocity,
                             size_t column_index, size_t offset_samples, uint8_t)
{
    // Stop any previous note on the same column (tracker: one note per column)
    for (auto& sv : m_voices) {
        if (sv.column == (int)column_index) sv.voice->panic();
    }

    float note_hz     = midi_note_to_hz(note);
    float vel_f       = velocity / 127.0f;
    const uint32_t sequence_slot = m_sequence_counter++;

    for (const auto& reg : m_regions) {
        if (note < reg.lokey || note > reg.hikey) continue;
        if (velocity < reg.lovel || velocity > reg.hivel) continue;
        if (reg.seq_length > 0 && reg.seq_position > 0) {
            if ((int)(sequence_slot % (uint32_t)reg.seq_length) + 1 != reg.seq_position) continue;
        }
        if (!reg.data) continue;

        // Pitch: note_hz / keycenter_hz * 440 → SampleVoice divides by 440 → correct ratio
        float keycenter_hz = midi_note_to_hz(reg.pitch_keycenter + reg.transpose);
        // Apply fine tune (cents)
        float tune_factor = std::pow(2.0f, reg.tune / 1200.0f);
        float start_freq  = note_hz / keycenter_hz * 440.0f * tune_factor;

        // Gain from volume opcode (dB)
        float gain = std::pow(10.0f, reg.volume / 20.0f) * m_volume * vel_f;

        auto sv = std::make_unique<SampleVoice>(reg.data, m_sample_rate);
        sv->set_adsr(reg.ampeg_attack, reg.ampeg_decay, reg.ampeg_sustain, reg.ampeg_release);
        sv->set_volume(gain);

        size_t end_pos = (reg.loop_end > 0) ? (size_t)reg.loop_end : 0;
        sv->set_region(end_pos, reg.loop_enabled, (size_t)reg.loop_start);
        sv->start(note, velocity, start_freq, offset_samples);

        // Voice-steal if at limit
        if (m_voices.size() >= MAX_VOICES) {
            // Remove oldest inactive voice, or the oldest active one
            auto it = std::find_if(m_voices.begin(), m_voices.end(),
                [](const SfzVoice& v){ return !v.voice->active(); });
            if (it == m_voices.end()) it = m_voices.begin();
            m_voices.erase(it);
        }

        m_voices.push_back({ std::move(sv), (int)column_index, note });
    }
}

void SfzInstrument::note_off(size_t column_index)
{
    for (auto& sv : m_voices) {
        if (sv.column == (int)column_index) sv.voice->stop();
    }
}

void SfzInstrument::panic()
{
    for (auto& sv : m_voices) sv.voice->panic();
    m_voices.clear();
}

void SfzInstrument::set_volume(float vol)
{
}

void SfzInstrument::process(float* l, float* r, size_t nframes)
{
    // Render all active voices
    for (auto& sv : m_voices) {
        if (sv.voice->active()) {
            sv.voice->process(l, r, nframes);
        }
    }
    // Prune finished voices
    m_voices.erase(
        std::remove_if(m_voices.begin(), m_voices.end(),
                       [](const SfzVoice& v){ return !v.voice->active(); }),
        m_voices.end());
}

} // namespace tpanar_ns
