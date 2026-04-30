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

#pragma once

#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>
#include <string>

namespace tpanar_ns {

class VoiceInstrument;

class VoiceSynthesisWorker {
public:
    VoiceSynthesisWorker(VoiceInstrument* voice_inst);
    ~VoiceSynthesisWorker();
    
    // Start the worker thread
    void start();
    
    // Stop the worker thread and wait for completion
    void stop();
    
    // Queue a text for synthesis
    void queue_synthesis(const std::string& text, float base_freq = 440.0f, bool update_active = true);
    
    // Get progress (0.0 to 1.0)
    float get_progress() const;
    
    // Get total texts queued
    size_t get_total_queued() const { return m_total_queued; }
    
    // Get texts completed
    size_t get_completed() const { return m_completed.load(); }
    
    // Is worker running?
    bool is_running() const { return m_running.load(); }
    
    // Is queue empty?
    bool is_queue_empty() const;
    
    // Set progress callback
    void set_progress_callback(std::function<void(float, size_t, size_t)> callback) {
        m_progress_callback = callback;
    }

private:
    struct SynthesisTask {
        std::string text;
        float base_freq;
        bool update_active;
    };
    
    void worker_thread_main();
    void process_queue();
    
    VoiceInstrument* m_voice_inst;
    std::unique_ptr<std::thread> m_thread;
    std::queue<SynthesisTask> m_queue;
    mutable std::mutex m_queue_mutex;
    
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop_requested{false};
    std::atomic<size_t> m_completed{0};
    size_t m_total_queued = 0;
    
    std::function<void(float, size_t, size_t)> m_progress_callback;
};

} // namespace tpanar_ns
