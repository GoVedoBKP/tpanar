/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once
#include "instrument.h"
#include <dssi.h>
#include <dlfcn.h>
#include <vector>
// Note: DSSI run_synth explicitly uses snd_seq_event_t from ALSA.
// On FreeBSD, this is provided by the alsa-lib port.
#include <alsa/asoundlib.h>

namespace tpanar_ns {

class DSSIInstrument : public Instrument {
public:
    DSSIInstrument(double sample_rate);
    ~DSSIInstrument();

    void note_on(uint8_t note, uint8_t velocity, size_t column_index = 0, size_t offset_samples = 0, uint8_t sample_index = 0) override;
    void note_off(size_t column_index = 0) override;
    void panic() override;
    void set_volume(float vol) override;
    void set_pitch(float freq) override;
    void process(float* l, float* r, size_t nframes) override;

    bool load_plugin(const std::string& path, int index = 0);

    size_t parameter_count() const override { return m_control_indices.size(); }
    Parameter get_parameter(size_t index) const override;
    void set_parameter(size_t index, float value) override;

    void load_program(unsigned long bank, unsigned long program);

    const std::string& path() const { return m_path; }
    int index() const { return m_index; }
    unsigned long bank() const { return m_bank; }
    unsigned long program() const { return m_program; }

protected:
    std::unique_ptr<Voice> create_voice() override { return nullptr; }

private:
    void* m_lib_handle = nullptr;
    const DSSI_Descriptor* m_descriptor = nullptr;
    LADSPA_Handle m_instance = nullptr;
    double m_sample_rate;

    std::string   m_path;
    int           m_index = 0;
    unsigned long m_bank = 0;
    unsigned long m_program = 0;

    std::vector<float> m_port_values;
    std::vector<int>   m_control_indices;
    int m_audio_out_l = -1;
    int m_audio_out_r = -1;
    std::vector<snd_seq_event_t> m_pending_events;
    int m_last_note[16]{-1, -1, -1, -1, -1, -1, -1, -1,
                        -1, -1, -1, -1, -1, -1, -1, -1};
    // Dummy zero buffer for unconnected LADSPA audio input/output ports.
    std::vector<float> m_dummy_audio_buf;
};

} // namespace tpanar_ns
