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
#include <atomic>
#include <thread>
#include <vector>

namespace tpanar_ns
{

class Engine;

class OssBackend : public AudioBackend
{
public:
    explicit OssBackend(Engine *engine,
                        uint32_t num_ins = 2, uint32_t num_outs = 2,
                        uint32_t num_midi_ins = 1, uint32_t num_midi_outs = 1);
    ~OssBackend();

    bool start() override;
    void stop() override;
    bool is_active() const override;

    uint32_t sample_rate() const override;
    uint32_t buffer_size() const override;
    AudioBackendType type() const override { return AudioBackendType::Oss; }

private:
    bool configure_device();
    void audio_loop();
    bool write_interleaved(const int16_t *data, size_t samples);

    Engine *m_engine;

    int m_fd;
    std::atomic<bool> m_active;
    std::atomic<bool> m_running;
    std::thread m_thread;

    uint32_t m_requested_ins;
    uint32_t m_requested_outs;
    uint32_t m_requested_midi_ins;
    uint32_t m_requested_midi_outs;

    uint32_t m_channels;
    uint32_t m_sample_rate_hz;
    uint32_t m_buffer_frames;

    std::vector<std::vector<float>> m_out_buffers;
    std::vector<float *> m_out_ptrs;
    std::vector<int16_t> m_interleaved;
};

} // namespace tpanar_ns
