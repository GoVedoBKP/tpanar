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
#include <string>
#include <vector>

namespace tpanar_ns
{

class DSP
{
public:
    virtual ~DSP() = default;

    virtual void process(float* l,
                         float* r,
                         size_t nframes) = 0;

    virtual size_t latency() const { return 0; }
    
    virtual std::string name() const = 0;
    virtual std::string type_name() const = 0;

    virtual void set_sample_rate(float sr) {}

    bool is_bypassed() const { return m_bypassed; }
    void set_bypass(bool b) { m_bypassed = b; }

    virtual std::vector<std::string> get_presets() { return {"Default"}; }
    virtual void load_preset(const std::string& name) { m_current_preset = name; }
    virtual void save_preset(const std::string& name) {}

    const std::string& current_preset() const { return m_current_preset; }
    void set_current_preset(const std::string& name) { m_current_preset = name; }

    virtual std::string get_state() const { return "{}"; }
    virtual void set_state(const std::string& state) {}

protected:
    bool m_bypassed = false;
    std::string m_current_preset = "Default";
};


} // namespace tpanar_ns
