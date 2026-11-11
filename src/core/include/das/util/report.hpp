// SPDX-License-Identifier: GPL-3.0-or-later
//
// Problem reports: a link to a new GitHub issue with the bug form already
// filled in (version, system and the end of the log), so a player only has
// to say what went wrong.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace das::report {

// Percent-encodes everything except letters, digits and -._~.
std::string urlEncode(std::string_view text);

struct Problem {
    std::string version;
    std::string system;
    std::string log;
    std::string what;     // the player's own words, for "What went wrong?"
    std::string snapshot; // the state of the game at the moment (bug snapshot)
};

// The repository's "new issue" link for the bug form. Lines are dropped from
// the start of the log (and then from the end of the snapshot) until the
// whole link fits in maxLength characters.
std::string issueUrl(std::string_view repository, const Problem& problem, std::size_t maxLength = 6000);

} // namespace das::report
