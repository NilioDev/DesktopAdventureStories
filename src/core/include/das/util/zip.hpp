// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading zip archives (stored and deflated entries), so players can import
// a game straight from the zip file it came in.
#pragma once

#include "das/data/binary.hpp"

#include <string>
#include <vector>

namespace das::zip {

struct Entry {
    std::string name; // path inside the archive, with '/'
    data::Bytes content;
};

// Reads every file entry (folders are skipped). Throws data::DataError for
// archives we cannot read, such as encrypted or multi-part ones.
std::vector<Entry> read(data::ByteView archive);

} // namespace das::zip
