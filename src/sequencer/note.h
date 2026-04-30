/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once
#include <cstdint>

namespace tpanar_ns
{

enum class EffectType : uint8_t
{
    None,
    SetTempo,
    Volume,
    PatternBreak,
    Jump,
    NoteCut
};

struct TrackEvent
{
    uint8_t note = 255;      // 0-119, 255 = empty
    uint8_t sample_idx = 0;  // 0 = none/default
    uint8_t volume = 255;    // 255 = empty/default, 0-127 = actual
    
    // Each track row has two effect columns
    uint8_t effect1 = 0;
    uint8_t param1 = 0;
    uint8_t effect2 = 0;
    uint8_t param2 = 0;
};

} // namespace tpanar_ns
