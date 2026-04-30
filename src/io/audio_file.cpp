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

#include "audio_file.h"
#include <sndfile.h>

namespace tpanar_ns
{

    bool AudioFile::load_audio(const ::std::string& path,
                             ::std::vector<float>& left,
                             ::std::vector<float>& right,
                             uint32_t& sample_rate)
    {
        SF_INFO info{};
        SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
        if (!file)
            return false;

        sample_rate = info.samplerate;

        ::std::vector<float> buffer(info.frames * info.channels);
        sf_readf_float(file, buffer.data(), info.frames);
        sf_close(file);

        left.resize(info.frames);
        right.resize(info.frames);

        for (sf_count_t i = 0; i < info.frames; ++i)
        {
            left[i]  = buffer[i * info.channels];
            right[i] = (info.channels > 1)
            ? buffer[i * info.channels + 1]
            : left[i];
        }

        return true;
    }

    bool AudioFile::save_wav(const ::std::string& path,
                             const ::std::vector<float>& left,
                             const ::std::vector<float>& right,
                             uint32_t sample_rate)
    {
        SF_INFO info{};
        info.channels = 2;
        info.samplerate = sample_rate;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

        SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &info);
        if (!file)
            return false;

        size_t frames = ::std::min(left.size(), right.size());
        ::std::vector<float> buffer(frames * 2);

        for (size_t i = 0; i < frames; ++i)
        {
            buffer[i * 2]     = left[i];
            buffer[i * 2 + 1] = right[i];
        }

        sf_writef_float(file, buffer.data(), frames);
        sf_close(file);

        return true;
    }

} // namespace tpanar_ns
