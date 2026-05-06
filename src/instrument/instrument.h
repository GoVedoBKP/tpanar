/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <array>
#include "../audio/voice.h"

namespace tpanar_ns
{

constexpr size_t MAX_VOICES = 32;

enum class InstrumentType {
    None,
    Sampler,
    SoundFont,
    SFZ,
    Plugin,
    Midi,
    XRNI
};

class Instrument
{
public:
    virtual ~Instrument() = default;

    virtual void note_on(uint8_t note, uint8_t velocity, size_t column_index = 0, size_t offset_samples = 0, uint8_t sample_index = 0) = 0;
    virtual void note_off(size_t column_index = 0) = 0;
    virtual void panic() = 0;

    virtual void set_sample_rate(double sr) {}

    virtual void set_volume(float vol) = 0;

    virtual void set_pitch(float freq) = 0;
    virtual void process(float* l,
                         float* r,
                         size_t nframes) = 0;

    void set_name(const std::string& name) { m_name = name.substr(0, 64); }
    const std::string& name() const { return m_name; }

    void set_plugin_name(const std::string& name) { m_plugin_name = name; }
    const std::string& plugin_name() const { return m_plugin_name; }

    void set_type(InstrumentType type) { m_type = type; }
    InstrumentType type() const { return m_type; }

    struct Parameter {
        int index;
        std::string name;
        float min = 0.0f;
        float max = 1.0f;
        float value = 0.0f;
    };

    virtual size_t parameter_count() const { return 0; }
    virtual Parameter get_parameter(size_t) const { return {}; }
    virtual void set_parameter(size_t, float) {}

protected:
    virtual ::std::unique_ptr<tpanar_ns::Voice> create_voice() = 0;

    tpanar_ns::Voice* allocate_voice(size_t column_index);

    std::string m_name = "New Instrument";
    std::string m_plugin_name = "";
    InstrumentType m_type = InstrumentType::None;
protected:
    ::std::array<::std::unique_ptr<tpanar_ns::Voice>, MAX_VOICES> m_voices;
};

class NoneInstrument : public Instrument {
public:
    void note_on(uint8_t, uint8_t, size_t, size_t, uint8_t) override {}
    void note_off(size_t) override {}
    void panic() override {}
    void set_volume(float) override {}
    void set_pitch(float) override {}
    void process(float*, float*, size_t) override {}
protected:
    std::unique_ptr<Voice> create_voice() override { return nullptr; }
};

} // namespace tpanar_ns
