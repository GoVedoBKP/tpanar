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

#include "voice_synthesis_worker.h"
#include "voice_instrument.h"
#include <chrono>

namespace tpanar_ns {

VoiceSynthesisWorker::VoiceSynthesisWorker(VoiceInstrument* voice_inst)
    : m_voice_inst(voice_inst)
{
}

VoiceSynthesisWorker::~VoiceSynthesisWorker() {
    stop();
}

void VoiceSynthesisWorker::start() {
    if (m_running.load()) return;
    
    m_running = true;
    m_stop_requested = false;
    m_completed = 0;
    m_total_queued = 0;
    
    m_thread = std::make_unique<std::thread>([this]() {
        this->worker_thread_main();
    });
}

void VoiceSynthesisWorker::stop() {
    if (!m_running.load()) return;
    
    // Signal stop and wait for thread
    m_stop_requested = true;
    
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    
    m_running = false;
    m_stop_requested = false;
    
    // Clear any remaining queue
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        while (!m_queue.empty()) {
            m_queue.pop();
        }
    }
}

void VoiceSynthesisWorker::queue_synthesis(const std::string& text, float base_freq, bool update_active) {
    if (!m_voice_inst) return;
    
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_queue.push({text, base_freq, update_active});
        m_total_queued++;
    }
}

float VoiceSynthesisWorker::get_progress() const {
    if (m_total_queued == 0) return 0.0f;
    return static_cast<float>(m_completed.load()) / static_cast<float>(m_total_queued);
}

bool VoiceSynthesisWorker::is_queue_empty() const {
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    return m_queue.empty();
}

void VoiceSynthesisWorker::worker_thread_main() {
    while (!m_stop_requested.load()) {
        process_queue();
        
        // Sleep briefly if queue is empty to avoid busy-waiting
        if (is_queue_empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    // Process any remaining items before exiting
    process_queue();
}

void VoiceSynthesisWorker::process_queue() {
    SynthesisTask task;
    
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        if (m_queue.empty()) return;
        
        task = m_queue.front();
        m_queue.pop();
    }
    
    fprintf(stderr, "[VoiceWorker] Processing: \"%s\" at %.2f Hz\n", task.text.c_str(), task.base_freq);
    
    // Synthesize outside of lock
    bool success = false;
    if (m_voice_inst && !task.text.empty()) {
        success = m_voice_inst->synthesize_text(task.text, task.base_freq, task.update_active);
    }
    
    fprintf(stderr, "[VoiceWorker] Result: %s\n", success ? "Success" : "FAILED");
    
    // Update progress
    size_t completed = m_completed.fetch_add(1) + 1;
    if (m_progress_callback) {
        m_progress_callback(get_progress(), completed, m_total_queued);
    }
}

} // namespace tpanar_ns
