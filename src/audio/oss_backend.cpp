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

#include "oss_backend.h"
#include "../core/engine.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <sys/ioctl.h>

#if defined(__has_include)
#  if __has_include(<sys/soundcard.h>)
#    include <sys/soundcard.h>
#  elif __has_include(<soundcard.h>)
#    include <soundcard.h>
#  else
#    error "OSS soundcard header not found"
#  endif
#else
#  include <sys/soundcard.h>
#endif

namespace tpanar_ns
{

namespace
{

constexpr uint32_t kDefaultSampleRate = 44100;
constexpr uint32_t kDefaultBufferFrames = 512;

int oss_sample_format()
{
#ifdef AFMT_S16_NE
    return AFMT_S16_NE;
#else
    return AFMT_S16_LE;
#endif
}

int clamp_pcm(float sample)
{
    const float clamped = std::max(-1.0f, std::min(1.0f, sample));
    return static_cast<int>(clamped * 32767.0f);
}

} // namespace

OssBackend::OssBackend(Engine *engine,
                       uint32_t num_ins, uint32_t num_outs,
                       uint32_t num_midi_ins, uint32_t num_midi_outs)
    : m_engine(engine),
      m_fd(-1),
      m_active(false),
      m_running(false),
      m_requested_ins(num_ins),
      m_requested_outs(num_outs),
      m_requested_midi_ins(num_midi_ins),
      m_requested_midi_outs(num_midi_outs),
      m_channels(std::max(2u, num_outs)),
      m_sample_rate_hz(kDefaultSampleRate),
      m_buffer_frames(kDefaultBufferFrames)
{
}

OssBackend::~OssBackend()
{
    stop();
}

bool OssBackend::start()
{
    if (m_active.load(std::memory_order_acquire)) {
        return true;
    }

    m_fd = ::open("/dev/dsp", O_WRONLY);
    if (m_fd < 0) {
        std::cerr << "Failed to open OSS device /dev/dsp: " << std::strerror(errno) << std::endl;
        return false;
    }

    if (!configure_device()) {
        stop();
        return false;
    }

    m_out_buffers.assign(m_channels, std::vector<float>(m_buffer_frames, 0.0f));
    m_out_ptrs.resize(m_channels, nullptr);
    for (uint32_t ch = 0; ch < m_channels; ++ch) {
        m_out_ptrs[ch] = m_out_buffers[ch].data();
    }
    m_interleaved.assign(static_cast<size_t>(m_buffer_frames) * m_channels, 0);

    m_running.store(true, std::memory_order_release);
    m_active.store(true, std::memory_order_release);
    m_thread = std::thread(&OssBackend::audio_loop, this);
    return true;
}

void OssBackend::stop()
{
    m_running.store(false, std::memory_order_release);

    const int fd = m_fd;
    m_fd = -1;
    if (fd >= 0) {
        ::close(fd);
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_active.store(false, std::memory_order_release);
    m_out_buffers.clear();
    m_out_ptrs.clear();
    m_interleaved.clear();
}

bool OssBackend::is_active() const
{
    return m_active.load(std::memory_order_acquire);
}

uint32_t OssBackend::sample_rate() const
{
    return m_sample_rate_hz;
}

uint32_t OssBackend::buffer_size() const
{
    return m_buffer_frames;
}

bool OssBackend::configure_device()
{
    int format = oss_sample_format();
    if (::ioctl(m_fd, SNDCTL_DSP_SETFMT, &format) == -1) {
        std::cerr << "Failed to set OSS sample format: " << std::strerror(errno) << std::endl;
        return false;
    }

    if (format != oss_sample_format()) {
        std::cerr << "OSS device rejected 16-bit PCM format." << std::endl;
        return false;
    }

    int channels = static_cast<int>(std::max(2u, m_requested_outs));
    if (::ioctl(m_fd, SNDCTL_DSP_CHANNELS, &channels) == -1) {
        std::cerr << "Failed to set OSS channel count: " << std::strerror(errno) << std::endl;
        return false;
    }
    if (channels <= 0) {
        std::cerr << "OSS device returned an invalid channel count." << std::endl;
        return false;
    }
    m_channels = static_cast<uint32_t>(channels);

    int sample_rate = static_cast<int>(kDefaultSampleRate);
    if (::ioctl(m_fd, SNDCTL_DSP_SPEED, &sample_rate) == -1) {
        std::cerr << "Failed to set OSS sample rate: " << std::strerror(errno) << std::endl;
        return false;
    }
    if (sample_rate <= 0) {
        std::cerr << "OSS device returned an invalid sample rate." << std::endl;
        return false;
    }
    m_sample_rate_hz = static_cast<uint32_t>(sample_rate);

    int block_bytes = 0;
    if (::ioctl(m_fd, SNDCTL_DSP_GETBLKSIZE, &block_bytes) == -1 || block_bytes <= 0) {
        block_bytes = static_cast<int>(kDefaultBufferFrames * m_channels * sizeof(int16_t));
    }

    const uint32_t bytes_per_frame = m_channels * sizeof(int16_t);
    m_buffer_frames = std::max(1u, static_cast<uint32_t>(block_bytes) / std::max(1u, bytes_per_frame));
    return true;
}

void OssBackend::audio_loop()
{
    while (m_running.load(std::memory_order_acquire)) {
        for (uint32_t ch = 0; ch < m_channels; ++ch) {
            std::fill(m_out_buffers[ch].begin(), m_out_buffers[ch].end(), 0.0f);
        }

        m_engine->process_audio(nullptr, 0, m_out_ptrs.data(), m_channels, m_buffer_frames);

        for (uint32_t frame = 0; frame < m_buffer_frames; ++frame) {
            for (uint32_t ch = 0; ch < m_channels; ++ch) {
                const size_t index = static_cast<size_t>(frame) * m_channels + ch;
                m_interleaved[index] = static_cast<int16_t>(clamp_pcm(m_out_buffers[ch][frame]));
            }
        }

        if (!write_interleaved(m_interleaved.data(), m_interleaved.size())) {
            break;
        }
    }

    m_active.store(false, std::memory_order_release);
}

bool OssBackend::write_interleaved(const int16_t *data, size_t samples)
{
    const char *bytes = reinterpret_cast<const char *>(data);
    size_t remaining = samples * sizeof(int16_t);

    while (remaining > 0 && m_running.load(std::memory_order_acquire)) {
        const ssize_t written = ::write(m_fd, bytes, remaining);
        if (written > 0) {
            bytes += written;
            remaining -= static_cast<size_t>(written);
            continue;
        }

        if (written < 0 && errno == EINTR) {
            continue;
        }

        if (m_running.load(std::memory_order_acquire)) {
            std::cerr << "OSS audio write failed: " << std::strerror(errno) << std::endl;
        }
        return false;
    }

    return remaining == 0;
}

} // namespace tpanar_ns
