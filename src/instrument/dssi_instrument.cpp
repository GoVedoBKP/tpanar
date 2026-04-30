/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "dssi_instrument.h"
#include <algorithm>
#include <iostream>
#include <cmath>

namespace {

float apply_sample_rate_hint(float value, const LADSPA_PortRangeHint& hint, double sample_rate)
{
    return LADSPA_IS_HINT_SAMPLE_RATE(hint.HintDescriptor) ? value * (float)sample_rate : value;
}

float interpolate_default(float lower, float upper, float t, bool logarithmic)
{
    if (logarithmic && lower > 0.0f && upper > 0.0f)
        return std::exp(std::log(lower) * (1.0f - t) + std::log(upper) * t);
    return lower * (1.0f - t) + upper * t;
}

float default_port_value(const LADSPA_PortRangeHint& hint, double sample_rate)
{
    float lower = apply_sample_rate_hint(hint.LowerBound, hint, sample_rate);
    float upper = apply_sample_rate_hint(hint.UpperBound, hint, sample_rate);
    bool logarithmic = LADSPA_IS_HINT_LOGARITHMIC(hint.HintDescriptor);

    float value = 0.0f;
    if (LADSPA_IS_HINT_DEFAULT_0(hint.HintDescriptor)) value = apply_sample_rate_hint(0.0f, hint, sample_rate);
    else if (LADSPA_IS_HINT_DEFAULT_1(hint.HintDescriptor)) value = apply_sample_rate_hint(1.0f, hint, sample_rate);
    else if (LADSPA_IS_HINT_DEFAULT_100(hint.HintDescriptor)) value = apply_sample_rate_hint(100.0f, hint, sample_rate);
    else if (LADSPA_IS_HINT_DEFAULT_440(hint.HintDescriptor)) value = apply_sample_rate_hint(440.0f, hint, sample_rate);
    else if (LADSPA_IS_HINT_DEFAULT_MINIMUM(hint.HintDescriptor)) value = lower;
    else if (LADSPA_IS_HINT_DEFAULT_MAXIMUM(hint.HintDescriptor)) value = upper;
    else if (LADSPA_IS_HINT_DEFAULT_LOW(hint.HintDescriptor)) value = interpolate_default(lower, upper, 0.25f, logarithmic);
    else if (LADSPA_IS_HINT_DEFAULT_MIDDLE(hint.HintDescriptor)) value = interpolate_default(lower, upper, 0.50f, logarithmic);
    else if (LADSPA_IS_HINT_DEFAULT_HIGH(hint.HintDescriptor)) value = interpolate_default(lower, upper, 0.75f, logarithmic);

    if (LADSPA_IS_HINT_BOUNDED_BELOW(hint.HintDescriptor))
        value = std::max(value, lower);
    if (LADSPA_IS_HINT_BOUNDED_ABOVE(hint.HintDescriptor))
        value = std::min(value, upper);
    return value;
}

} // namespace

