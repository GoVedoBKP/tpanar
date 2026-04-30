/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "midi_instrument.h"
#include "../core/engine.h"
#include <algorithm>

namespace tpanar_ns {

    void MidiInstrument::note_on(uint8_t note, uint8_t velocity, size_t column_index, size_t, uint8_t)
 {
    if (!m_engine) return;
    
    // Stop last note on same column
    note_off(column_index);

    m_last_note[column_index % 16] = note;

    MidiMessage msg;
    msg.status = 0x90 | (m_channel & 0x0F);
    msg.data1 = note & 0x7F;
    msg.data2 = velocity & 0x7F;
    m_engine->m_midi_out_queue.push(msg);
}

void MidiInstrument::note_off(size_t column_index) {
    if (!m_engine) return;

    MidiMessage msg;
    msg.status = 0x80 | (m_channel & 0x0F);
    msg.data1 = m_last_note[column_index % 16] & 0x7F;
    msg.data2 = 0;
    m_engine->m_midi_out_queue.push(msg);
}

void MidiInstrument::panic() {
    if (!m_engine) return;
    MidiMessage msg;
    msg.status = 0xB0 | (m_channel & 0x0F);
    msg.data1 = 123; // All Notes Off
    msg.data2 = 0;
    m_engine->m_midi_out_queue.push(msg);
}

void MidiInstrument::set_volume(float vol) {
    if (!m_engine) return;
    MidiMessage msg;
    msg.status = 0xB0 | (m_channel & 0x0F);
    msg.data1 = 7; // Main Volume CC
    msg.data2 = static_cast<uint8_t>(vol * 127.0f) & 0x7F;
    m_engine->m_midi_out_queue.push(msg);
}

void MidiInstrument::set_pitch(float freq) {
    // Standard MIDI uses Pitch Bend for frequency control.
}

void MidiInstrument::process(float* l, float* r, size_t nframes) {
    // MIDI instruments don't generate audio themselves.
    // Audio inputs (if attached to the track) are mixed in by the Track after this call.
    // We output silence; the track will handle audio input routing.
    std::fill(l, l + nframes, 0.0f);
    std::fill(r, r + nframes, 0.0f);
}

void MidiInstrument::set_audio_input(int channel_l, int channel_r) {
    m_audio_input_l = channel_l;
    m_audio_input_r = channel_r;
}

void MidiInstrument::get_audio_input(int& channel_l, int& channel_r) const {
    channel_l = m_audio_input_l;
    channel_r = m_audio_input_r;
}

void MidiInstrument::set_channel(int ch) {
    m_channel = std::max(0, std::min(15, ch));
}

void MidiInstrument::set_program(int prog) {
    m_program = std::max(0, std::min(127, prog));
    if (!m_engine) return;
    MidiMessage msg;
    msg.status = 0xC0 | (m_channel & 0x0F);
    msg.data1 = m_program & 0x7F;
    msg.data2 = 0;
    m_engine->m_midi_out_queue.push(msg);
}

} // namespace tpanar_ns
