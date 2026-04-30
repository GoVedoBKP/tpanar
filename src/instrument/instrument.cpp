/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "../core/engine.h" // ADDED
#include <cmath> // Added for powf
#include "instrument.h"

namespace tpanar_ns
{

    tpanar_ns::Voice* tpanar_ns::Instrument::allocate_voice(size_t column_index)
    {
        // find inactive voice
        for (auto& v : m_voices)
        {
            if (v && !v->active()) {
                v->set_column(column_index);
                return v.get();
            }
        }

        // voice stealing: steal oldest (voice 0)
        if (m_voices[0]) {
            m_voices[0]->set_column(column_index);
            return m_voices[0].get();
        }
        return nullptr;
    }

} // namespace tpanar_ns
