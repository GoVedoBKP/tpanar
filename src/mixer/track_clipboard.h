/*
 * TPanar - Digital Drums Recording Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once

#include <vector>
#include <cstddef>

namespace tpanar_ns {

/**
 * Track Audio Clipboard
 * 
 * Stores audio samples (L/R channels) copied from tracks.
 * Shared per-engine clipboard for copy/paste operations.
 */
class TrackClipboard {
public:
    struct AudioEntry {
        std::vector<float> left;
        std::vector<float> right;
    };

    TrackClipboard() = default;

    /**
     * Copy audio samples to clipboard
     * @param left Left channel samples
     * @param right Right channel samples
     */
    void set_audio(const std::vector<float>& left, const std::vector<float>& right);
    void set_audio_group(const std::vector<AudioEntry>& entries);

    /**
     * Get clipboard content
     * @param left Output: left channel samples
     * @param right Output: right channel samples
     * @return true if clipboard has content, false if empty
     */
    bool get_audio(std::vector<float>& left, std::vector<float>& right) const;
    bool get_audio_group(std::vector<AudioEntry>& entries) const;

    /**
     * Check if clipboard has audio
     */
    bool has_content() const;

    /**
     * Clear clipboard
     */
    void clear();

    /**
     * Get number of samples in clipboard
     */
    size_t sample_count() const;
    size_t track_count() const;

private:
    std::vector<AudioEntry> m_entries;
};

} // namespace tpanar_ns
