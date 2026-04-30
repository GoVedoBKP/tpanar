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
#include <cstdint>
#include <cstddef>

namespace tpanar_ns
{

    class Voice
    {
    public:
        virtual ~Voice() = default;

        virtual void start(uint8_t note,
                           uint8_t velocity,
                           float frequency,
                           size_t offset_samples = 0) = 0;

                           virtual void stop() = 0;
                           virtual void panic() = 0;

                           virtual void set_pitch(float freq) = 0;
                           virtual void set_volume(float vol) = 0;

                           virtual void process(float* out_l,
                                                float* out_r,
                                                size_t frames) = 0;

                                                virtual bool active() const = 0;

        void set_column(size_t col) { m_column = col; }
        size_t column() const { return m_column; }

    protected:
        size_t m_column = 0;
    };

} // namespace tpanar_ns