namespace tpanar_ns {

DSSIInstrument::DSSIInstrument(double sample_rate) : m_sample_rate(sample_rate) {}

DSSIInstrument::~DSSIInstrument() {
    if (m_instance && m_descriptor) {
        m_descriptor->LADSPA_Plugin->cleanup(m_instance);
    }
    if (m_lib_handle) {
        dlclose(m_lib_handle);
    }
}

bool DSSIInstrument::load_plugin(const std::string& path, int index) {
    if (m_lib_handle) {
        if (m_instance) m_descriptor->LADSPA_Plugin->cleanup(m_instance);
        dlclose(m_lib_handle);
        m_lib_handle = nullptr;
        m_instance = nullptr;
        m_control_indices.clear();
        m_port_values.clear();
        m_audio_out_l = -1;
        m_audio_out_r = -1;
    }

    m_lib_handle = dlopen(path.c_str(), RTLD_NOW);
    if (!m_lib_handle) {
        std::cerr << "Failed to load DSSI plugin: " << dlerror() << std::endl;
        return false;
    }

    m_path = path;
    m_index = index;

    DSSI_Descriptor_Function df = (DSSI_Descriptor_Function)dlsym(m_lib_handle, "dssi_descriptor");
    if (!df) {
        std::cerr << "Not a valid DSSI plugin (missing dssi_descriptor)" << std::endl;
        return false;
    }

    m_descriptor = df(index); 
    if (!m_descriptor) {
        dlclose(m_lib_handle);
        m_lib_handle = nullptr;
        m_audio_out_l = -1;
        m_audio_out_r = -1;
        return false;
    }

    const LADSPA_Descriptor* ladspa = m_descriptor->LADSPA_Plugin;
    m_instance = ladspa->instantiate(ladspa, (unsigned long)m_sample_rate);
    if (!m_instance) {
        dlclose(m_lib_handle);
        m_lib_handle = nullptr;
        m_audio_out_l = -1;
        m_audio_out_r = -1;
        return false;
    }

    set_plugin_name(ladspa->Name);
    m_audio_out_l = -1;
    m_audio_out_r = -1;

    // Dummy buffer for all audio ports not used as primary L/R output.
    // LADSPA requires every port to be connected before run(); unconnected
    // audio input ports would cause the plugin to read from a dangling pointer.
    m_dummy_audio_buf.assign(2048, 0.0f);

    // Discover ports
    m_port_values.resize(ladspa->PortCount, 0.0f);
    for (unsigned long i = 0; i < ladspa->PortCount; ++i) {
        LADSPA_PortDescriptor d = ladspa->PortDescriptors[i];
        if (LADSPA_IS_PORT_AUDIO(d)) {
            if (LADSPA_IS_PORT_OUTPUT(d)) {
                if (m_audio_out_l == -1) {
                    m_audio_out_l = (int)i;
                } else if (m_audio_out_r == -1) {
                    m_audio_out_r = (int)i;
                } else {
                    // Extra audio output — connect to discard buffer.
                    ladspa->connect_port(m_instance, i, m_dummy_audio_buf.data());
                }
            } else {
                // Audio input port — connect to silent dummy buffer.
                ladspa->connect_port(m_instance, i, m_dummy_audio_buf.data());
            }
        } else if (LADSPA_IS_PORT_CONTROL(d) && LADSPA_IS_PORT_INPUT(d)) {
            m_control_indices.push_back((int)i);
            
            // Set default value if hint provided
            LADSPA_PortRangeHint hint = ladspa->PortRangeHints[i];
            float val = LADSPA_IS_HINT_HAS_DEFAULT(hint.HintDescriptor)
                ? default_port_value(hint, m_sample_rate)
                : 0.0f;
            m_port_values[i] = val;
            ladspa->connect_port(m_instance, i, &m_port_values[i]);
        } else if (LADSPA_IS_PORT_CONTROL(d) && LADSPA_IS_PORT_OUTPUT(d)) {
            // Control output ports also need a valid connection.
            ladspa->connect_port(m_instance, i, &m_port_values[i]);
        }
    }

    if (ladspa->activate) {
        ladspa->activate(m_instance);
    }

    // Pre-reserve MIDI event buffer to avoid heap allocation in the RT audio thread.
    m_pending_events.reserve(64);

    return true;
}

Instrument::Parameter DSSIInstrument::get_parameter(size_t index) const {
    if (index >= m_control_indices.size()) return {};
    int port_idx = m_control_indices[index];
    const LADSPA_Descriptor* ladspa = m_descriptor->LADSPA_Plugin;
    
    Instrument::Parameter p;
    p.index = (int)index;
    p.name = ladspa->PortNames[port_idx];
    p.min = ladspa->PortRangeHints[port_idx].LowerBound;
    p.max = ladspa->PortRangeHints[port_idx].UpperBound;
    p.value = m_port_values[port_idx];
    
    // Sanity check for bounds if not provided
    if (!LADSPA_IS_HINT_BOUNDED_BELOW(ladspa->PortRangeHints[port_idx].HintDescriptor)) p.min = 0.0f;
    if (!LADSPA_IS_HINT_BOUNDED_ABOVE(ladspa->PortRangeHints[port_idx].HintDescriptor)) p.max = 1.0f;
    if (p.max <= p.min) p.max = p.min + 1.0f;

    return p;
}

void DSSIInstrument::set_parameter(size_t index, float value) {
    if (index >= m_control_indices.size()) return;
    int port_idx = m_control_indices[index];
    m_port_values[port_idx] = value;
}

void DSSIInstrument::load_program(unsigned long bank, unsigned long program) {
    if (m_instance && m_descriptor && m_descriptor->select_program) {
        m_descriptor->select_program(m_instance, bank, program);
        m_bank = bank;
        m_program = program;
    }
}

    void DSSIInstrument::note_on(uint8_t note, uint8_t velocity, size_t column_index, size_t, uint8_t)
    {
        if (!m_descriptor) return;
        const size_t chan = column_index % 16;
        if (m_last_note[chan] != -1) {
            note_off(column_index);
        }
        snd_seq_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = SND_SEQ_EVENT_NOTEON;
        ev.data.note.channel = (unsigned char)chan;
        ev.data.note.note = note;
        ev.data.note.velocity = velocity;
        m_pending_events.push_back(ev);
        m_last_note[chan] = note;
    }

    void DSSIInstrument::note_off(size_t column_index)
    {
        if (!m_descriptor) return;
        const size_t chan = column_index % 16;
        if (m_last_note[chan] == -1) {
            return;
        }
        snd_seq_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = SND_SEQ_EVENT_NOTEOFF;
        ev.data.note.channel = (unsigned char)chan;
        ev.data.note.note = (unsigned char)m_last_note[chan];
        ev.data.note.velocity = 0;
        m_pending_events.push_back(ev);
        m_last_note[chan] = -1;
    }

    void DSSIInstrument::panic()
    {
        if (!m_descriptor) return;
        for (size_t chan = 0; chan < 16; ++chan) {
            snd_seq_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = SND_SEQ_EVENT_CONTROLLER;
            ev.data.control.channel = (unsigned char)chan;
            ev.data.control.param = 123; // All notes off
            ev.data.control.value = 0;
            m_pending_events.push_back(ev);
            m_last_note[chan] = -1;
        }
    }

    void DSSIInstrument::set_volume(float vol) {}
    void DSSIInstrument::set_pitch(float freq) {}

    void DSSIInstrument::process(float* l, float* r, size_t nframes) {
        if (!m_instance || !m_descriptor) {
            for(size_t i=0; i<nframes; ++i) { l[i]=0; r[i]=0; }
            return;
        }

        const LADSPA_Descriptor* ladspa = m_descriptor->LADSPA_Plugin;
        
        // Connect audio outputs
        if (m_audio_out_l != -1) ladspa->connect_port(m_instance, (unsigned long)m_audio_out_l, l);
        if (m_audio_out_r != -1) ladspa->connect_port(m_instance, (unsigned long)m_audio_out_r, r);

        if (m_descriptor->run_synth) {
            m_descriptor->run_synth(m_instance, (unsigned long)nframes, m_pending_events.data(), (unsigned long)m_pending_events.size());
        } else {
            ladspa->run(m_instance, (unsigned long)nframes);
        }
        m_pending_events.clear();

        if (m_audio_out_l != -1 && m_audio_out_r == -1) {
            for (size_t i = 0; i < nframes; ++i) r[i] = l[i];
        }
    }

} // namespace tpanar_ns
