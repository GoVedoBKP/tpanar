/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once
#include "instrument.h"

namespace tpanar_ns {

class Engine;

class MidiInstrument : public Instrument {
public:
    MidiInstrument(Engine* engine) : m_engine(engine), m_channel(0), m_program(0) {}

    void note_on(uint8_t note, uint8_t velocity, size_t column_index = 0, size_t offset_samples = 0, uint8_t sample_index = 0) override;
    void note_off(size_t column_index = 0) override;
    void panic() override;
    void set_volume(float vol) override;
    void set_pitch(float freq) override;
    void process(float* l, float* r, size_t nframes) override;

    void set_channel(int ch);
    int channel() const { return m_channel; }

    void set_program(int prog);
    int program() const { return m_program; }

    // Audio Input support (for routing external audio through MIDI instrument bus)
    void set_audio_input(int channel_l, int channel_r);
    void get_audio_input(int& channel_l, int& channel_r) const;

protected:
    std::unique_ptr<Voice> create_voice() override { return nullptr; }

private:
    Engine* m_engine;
    int m_channel;
    int m_program;
    uint8_t m_last_note[16] = {0};
    
    // Audio Input
    int m_audio_input_l = -1;
    int m_audio_input_r = -1;
};

} // namespace tpanar_ns
