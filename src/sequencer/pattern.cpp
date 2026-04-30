/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "pattern.h"
#include <nlohmann/json.hpp>

namespace tpanar_ns
{

void to_json(nlohmann::json& j, const TrackEvent& p) {
    j = nlohmann::json{
        {"note", p.note},
        {"sample", p.sample_idx},
        {"volume", p.volume},
        {"fx1", p.effect1},
        {"p1", p.param1},
        {"fx2", p.effect2},
        {"p2", p.param2}
    };
}

nlohmann::json Pattern::to_json() const
{
    nlohmann::json j;
    j["rows"] = m_row_count;

    nlohmann::json tracks_json = nlohmann::json::array();
    for (size_t track_idx = 0; track_idx < m_tracks.size(); ++track_idx) {
        nlohmann::json track_info;
        track_info["cols"] = m_tracks[track_idx].columns;
        
        nlohmann::json rows_json = nlohmann::json::array();
        for (size_t row_idx = 0; row_idx < m_row_count; ++row_idx) {
            nlohmann::json columns_json = nlohmann::json::array();
            for (size_t col_idx = 0; col_idx < m_tracks[track_idx].columns; ++col_idx) {
                columns_json.push_back(m_tracks[track_idx].data[row_idx * MAX_COLS + col_idx]);
            }
            rows_json.push_back(columns_json);
        }
        track_info["data"] = rows_json;
        tracks_json.push_back(track_info);
    }
    j["tracks"] = tracks_json;

    return j;
}

void Pattern::resize_rows(size_t new_rows) {
    for (auto& track : m_tracks) {
        std::vector<TrackEvent> new_data(new_rows * MAX_COLS);
        const size_t rows_to_copy = std::min(m_row_count, new_rows);
        for (size_t row = 0; row < rows_to_copy; ++row) {
            for (size_t col = 0; col < MAX_COLS; ++col) {
                new_data[row * MAX_COLS + col] = track.data[row * MAX_COLS + col];
            }
        }
        track.data = std::move(new_data);
    }
    m_row_count = new_rows;
}

void Pattern::insert_track(size_t index, size_t cols) {
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (index > m_tracks.size()) index = m_tracks.size();
    m_tracks.insert(m_tracks.begin() + index, TrackData(m_row_count, cols));
}

void Pattern::remove_track(size_t index) {
    if (index >= m_tracks.size()) return;
    m_tracks.erase(m_tracks.begin() + index);
}

void Pattern::move_track(size_t from, size_t to) {
    if (from >= m_tracks.size() || to >= m_tracks.size() || from == to) return;

    TrackData moved = std::move(m_tracks[from]);
    m_tracks.erase(m_tracks.begin() + from);
    m_tracks.insert(m_tracks.begin() + to, std::move(moved));
}

void Pattern::insert_row(size_t row) {
    if (row >= m_row_count) return;
    for (auto& track : m_tracks) {
        // Shift rows down from the end up to the inserted row
        for (size_t r = m_row_count - 1; r > row; --r) {
            for (size_t c = 0; c < MAX_COLS; ++c) {
                track.data[r * MAX_COLS + c] = track.data[(r - 1) * MAX_COLS + c];
            }
        }
        // Clear the inserted row
        for (size_t c = 0; c < MAX_COLS; ++c) {
            track.data[row * MAX_COLS + c] = TrackEvent();
        }
    }
}

void Pattern::delete_row(size_t row) {
    if (row >= m_row_count) return;
    for (auto& track : m_tracks) {
        // Shift rows up from the deleted row to the end
        for (size_t r = row; r < m_row_count - 1; ++r) {
            for (size_t c = 0; c < MAX_COLS; ++c) {
                track.data[r * MAX_COLS + c] = track.data[(r + 1) * MAX_COLS + c];
            }
        }
        // Clear the last row
        for (size_t c = 0; c < MAX_COLS; ++c) {
            track.data[(m_row_count - 1) * MAX_COLS + c] = TrackEvent();
        }
    }
}

uint8_t Pattern::get_field(size_t track, size_t row, size_t abs_field) const {
    size_t num_cols = column_count(track);
    if (num_cols == 0 && abs_field == 0) {
        return event(track, row, 0).param1;
    }
    if (abs_field < num_cols * 3) {
        const auto& ev = event(track, row, abs_field / 3);
        int sub_f = (int)abs_field % 3;
        if (sub_f == 0) return ev.note;
        if (sub_f == 1) return ev.sample_idx;
        return ev.volume;
    } else {
        const auto& ev = event(track, row, 0);
        int fx_f = (int)abs_field - (int)num_cols * 3;
        if (fx_f == 0) return ev.effect1;
        if (fx_f == 1) return ev.param1;
        if (fx_f == 2) return ev.effect2;
        if (fx_f == 3) return ev.param2;
    }
    return 0;
}

void Pattern::set_field(size_t track, size_t row, size_t abs_field, uint8_t val) {
    size_t num_cols = column_count(track);
    if (num_cols == 0 && abs_field == 0) {
        event(track, row, 0).param1 = val;
        return;
    }
    if (abs_field < num_cols * 3) {
        auto& ev = event(track, row, abs_field / 3);
        int sub_f = (int)abs_field % 3;
        if (sub_f == 0) ev.note = val;
        else if (sub_f == 1) ev.sample_idx = val;
        else if (sub_f == 2) ev.volume = val;
    } else {
        auto& ev = event(track, row, 0);
        int fx_f = (int)abs_field - (int)num_cols * 3;
        if (fx_f == 0) ev.effect1 = val;
        else if (fx_f == 1) ev.param1 = val;
        else if (fx_f == 2) ev.effect2 = val;
        else if (fx_f == 3) ev.param2 = val;
    }
}

} // namespace tpanar_ns
