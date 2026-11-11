// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's log, kept for problem reports: every SDL_Log line still goes to
// the usual place, and the last lines are also kept in memory and in
// log.txt next to the settings (the one before that in log-previous.txt).
//
// A marker file says the app is running. If it is still there at the next
// start, the app did not close properly (a crash, or it was killed while in
// the foreground).
#pragma once

#include <filesystem>
#include <string>

namespace das::app::problem_log {

// Starts catching log lines in memory. Call first thing.
void begin();
// Writes the log to `folder` from now on. Returns whether the last run
// ended without closing properly.
bool attach(const std::filesystem::path& folder);
// The app closes properly, or goes to the background (where a phone may
// stop it without notice): no crash. resume() puts the marker back.
void pause();
void resume();
// The last lines of the log, oldest first, then the end of the run before
// when that one stopped unexpectedly.
std::string tail();
std::filesystem::path file();

} // namespace das::app::problem_log
