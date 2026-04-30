/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once
#include <array>
#include <cstddef> // Added for size_t

namespace tpanar_ns
{

    constexpr size_t MAX_AUTOMATION_POINTS = 512;

    struct AutomationPoint
    {
        size_t row;
        float value;
    };

    class AutomationLane
    {
    public:
        void add_point(size_t row, float value);
        float value_at(size_t row) const;

    private:
        ::std::array<tpanar_ns::AutomationPoint,
        MAX_AUTOMATION_POINTS> m_points{};
        size_t m_count = 0;
    };

} // namespace tpanar_ns
