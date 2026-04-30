/*
 * TPanar - Digital Audio Workstation
 * Copyright (C) 2025  Miroslav Shaltev
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "mixer_model.h"

namespace tpanar_ns
{

    void tpanar_ns::MixerModel::update_from_engine(
        const ::std::vector<tpanar_ns::MixerChannelState>& state)
    {
        m_channels = state;
    }

    const ::std::vector<tpanar_ns::MixerChannelState>&
    tpanar_ns::MixerModel::channels() const
    {
        return m_channels;
    }

} // namespace tpanar_ns
