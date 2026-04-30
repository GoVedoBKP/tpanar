/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#include "track_clipboard.h"

namespace tpanar_ns {

void TrackClipboard::set_audio(const std::vector<float>& left, const std::vector<float>& right) {
    m_entries.clear();
    m_entries.push_back({left, right});
}

void TrackClipboard::set_audio_group(const std::vector<AudioEntry>& entries) {
    m_entries = entries;
}

bool TrackClipboard::get_audio(std::vector<float>& left, std::vector<float>& right) const {
    if (m_entries.empty() || m_entries.front().left.empty()) {
        return false;
    }
    left = m_entries.front().left;
    right = m_entries.front().right;
    return true;
}

bool TrackClipboard::get_audio_group(std::vector<AudioEntry>& entries) const {
    if (m_entries.empty()) {
        return false;
    }
    entries = m_entries;
    return true;
}

bool TrackClipboard::has_content() const {
    return !m_entries.empty() && !m_entries.front().left.empty();
}

void TrackClipboard::clear() {
    m_entries.clear();
}

size_t TrackClipboard::sample_count() const {
    return m_entries.empty() ? 0 : m_entries.front().left.size();
}

size_t TrackClipboard::track_count() const {
    return m_entries.size();
}

} // namespace tpanar_ns
