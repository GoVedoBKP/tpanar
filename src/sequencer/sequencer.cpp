/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "sequencer.h"

namespace tpanar_ns
{

void Sequencer::set_timing(tpanar_ns::Timing* timing)
{
    m_timing = timing;
}

void Sequencer::set_order_list(const ::std::vector<int>& orders)
{
    m_order_list = orders;
}

void Sequencer::set_patterns(const ::std::vector<tpanar_ns::Pattern>& patterns_list)
{
    for (size_t i = 0; i < patterns_list.size() && i < MAX_PATTERNS; ++i)
    {
        m_patterns[i] = patterns_list[i];
    }
}

const TrackEvent& Sequencer::current_event(size_t track) const
{
    static TrackEvent empty;

    if (m_order_list.empty())
        return empty;

    int pattern_index = m_order_list[m_current_order];
    if (pattern_index < 0 || (size_t)pattern_index >= MAX_PATTERNS) return empty;

    return m_patterns[pattern_index].event(track, m_current_row, 0);
}

void Sequencer::advance_row()
{
    if (m_order_list.empty())
        return;

    int pattern_index = m_order_list[m_current_order];
    if (pattern_index < 0 || (size_t)pattern_index >= MAX_PATTERNS) return;

    m_current_row++;

    if (m_current_row >= m_patterns[pattern_index].row_count())
    {
        m_current_row = 0;
        m_current_order++;

        if (m_current_order >= m_order_list.size())
            m_current_order = 0;
    }

    if (m_pending_order_jump >= 0)
    {
        m_current_order = m_pending_order_jump;
        m_current_row = 0;
        m_pending_order_jump = -1;
    }
} 

void Sequencer::jump_to_order(int order)
{
    m_pending_order_jump = order;
}

void Sequencer::break_to_row(int row)
{
    m_pending_row_break = row;
}

void Sequencer::pattern_break()
{
    m_current_row = 0;
    m_current_order++;
}

void Sequencer::jump_to(size_t order)
{
    if (order < m_order_length)
    {
        m_current_order = order;
        m_current_row = 0;
    }
}

} // namespace tpanar_ns
