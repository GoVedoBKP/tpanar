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

#include "dsp.h"
#include <array>
#include <memory>

namespace tpanar_ns
{

constexpr size_t MAX_INSERTS = 32;

class DSPChain
{
public:
    void process(float* l,
                 float* r,
                 size_t nframes);

    void set(size_t index, ::std::unique_ptr<DSP> dsp);
    void enable(size_t index, bool en);
    void move_up(size_t index);
    void move_down(size_t index);
    void remove(size_t index);

    void set_sample_rate(float sr) {
        for (auto& e : m_effects) {
            if (e) e->set_sample_rate(sr);
        }
    }

    const ::std::array<::std::unique_ptr<DSP>, MAX_INSERTS>& effects() const { return m_effects; }
    bool is_enabled(size_t index) const { return index < MAX_INSERTS && m_enabled[index]; }

    void save_chain(const std::string& path);
    void load_chain(const std::string& path);

    void to_json(void* j_ptr) const; // j_ptr is nlohmann::json*
    void from_json(const void* j_ptr); // j_ptr is const nlohmann::json*

private:
    ::std::array<::std::unique_ptr<DSP>, MAX_INSERTS> m_effects{};
    ::std::array<bool, MAX_INSERTS> m_enabled{};
};

} // namespace tpanar_ns
