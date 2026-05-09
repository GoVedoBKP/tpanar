/*
 * TPanar - Digital Audio Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "midi_export.h"
#include "../core/engine.h"
#include "../sequencer/pattern.h"
#include "../sequencer/note.h"
#include "../mixer/track.h"

#include <fstream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <string>
#include <cstring>

namespace tpanar_ns {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void write_u16_be(std::ostream& out, uint16_t v)
{
    uint8_t buf[2] = { uint8_t(v >> 8), uint8_t(v & 0xff) };
    out.write(reinterpret_cast<char*>(buf), 2);
}

static void write_u32_be(std::ostream& out, uint32_t v)
{
    uint8_t buf[4] = { uint8_t(v >> 24), uint8_t((v >> 16) & 0xff),
                       uint8_t((v >> 8) & 0xff), uint8_t(v & 0xff) };
    out.write(reinterpret_cast<char*>(buf), 4);
}

static void write_vlq(std::ostream& out, uint32_t value)
{
    // Variable-length quantity encoding (MIDI standard)
    uint8_t buf[4];
    int n = 0;
    buf[n++] = value & 0x7f;
    while (value >>= 7)
        buf[n++] = 0x80 | (value & 0x7f);
    for (int i = n - 1; i >= 0; --i)
        out.put(static_cast<char>(buf[i]));
}

struct MidiEvent
{
    uint32_t tick = 0;
    std::vector<uint8_t> data;
};

static void write_track(std::ostream& out, std::vector<MidiEvent>& events)
{
    // Sort events by tick; stable so simultaneous events stay in insertion order
    std::stable_sort(events.begin(), events.end(),
                     [](const MidiEvent& a, const MidiEvent& b) {
                         return a.tick < b.tick;
                     });

    // Build track body
    std::string body;
    {
        std::ostringstream ss;
        uint32_t last_tick = 0;
        for (const auto& ev : events) {
            uint32_t delta = ev.tick - last_tick;
            last_tick = ev.tick;

            // Write VLQ delta to ss via temporary buffer trick
            uint8_t buf[4];
            int n = 0;
            uint32_t val = delta;
            buf[n++] = val & 0x7f;
            while (val >>= 7)
                buf[n++] = 0x80 | (val & 0x7f);
            for (int i = n - 1; i >= 0; --i)
                ss.put(static_cast<char>(buf[i]));

            ss.write(reinterpret_cast<const char*>(ev.data.data()),
                     static_cast<std::streamsize>(ev.data.size()));
        }
        body = ss.str();
    }

    // MTrk header + body
    out.write("MTrk", 4);
    write_u32_be(out, static_cast<uint32_t>(body.size()));
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
}

// Push a tempo meta-event (Set Tempo, microseconds per beat)
static void push_tempo(std::vector<MidiEvent>& events, uint32_t tick, int bpm)
{
    uint32_t us = 60000000u / static_cast<uint32_t>(bpm > 0 ? bpm : 120);
    MidiEvent ev;
    ev.tick = tick;
    ev.data = { 0xFF, 0x51, 0x03,
                uint8_t(us >> 16), uint8_t((us >> 8) & 0xff), uint8_t(us & 0xff) };
    events.push_back(std::move(ev));
}

// Push track name meta-event
static void push_track_name(std::vector<MidiEvent>& events, const std::string& name)
{
    MidiEvent ev;
    ev.tick = 0;
    ev.data.push_back(0xFF);
    ev.data.push_back(0x03);
    // VLQ length of the name
    uint32_t len = static_cast<uint32_t>(name.size());
    {
        uint8_t buf[4];
        int n = 0;
        uint32_t val = len;
        buf[n++] = val & 0x7f;
        while (val >>= 7)
            buf[n++] = 0x80 | (val & 0x7f);
        for (int i = n - 1; i >= 0; --i)
            ev.data.push_back(buf[i]);
    }
    for (char c : name) ev.data.push_back(static_cast<uint8_t>(c));
    events.push_back(std::move(ev));
}

// Push end-of-track meta-event
static void push_eot(std::vector<MidiEvent>& events, uint32_t tick)
{
    MidiEvent ev;
    ev.tick = tick;
    ev.data = { 0xFF, 0x2F, 0x00 };
    events.push_back(std::move(ev));
}

// ---------------------------------------------------------------------------
// Main export function
// ---------------------------------------------------------------------------

bool export_midi(const Engine& engine, const std::string& path)
{
    // MIDI resolution: ticks per quarter note (beat)
    const uint32_t TPQ = 480;

    const int bpm = static_cast<int>(engine.tempo());
    const uint32_t lpb = engine.lpb();                       // lines per beat
    const uint32_t ticks_per_row = (lpb > 0) ? (TPQ / lpb) : TPQ;

    // Collect indices of notation tracks (skip Audio / Pilot)
    std::vector<size_t> note_track_indices;
    for (size_t ti = 0; ti < engine.track_count(); ++ti) {
        const auto& t = engine.track(ti);
        if (t.kind() == TrackKind::Note)
            note_track_indices.push_back(ti);
    }

    const size_t num_midi_tracks = 1 + note_track_indices.size(); // +1 tempo track

    // -----------------------------------------------------------------------
    // Assign MIDI channels:
    // - Drum tracks (DrumType != None) → channel 9 (0-based)
    // - Others → channels 0-8, 10-15 cycling through available channels
    // Each note track gets one channel; columns > 0 share the same channel
    // (tracker columns are voice-polyphony, not separate instruments).
    // -----------------------------------------------------------------------
    auto assign_channel = [&](size_t track_idx) -> uint8_t {
        const auto& t = engine.track(track_idx);
        if (t.drum_type() != DrumType::None)
            return 9; // GM drums
        // Assign sequential non-drum channels
        uint8_t ch = 0;
        int drums_before = 0;
        for (size_t ti : note_track_indices) {
            if (ti == track_idx) break;
            if (engine.track(ti).drum_type() != DrumType::None)
                ++drums_before;
            else
                ++ch;
        }
        // Skip channel 9 (reserved for drums)
        if (ch >= 9) ++ch;
        return ch % 16;
    };

    // -----------------------------------------------------------------------
    // Walk the order list and accumulate events per note track
    // -----------------------------------------------------------------------

    // Per-track event lists (index corresponds to note_track_indices)
    std::vector<std::vector<MidiEvent>> track_events(note_track_indices.size());
    std::vector<MidiEvent> tempo_events;

    // We also need a tempo track
    push_tempo(tempo_events, 0, bpm);

    // Current absolute tick position
    uint32_t abs_tick = 0;

    // Pending note-offs: [track_events_idx][column] → {note, tick}
    struct PendingOff { uint8_t note = 255; uint32_t tick = 0; };
    const size_t MAX_COLS_EXPORT = MAX_COLS;
    std::vector<std::vector<PendingOff>> pending_offs(note_track_indices.size(),
                                                      std::vector<PendingOff>(MAX_COLS_EXPORT));

    // Current BPM (may be changed by SetTempo effects)
    int current_bpm = bpm;

    for (size_t order_pos = 0; order_pos < engine.m_order.size(); ++order_pos) {
        size_t pat_idx = engine.m_order[order_pos];
        if (pat_idx >= engine.pattern_count()) continue;
        const Pattern& pat = engine.pattern(pat_idx);
        const size_t rows = pat.row_count();

        for (size_t row = 0; row < rows; ++row) {
            const uint32_t row_tick = abs_tick;

            // Scan all notation tracks
            for (size_t tei = 0; tei < note_track_indices.size(); ++tei) {
                size_t ti = note_track_indices[tei];
                if (ti >= pat.track_count()) continue;
                auto& evlist = track_events[tei];
                const uint8_t channel = assign_channel(ti);
                const size_t ncols = pat.column_count(ti);

                for (size_t col = 0; col < ncols && col < MAX_COLS_EXPORT; ++col) {
                    const TrackEvent& te = pat.event(ti, row, col);

                    // --- Effects: SetTempo (before note processing) ---
                    auto handle_effect = [&](uint8_t eff, uint8_t param) {
                        if (eff == static_cast<uint8_t>(EffectType::SetTempo) && param > 0) {
                            current_bpm = param;
                            push_tempo(tempo_events, row_tick, current_bpm);
                        } else if (eff == static_cast<uint8_t>(EffectType::Volume)) {
                            // CC7 (main volume)
                            MidiEvent ve;
                            ve.tick = row_tick;
                            ve.data = { uint8_t(0xB0 | channel), 7, param };
                            evlist.push_back(ve);
                        } else if (eff == static_cast<uint8_t>(EffectType::NoteCut) && param > 0) {
                            // Schedule note-off at tick + param ticks into this row
                            auto& poff = pending_offs[tei][col];
                            if (poff.note != 255) {
                                uint32_t cut_tick = row_tick + param;
                                MidiEvent noff;
                                noff.tick = cut_tick;
                                noff.data = { uint8_t(0x80 | channel), poff.note, 0 };
                                evlist.push_back(noff);
                                poff.note = 255;
                            }
                        }
                    };
                    handle_effect(te.effect1, te.param1);
                    handle_effect(te.effect2, te.param2);

                    // --- Note event ---
                    if (te.note != NOTE_EMPTY) {
                        // Close any pending note-off for this column first
                        auto& poff = pending_offs[tei][col];
                        if (poff.note != 255) {
                            // Note off exactly at the start of this row (1 tick before would be nicer but same tick is fine)
                            MidiEvent noff;
                            noff.tick = row_tick;
                            noff.data = { uint8_t(0x80 | channel), poff.note, 0 };
                            evlist.push_back(noff);
                            poff.note = 255;
                        }

                        uint8_t velocity = (te.volume < 128) ? te.volume : 100;
                        MidiEvent non;
                        non.tick = row_tick;
                        non.data = { uint8_t(0x90 | channel), te.note, velocity };
                        evlist.push_back(non);

                        // Schedule note-off 1 tick before the next row
                        poff.note = te.note;
                        poff.tick = row_tick + ticks_per_row - 1;
                    }
                }
            }

            abs_tick += ticks_per_row;
        }
    }

    // Flush all pending note-offs at their scheduled tick (or end)
    for (size_t tei = 0; tei < note_track_indices.size(); ++tei) {
        size_t ti = note_track_indices[tei];
        const uint8_t channel = assign_channel(ti);
        for (size_t col = 0; col < MAX_COLS_EXPORT; ++col) {
            auto& poff = pending_offs[tei][col];
            if (poff.note != 255) {
                MidiEvent noff;
                noff.tick = poff.tick;
                noff.data = { uint8_t(0x80 | channel), poff.note, 0 };
                track_events[tei].push_back(noff);
                poff.note = 255;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Write the SMF file
    // -----------------------------------------------------------------------
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    // MThd header
    out.write("MThd", 4);
    write_u32_be(out, 6);                                     // chunk length
    write_u16_be(out, 1);                                     // format 1 (multi-track)
    write_u16_be(out, static_cast<uint16_t>(num_midi_tracks));
    write_u16_be(out, static_cast<uint16_t>(TPQ));            // ticks per quarter

    // Track 0: tempo track
    push_eot(tempo_events, abs_tick);
    write_track(out, tempo_events);

    // Tracks 1..N: notation tracks
    for (size_t tei = 0; tei < note_track_indices.size(); ++tei) {
        const std::string& name = engine.track(note_track_indices[tei]).name();
        push_track_name(track_events[tei], name);
        push_eot(track_events[tei], abs_tick);
        write_track(out, track_events[tei]);
    }

    return out.good();
}

} // namespace tpanar_ns
