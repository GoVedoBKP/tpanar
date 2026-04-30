/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once
#include <vector>
#include <memory>

namespace tpanar_ns
{

    class Instrument;

    class InstrumentRack
    {
    public:
        size_t add(::std::unique_ptr<tpanar_ns::Instrument> inst);

        Instrument* get(size_t index);

        size_t size() const;

    private:
        ::std::vector<::std::unique_ptr<tpanar_ns::Instrument>> m_instruments;
    };

} // namespace tpanar_ns
