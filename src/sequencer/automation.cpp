/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "automation.h"
#include <algorithm>

namespace tpanar_ns
{

    void tpanar_ns::AutomationLane::add_point(size_t row, float value)
    {
        if (m_count >= MAX_AUTOMATION_POINTS)
            return;

        m_points[m_count++] = { row, value };

        ::std::sort(m_points.begin(),
                  m_points.begin() + m_count,
                  [](const tpanar_ns::AutomationPoint& a,
                     const tpanar_ns::AutomationPoint& b)
                  {
                      return a.row < b.row;
                  });
    }

    float tpanar_ns::AutomationLane::value_at(size_t row) const
    {
        if (m_count == 0)
            return 0.0f;

        if (m_count == 1)
            return m_points[0].value;

        if (row <= m_points[0].row)
            return m_points[0].value;

        if (row >= m_points[m_count - 1].row)
            return m_points[m_count - 1].value;

        for (size_t i = 0; i < m_count - 1; ++i)
        {
            const auto& a = m_points[i];
            const auto& b = m_points[i + 1];

            if (row >= a.row && row <= b.row)
            {
                float t =
                float(row - a.row) /
                float(b.row - a.row);

                return a.value +
                t * (b.value - a.value);
            }
        }

        return 0.0f;
    }

} // namespace tpanar_ns
