// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/data/game_files.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>

namespace das::data {

namespace fs = std::filesystem;

namespace {

struct KnownGame {
    Layout game;
    const char* dataFile;
    const char* executable;
};

constexpr KnownGame kKnownGames[] = {
    {Layout::Yoda, "yodesk.dta", "yodesk.exe"},
    {Layout::Indy, "desktop.daw", "deskadv.exe"},
};

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::optional<fs::path> findInFolder(const fs::path& folder, const std::string& wanted) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (entry.is_regular_file(ec) && lower(entry.path().filename().string()) == wanted) {
            return entry.path();
        }
    }
    return std::nullopt;
}

void scanFolder(const fs::path& folder, std::vector<GameFiles>& found) {
    for (const auto& known : kKnownGames) {
        if (auto data = findInFolder(folder, known.dataFile)) {
            GameFiles files{known.game, *data, {}};
            if (auto exe = findInFolder(folder, known.executable)) {
                files.executable = *exe;
            }
            found.push_back(std::move(files));
        }
    }
}

} // namespace

std::vector<GameFiles> findGameFiles(const fs::path& where) {
    std::vector<GameFiles> found;
    std::error_code ec;
    if (fs::is_regular_file(where, ec)) {
        scanFolder(where.parent_path().empty() ? fs::path(".") : where.parent_path(), found);
        std::erase_if(found, [&](const GameFiles& f) { return !fs::equivalent(f.dataFile, where, ec); });
        return found;
    }
    if (!fs::is_directory(where, ec)) {
        return found;
    }
    scanFolder(where, found);
    for (const auto& entry : fs::directory_iterator(where, ec)) {
        if (entry.is_directory(ec)) {
            scanFolder(entry.path(), found);
        }
    }
    return found;
}

Bytes readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw DataError("cannot open " + path.string());
    }
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace das::data
