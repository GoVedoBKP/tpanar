/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once

#include "note.h"
#include <vector>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <nlohmann/json.hpp>

namespace tpanar_ns
{

constexpr uint8_t NOTE_EMPTY = 255;

constexpr size_t MAX_COLS = 16;

struct TrackData {
    size_t columns = 1;
    // Flat data[row * MAX_COLS + column]
    std::vector<TrackEvent> data;
    
    TrackData(size_t row_count = 64, size_t col_count = 1) : columns(col_count) {
        data.resize(row_count * MAX_COLS);
    }
};

class Pattern
{
public:
    Pattern(size_t rows = 64, size_t tracks = 8) : m_row_count(rows) {
        m_tracks.resize(tracks, TrackData(rows, 1));
    }

    size_t track_count() const { return m_tracks.size(); }
    size_t row_count() const { return m_row_count; }
    
    size_t column_count(size_t track) const { 
        if (track >= m_tracks.size()) return 0;
        return m_tracks[track].columns; 
    }

    void set_column_count(size_t track, size_t cols) {
        if (track >= m_tracks.size()) return;
        if (cols > MAX_COLS) cols = MAX_COLS;
        m_tracks[track].columns = cols;
    }

    nlohmann::json to_json() const;

    TrackEvent& event(size_t track, size_t row, size_t column) {
        if (track >= m_tracks.size() || row >= m_row_count || column >= MAX_COLS) {
             static TrackEvent dummy; return dummy;
        }
        return m_tracks[track].data[row * MAX_COLS + column];
    }

    const TrackEvent& event(size_t track, size_t row, size_t column) const {
        if (track >= m_tracks.size() || row >= m_row_count || column >= MAX_COLS) {
             static const TrackEvent dummy; return dummy;
        }
        return m_tracks[track].data[row * MAX_COLS + column];
    }

    void resize_rows(size_t new_rows);

    void resize_tracks(size_t new_tracks) {
        m_tracks.resize(new_tracks, TrackData(m_row_count, 1));
    }

    void insert_track(size_t index, size_t cols = 1);
    void remove_track(size_t index);
    void move_track(size_t from, size_t to);

    void insert_row(size_t row);
    void delete_row(size_t row);

    uint8_t get_field(size_t track, size_t row, size_t abs_field) const;
    void set_field(size_t track, size_t row, size_t abs_field, uint8_t val);

private:
    size_t m_row_count;
    std::vector<TrackData> m_tracks;
};

} // namespace tpanar_ns
