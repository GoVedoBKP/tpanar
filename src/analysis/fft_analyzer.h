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
#include <vector>
#include <fftw3.h> // Added for FFTW3

namespace tpanar_ns
{

    class FFTAnalyzer
    {
    public:
        FFTAnalyzer(size_t size);
        ~FFTAnalyzer(); // Destructor to destroy FFTW plan

        void process(const float* input);

        const ::std::vector<float>& magnitudes() const;

    private:
        size_t m_size;
        ::std::vector<float> m_magnitudes; // Renamed from m_output to be more descriptive

        // FFTW3 members
        fftw_complex *m_in, *m_out; // Input and output buffers
        fftw_plan m_plan;           // FFTW plan
    };

} // namespace tpanar_ns
