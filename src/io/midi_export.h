/*
 * TPanar - Digital Audio Workstation
 * Copyright (C) 2026  Miroslav Shaltev
 */

#pragma once

#include <string>

namespace tpanar_ns {

class Engine;

// Export all notation (note) tracks to a Standard MIDI File (format 1).
// Returns true on success, false on failure (e.g. write error).
bool export_midi(const Engine& engine, const std::string& path);

} // namespace tpanar_ns
