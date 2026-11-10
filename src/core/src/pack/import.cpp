// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/pack/import.hpp"

#include "das/data/game_files.hpp"
#include "das/pack/names.hpp"
#include "das/util/zip.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>

namespace das::pack {

namespace fs = std::filesystem;
using data::Bytes;
using data::DataError;

namespace detail {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string soundFileName(const std::string& path) {
    const auto slash = path.find_last_of("\\/:");
    return lower(slash == std::string::npos ? path : path.substr(slash + 1));
}

} // namespace detail

namespace {

constexpr int kMaxFolderDepth = 3;

struct KnownGame {
    const char* id;
    const char* dataFile;
    const char* executable;
};

constexpr KnownGame kKnownGames[] = {
    {"yoda", "yodesk.dta", "yodesk.exe"},
    {"indy", "desktop.daw", "deskadv.exe"},
};

class FolderSource final : public Source {
public:
    explicit FolderSource(fs::path root) : root_(std::move(root)) {}

    std::vector<std::string> list() const override {
        std::vector<std::string> out;
        std::error_code ec;
        fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it.depth() >= kMaxFolderDepth) {
                it.disable_recursion_pending();
            }
            std::error_code fileEc;
            if (it->is_regular_file(fileEc)) {
                out.push_back(fs::relative(it->path(), root_, fileEc).generic_string());
            }
        }
        return out;
    }

    Bytes read(const std::string& path) const override { return data::readFile(root_ / fs::path(path)); }

private:
    fs::path root_;
};

class ZipSource final : public Source {
public:
    explicit ZipSource(std::vector<zip::Entry> entries) : entries_(std::move(entries)) {}

    std::vector<std::string> list() const override {
        std::vector<std::string> out;
        for (const auto& e : entries_) {
            out.push_back(e.name);
        }
        return out;
    }

    Bytes read(const std::string& path) const override {
        for (const auto& e : entries_) {
            if (e.name == path) {
                return e.content;
            }
        }
        throw DataError("archive has no file " + path);
    }

private:
    std::vector<zip::Entry> entries_;
};

std::string folderOf(const std::string& path) {
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string fileOf(const std::string& path) {
    const auto slash = path.rfind('/');
    return detail::lower(slash == std::string::npos ? path : path.substr(slash + 1));
}

} // namespace

std::unique_ptr<Source> openSource(const fs::path& where) {
    std::error_code ec;
    if (fs::is_directory(where, ec)) {
        return std::make_unique<FolderSource>(where);
    }
    if (fs::is_regular_file(where, ec)) {
        if (detail::lower(where.extension().string()) == ".zip") {
            return std::make_unique<ZipSource>(zip::read(data::readFile(where)));
        }
        // A data file itself: use the folder it is in.
        return std::make_unique<FolderSource>(where.has_parent_path() ? where.parent_path() : fs::path("."));
    }
    return nullptr;
}

std::vector<std::string> gamesIn(const Source& source) {
    const std::vector<std::string> paths = source.list();
    std::vector<std::string> out;
    for (const auto& known : kKnownGames) {
        if (std::any_of(paths.begin(), paths.end(), [&](const auto& p) { return fileOf(p) == known.dataFile; })) {
            out.emplace_back(known.id);
        }
    }
    return out;
}

std::vector<Pack> importGames(const Source& source, const std::vector<std::string>& only) {
    std::vector<std::string> paths = source.list();
    // Shallow paths first, so a file next to the data file wins over one with
    // the same name further down.
    std::stable_sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
        return std::count(a.begin(), a.end(), '/') < std::count(b.begin(), b.end(), '/');
    });
    std::vector<Pack> packs;
    for (const auto& known : kKnownGames) {
        if (!only.empty() && std::find(only.begin(), only.end(), known.id) == only.end()) {
            continue;
        }
        for (const auto& path : paths) {
            if (fileOf(path) != known.dataFile) {
                continue;
            }
            const std::string folder = folderOf(path);
            // Files of this install: in the data file's folder or one below
            // it (Yoda Stories keeps its sounds in "sfx").
            std::map<std::string, std::string> nearby;
            for (const auto& other : paths) {
                const std::string otherFolder = folderOf(other);
                if (otherFolder.rfind(folder, 0) == 0 &&
                    std::count(otherFolder.begin() + static_cast<std::ptrdiff_t>(folder.size()), otherFolder.end(),
                               '/') <= 1) {
                    nearby.emplace(fileOf(other), other);
                }
            }

            Pack pack;
            pack.data = data::parseGameData(source.read(path));
            pack.game = gameId(pack.data.layout());
            std::optional<data::Palette> palette;
            if (const auto exe = nearby.find(known.executable); exe != nearby.end()) {
                palette = data::findPalette(source.read(exe->second));
            }
            pack.palette = palette.value_or(data::greyscalePalette());
            for (const auto& name : pack.data.sounds) {
                const std::string file = detail::soundFileName(name);
                if (const auto it = nearby.find(file); it != nearby.end()) {
                    pack.sounds["sounds/" + file] = source.read(it->second);
                }
            }
            std::erase_if(packs, [&](const Pack& p) { return p.game == pack.game; });
            packs.push_back(std::move(pack));
        }
    }
    return packs;
}

std::vector<std::string> Library::games() const {
    std::vector<std::string> out;
    for (const char* game : {"yoda", "indy"}) {
        if (has(game)) {
            out.emplace_back(game);
        }
    }
    return out;
}

bool Library::has(const std::string& game) const {
    std::error_code ec;
    return fs::is_regular_file(folder(game) / kManifestName, ec);
}

Pack Library::load(const std::string& game) const { return decode(readFolder(folder(game))); }

void Library::install(const Pack& pack) const {
    const fs::path target = folder(pack.game);
    const fs::path staging = root_ / (pack.game + ".new");
    const fs::path old = root_ / (pack.game + ".old");
    fs::remove_all(staging);
    fs::remove_all(old);
    writeFolder(encode(pack), staging);
    std::error_code ec;
    if (fs::exists(target, ec)) {
        fs::rename(target, old);
    }
    fs::rename(staging, target);
    fs::remove_all(old, ec);
}

} // namespace das::pack
