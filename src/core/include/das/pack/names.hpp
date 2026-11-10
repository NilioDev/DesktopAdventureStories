// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal helpers for file names shared by the pack writer and the importer.
#pragma once

#include <string>

namespace das::pack::detail {

std::string lower(std::string text);

// The lower-case file name of an original sound path such as
// "C:\INDY\MASTER\PUSH.WAV" -> "push.wav".
std::string soundFileName(const std::string& path);

} // namespace das::pack::detail
