/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "instrument_rack.h"
#include "instrument.h" // Added for tpanar_ns::Instrument definition

namespace tpanar_ns
{

    size_t tpanar_ns::InstrumentRack::add(
        ::std::unique_ptr<tpanar_ns::Instrument> inst)
    {
        m_instruments.push_back(::std::move(inst));
        return m_instruments.size() - 1;
    }

    tpanar_ns::Instrument* tpanar_ns::InstrumentRack::get(size_t index)
    {
        if (index >= m_instruments.size())
            return nullptr;

        return m_instruments[index].get();
    }

    size_t tpanar_ns::InstrumentRack::size() const
    {
        return m_instruments.size();
    }

} // namespace tpanar_ns
