/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "mixer_bus.h"
#include <algorithm>
#include <cmath>

namespace tpanar_ns {

MixerBus::MixerBus() : m_name("Bus"), m_output_bus(ROUTE_MASTER) {}

MixerBus::MixerBus(MixerBus&& other) noexcept 
    : m_name(std::move(other.m_name)),
      m_volume(other.m_volume),
      m_pan(other.m_pan),
      m_mute(other.m_mute),
      m_output_bus(other.m_output_bus),
      m_chain(std::move(other.m_chain)) {
    m_meter_l.store(other.m_meter_l.load());
    m_meter_r.store(other.m_meter_r.load());
}

MixerBus& MixerBus::operator=(MixerBus&& other) noexcept {
    if (this != &other) {
        m_name = std::move(other.m_name);
        m_volume = other.m_volume;
        m_pan = other.m_pan;
        m_mute = other.m_mute;
        m_output_bus = other.m_output_bus;
        m_chain = std::move(other.m_chain);
        m_meter_l.store(other.m_meter_l.load());
        m_meter_r.store(other.m_meter_r.load());
    }
    return *this;
}

void MixerBus::process(float* out_l, float* out_r, size_t nframes) {
    float peak_l = 0.0f;
    float peak_r = 0.0f;

    m_chain.process(out_l, out_r, nframes);

    float vol = m_mute ? 0.0f : m_volume;
    float pan_l = std::min(1.0f, 1.0f - m_pan);
    float pan_r = std::min(1.0f, 1.0f + m_pan);

    for (size_t i = 0; i < nframes; ++i) {
        out_l[i] *= vol * pan_l;
        out_r[i] *= vol * pan_r;
        peak_l = std::max(peak_l, std::abs(out_l[i]));
        peak_r = std::max(peak_r, std::abs(out_r[i]));
    }

    m_meter_l.store(peak_l);
    m_meter_r.store(peak_r);
}

void MixerBus::set_name(const std::string& name) { m_name = name; }
const std::string& MixerBus::name() const { return m_name; }

void MixerBus::set_volume(float v) { m_volume = v; }
float MixerBus::volume() const { return m_volume; }

void MixerBus::set_pan(float p) { m_pan = p; }
float MixerBus::pan() const { return m_pan; }

void MixerBus::set_mute(bool m) { m_mute = m; }
bool MixerBus::muted() const { return m_mute; }

float MixerBus::meter_l() const { return m_meter_l.load(); }
float MixerBus::meter_r() const { return m_meter_r.load(); }

void MixerBus::set_output_bus(int bus_idx) { m_output_bus = bus_idx; }
int MixerBus::output_bus() const { return m_output_bus; }

void MixerBus::set_effect(size_t index, ::std::unique_ptr<tpanar_ns::DSP> dsp) {
    m_chain.set(index, std::move(dsp));
}

void MixerBus::enable_effect(size_t index, bool en) {
    m_chain.enable(index, en);
}

void MixerBus::move_effect_up(size_t index) {
    m_chain.move_up(index);
}

void MixerBus::move_effect_down(size_t index) {
    m_chain.move_down(index);
}

void MixerBus::remove_effect(size_t index) {
    m_chain.remove(index);
}

tpanar_ns::DSP* MixerBus::get_effect(size_t index) const {
    if (index >= tpanar_ns::MAX_INSERTS) return nullptr;
    return m_chain.effects()[index].get();
}

bool MixerBus::is_effect_enabled(size_t index) const {
    return true;
}

void MixerBus::save_effect_chain(const std::string& path) {
    m_chain.save_chain(path);
}

void MixerBus::load_effect_chain(const std::string& path) {
    m_chain.load_chain(path);
}

} // namespace tpanar_ns
