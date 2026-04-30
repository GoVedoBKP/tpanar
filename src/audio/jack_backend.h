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

#include "audio_backend.h"
#include <jack/jack.h>
#include <vector>

namespace tpanar_ns
{

class Engine;

class JackBackend : public AudioBackend
{
public:
    explicit JackBackend(Engine *engine, 
                         uint32_t num_ins = 2, uint32_t num_outs = 2,
                         uint32_t num_midi_ins = 1, uint32_t num_midi_outs = 1);
    ~JackBackend();

    bool start() override;
    void stop() override;
    bool is_active() const override;

    uint32_t sample_rate() const override;
    uint32_t buffer_size() const override;
    AudioBackendType type() const override { return AudioBackendType::Jack; }

private:
    static int process_callback(jack_nframes_t nframes, void *arg);

    int process(jack_nframes_t nframes);

    Engine *m_engine;

    jack_client_t *m_client;
    
    uint32_t m_num_ins;
    uint32_t m_num_outs;
    uint32_t m_num_midi_ins;
    uint32_t m_num_midi_outs;

    std::vector<jack_port_t*> m_input_ports;
    std::vector<jack_port_t*> m_output_ports;
    std::vector<jack_port_t*> m_midi_input_ports;
    std::vector<jack_port_t*> m_midi_output_ports;

    // Pre-allocated buffer pointer arrays — reused every callback to avoid
    // dynamic memory allocation in the real-time audio thread.
    std::vector<float*> m_out_bufs_rt;
    std::vector<float*> m_in_bufs_rt;
};

} // namespace tpanar_ns
