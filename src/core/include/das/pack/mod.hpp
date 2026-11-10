// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mods: small overlays on a game's pack that change or add zones, tiles,
// sounds, puzzles and characters. A mod is a folder (or a .zip of one) with
// a mod.json and only the files it changes, in the same formats as the pack.
// See docs/formats/mod.md.
#pragma once

#include "das/pack/pack.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace das::pack {

inline constexpr int kModFormatVersion = 1;
inline constexpr const char* kModManifestName = "mod.json";

struct ModInfo {
    std::string id;   // lower-case letters, digits, '-' and '_'
    std::string game; // "yoda" or "indy"
    std::string name;
    std::string version;
    std::string author;
    std::string description;
};

struct Mod {
    ModInfo info;
    Files files; // every file of the mod, mod.json included
    std::filesystem::path location; // folder or .zip it came from; empty when made in memory
};

// Whether `id` can name a mod (and its folder).
bool validModId(const std::string& id);

ModInfo readModInfo(const Files& files);
// Makes the mod.json for `info`.
data::Bytes writeModInfo(const ModInfo& info);

// Reads one mod from a folder or a .zip archive. A zip may hold the mod's
// files at its top or inside one folder.
Mod readMod(const std::filesystem::path& where);
// Every mod in a folder (subfolders and .zip files), sorted by id. Mods that
// cannot be read are left out; their errors go to `problems` when given.
std::vector<Mod> findMods(const std::filesystem::path& folder, std::vector<std::string>* problems = nullptr);

// Changes `pack` as the mod says. Throws data::DataError (with the file
// name) when the mod does not fit the pack, leaving `pack` partly changed.
void applyMod(Pack& pack, const Mod& mod);

// The mod that turns `original` into `changed`: only what differs. Both
// packs must be of the same game; `changed` may only add zones and tiles,
// not remove them.
Mod makeMod(const Pack& original, const Pack& changed, const ModInfo& info);

// Writes a mod into `folder` (replacing what was there), then sets
// `mod.location` to it.
void writeMod(Mod& mod, const std::filesystem::path& folder);

} // namespace das::pack
