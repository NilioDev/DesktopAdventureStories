// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one-time import: finds the original games in a folder or zip archive
// and turns each into a pack (see pack.hpp). A Library keeps the imported
// packs in the app's own data folder.
#pragma once

#include "das/pack/pack.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace das::pack {

// The files of an original install, by '/'-separated relative path.
class Source {
public:
    virtual ~Source() = default;
    virtual std::vector<std::string> list() const = 0;
    virtual data::Bytes read(const std::string& path) const = 0;
};

// A folder (searched a few levels deep) or a .zip archive. Returns nullptr
// when `where` is neither.
std::unique_ptr<Source> openSource(const std::filesystem::path& where);
// A .zip archive already read into memory (for example from a file picker
// that hands out contents, not paths). Throws when it is no zip.
std::unique_ptr<Source> zipSource(const data::Bytes& archive);
// Loose files already read into memory, by file name.
std::unique_ptr<Source> filesSource(std::vector<std::pair<std::string, data::Bytes>> files);
// Whether bytes start like a .zip archive.
bool looksLikeZip(const data::Bytes& bytes);

// Ids ("yoda", "indy") of the games whose data file is in the source, found
// by file name only.
std::vector<std::string> gamesIn(const Source& source);

// One pack for every game data file in the source; with `only`, just for
// those games.
std::vector<Pack> importGames(const Source& source, const std::vector<std::string>& only = {});

class Library {
public:
    explicit Library(std::filesystem::path root) : root_(std::move(root)) {}

    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path folder(const std::string& game) const { return root_ / game; }

    // Ids of the installed packs ("yoda", "indy"), in a fixed order.
    std::vector<std::string> games() const;
    bool has(const std::string& game) const;
    Pack load(const std::string& game) const;
    // Writes the pack, replacing an earlier copy only once the new one is
    // complete.
    void install(const Pack& pack) const;

private:
    std::filesystem::path root_;
};

} // namespace das::pack
