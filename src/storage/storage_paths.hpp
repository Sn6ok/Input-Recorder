#pragma once

// Resolves the per-user local data directory for the database and related files
// (spec §164: %LOCALAPPDATA%\InputRecorder\). Windows-only (uses the Known
// Folder API); tests construct EventStore/StorageWorker with explicit paths, so
// this file is not needed off Windows.

#include <string>

namespace ir {

// Returns %LOCALAPPDATA%\InputRecorder (UTF-8), creating it if needed. Empty on
// failure.
std::string default_data_dir();

// Full path to the primary database file inside the data dir (UTF-8). Empty on
// failure.
std::string default_database_path();

}  // namespace ir
