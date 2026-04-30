/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once
#include "instrument.h"

namespace tpanar_ns {

class LV2Instrument : public Instrument {
public:
    LV2Instrument(double sample_rate) {}
    void note_on(uint8_t note, uint8_t velocity, size_t column_index = 0, size_t offset_samples = 0, uint8_t sample_index = 0) override {}
    void note_off(size_t column_index = 0) override {}
    void panic() override {}
    void set_volume(float vol) override {}
    void set_pitch(float freq) override {}
    void process(float* l, float* r, size_t nframes) override {
        for(size_t i=0; i<nframes; ++i) { l[i]=0; r[i]=0; }
    }
protected:
    std::unique_ptr<Voice> create_voice() override { return nullptr; }
};

} // namespace tpanar_ns
