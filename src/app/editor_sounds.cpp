// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's Sounds tab: listen to the game's sounds, replace them with
// files of your own and add new ones for scripts and weapons to play.
#include "editor.hpp"

#include "das/pack/names.hpp"
#include "das/script/opcodes.hpp"
#include "das/util/text.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace das::app {

namespace {

constexpr float kPad = 6;

std::string fileOf(const std::string& name) { return pack::detail::soundFileName(text::windows1252ToUtf8(name)); }

// SDL gives paths in UTF-8.
std::filesystem::path utf8Path(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string utf8Name(const std::filesystem::path& p) {
    const auto u = p.filename().u8string();
    return std::string(u.begin(), u.end());
}

bool endsWith(const std::string& s, const char* end) {
    const std::string e(end);
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}

// The open-file dialog answers on its own time, maybe on another thread.
void SDLCALL fileChosen(void* userdata, const char* const* files, int) {
    auto* holder = static_cast<std::shared_ptr<Editor::FileRequest>*>(userdata);
    {
        std::lock_guard lock((*holder)->mutex);
        if (files && files[0]) {
            (*holder)->path = files[0];
        }
        (*holder)->done = true;
    }
    delete holder;
}

} // namespace

void Editor::chooseSoundFile(int replace) {
    fileRequest_ = std::make_shared<FileRequest>();
    fileRequest_->replace = replace;
    static const SDL_DialogFileFilter filters[] = {{"Sounds and music", "wav;mid"}};
    SDL_ShowOpenFileDialog(fileChosen, new std::shared_ptr<FileRequest>(fileRequest_), nullptr, filters, 1, nullptr,
                           false);
}

void Editor::takeSoundFile() {
    if (!fileRequest_) {
        return;
    }
    std::string path;
    int replace = -1;
    {
        std::lock_guard lock(fileRequest_->mutex);
        if (!fileRequest_->done) {
            return;
        }
        path = fileRequest_->path;
        replace = fileRequest_->replace;
    }
    fileRequest_.reset();
    if (path.empty()) {
        return;
    }
    std::ifstream in(utf8Path(path), std::ios::binary);
    data::Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string file = pack::detail::lower(utf8Name(utf8Path(path)));
    if (!in.good() && !in.eof()) {
        bytes.clear();
    }
    if (bytes.empty() || !(endsWith(file, ".wav") || endsWith(file, ".mid"))) {
        notice_ = "Not used: choose a .wav sound or a .mid song.";
        noticeAt_ = SDL_GetTicks();
        return;
    }
    auto& names = host_.data->sounds;
    if (replace >= 0 && static_cast<std::size_t>(replace) < names.size()) {
        // Keeps the sound's own name, so scripts and weapons still find it.
        const std::string key = "sounds/" + fileOf(names[static_cast<std::size_t>(replace)]);
        rememberSound(key);
        host_.sounds[key] = std::move(bytes);
        sound_ = replace;
        notice_ = "Sound " + std::to_string(replace) + " replaced.";
    } else {
        // A new name: the sound's number and the file's name. Mods add new
        // sounds in the order of their file names, so the number keeps it.
        char number[16];
        std::snprintf(number, sizeof number, "%04zu-", names.size());
        const std::string name = number + file;
        const std::string key = "sounds/" + name;
        rememberSound(key);
        names.push_back(text::utf8ToWindows1252(name));
        host_.sounds[key] = std::move(bytes);
        sound_ = static_cast<int>(names.size()) - 1;
        soundScroll_ = 1e9f;
        notice_ = "Sound " + std::to_string(sound_) + " added: " + name + ".";
    }
    noticeAt_ = SDL_GetTicks();
    if (host_.soundsChanged) {
        host_.soundsChanged(host_.sounds);
    }
}

void Editor::rememberSound(const std::string& key) {
    remember(-1, -1);
    auto& s = undo_.back();
    s.sound = true;
    s.soundNames = host_.data->sounds;
    s.soundKey = key;
    const auto it = host_.sounds.find(key);
    if (it != host_.sounds.end()) {
        s.soundBytes = it->second;
    }
}

void Editor::soundsTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right) {
    auto& d = *host_.data;
    sound_ = std::clamp(sound_, 0, std::max(0, static_cast<int>(d.sounds.size()) - 1));

    // The list.
    {
        const SDL_FRect& r = left;
        ui::fill(renderer_, r, ui::color::panel);
        ui::fill(renderer_, {r.x + r.w - 1, r.y, 1, r.h}, ui::color::border);
        const float x = r.x + kPad;
        const float w = r.w - 2 * kPad;
        float y = r.y + kPad;
        ui::text(renderer_, x, y, "Sounds", ui::color::accent);
        y += 14;
        const float buttonH = gui::kRow + kPad;
        const SDL_FRect list{r.x, y, r.w - 1, r.y + r.h - y - buttonH - kPad};
        const float rowH = gui::kRow + 2;
        gui_.beginScroll(list, soundScroll_, static_cast<float>(d.sounds.size()) * rowH);
        float ry = list.y - soundScroll_;
        for (int i = 0; i < static_cast<int>(d.sounds.size()); ++i) {
            const SDL_FRect row{x, ry, w - 4, gui::kRow};
            ry += rowH;
            if (!gui_.visible(row)) {
                continue;
            }
            const std::string file = fileOf(d.sounds[static_cast<std::size_t>(i)]);
            if (gui_.button("sound-" + std::to_string(i), row, "",
                            "Sound " + std::to_string(i) + ": " + file + ". Click to choose it.", i == sound_)) {
                sound_ = i;
            }
            ui::text(renderer_, row.x + 4, row.y + 3, gui::fitText(std::to_string(i) + " " + file, row.w - 8),
                     i == sound_ ? ui::color::accent : ui::color::text);
        }
        gui_.endScroll();
        if (gui_.button("sound-add", {x, r.y + r.h - kPad - gui::kRow, w, gui::kRow}, "Add a sound...",
                        "Choose a .wav sound or .mid song on your computer to add at the end of the list.", false,
                        !fileRequest_)) {
            chooseSoundFile(-1);
        }
    }

    // The sound.
    const SDL_FRect& r = center;
    const float x = r.x + kPad * 2;
    const float w = std::min(r.w - 4 * kPad, 520.0f);
    float y = r.y + kPad;
    if (d.sounds.empty()) {
        gui_.paragraph(x, y, w, "This game has no sounds.", ui::color::faint);
    } else {
        const std::string file = fileOf(d.sounds[static_cast<std::size_t>(sound_)]);
        const auto it = host_.sounds.find("sounds/" + file);
        const bool has = it != host_.sounds.end() && !it->second.empty();
        ui::text(renderer_, x, y, "Sound " + std::to_string(sound_), ui::color::accent);
        y += 14;
        ui::text(renderer_, x, y, file, ui::color::text);
        y += 12;
        const std::string what = endsWith(file, ".mid") ? "A song (MIDI music)." : "A sound effect.";
        ui::text(renderer_, x, y,
                 has ? what + " " + std::to_string(it->second.size() / 1024 + 1) + " KB." : "The file is missing.",
                 has ? ui::color::dim : ui::color::danger);
        y += 16;
        const float half = (w - 4) / 2;
        if (gui_.button("sound-play", {x, y, half, gui::kRow}, "Listen", "Play the sound.", false,
                        has && static_cast<bool>(host_.playSound))) {
            host_.playSound(it->second);
        }
        if (gui_.button("sound-replace", {x + half + 4, y, half, gui::kRow}, "Replace with a file...",
                        "Choose a .wav sound or .mid song on your computer to use instead. Scripts and weapons that "
                        "play this sound then play the new one.",
                        false, !fileRequest_)) {
            chooseSoundFile(sound_);
        }
        y += gui::kRow + kPad + 4;

        // Who plays it.
        int lines = 0;
        int zones = 0;
        const auto layout = d.layout();
        const int playOp = script::opcode(layout, script::Instruction::PlaySound);
        for (const auto& z : d.zones) {
            int here = 0;
            if (z.actions) {
                for (const auto& a : *z.actions) {
                    for (const auto& l : a.instructions) {
                        if (static_cast<int>(l.opcode) == playOp && l.args[0] == sound_) {
                            ++here;
                        }
                    }
                }
            }
            lines += here;
            zones += here > 0 ? 1 : 0;
        }
        std::vector<int> weapons;
        for (const auto& v : d.characterWeapons) {
            if (v.reference == sound_ && v.character < characters_.size() && characters_[v.character].type == 4) {
                weapons.push_back(v.character);
            }
        }
        std::string used = lines == 0 ? "No script plays it."
                                      : std::to_string(lines) + (lines == 1 ? " script line" : " script lines") +
                                            " in " + std::to_string(zones) + (zones == 1 ? " zone play it." : " zones play it.");
        for (const int c : weapons) {
            used += " The weapon " + characterName(c) + " makes it.";
        }
        y += gui_.paragraph(x, y, w, used, ui::color::dim, 4) + kPad;
    }

    // Help.
    ui::fill(renderer_, right, ui::color::panel);
    ui::fill(renderer_, {right.x, right.y, 1, right.h}, ui::color::border);
    gui_.paragraph(right.x + kPad, right.y + kPad, right.w - 2 * kPad,
                   "Sounds are played by scripts (Play sound, on the Scripts tab) by their number, and by weapons "
                   "(on the Characters tab). Songs (.mid) play as music. A replaced sound keeps its number and name, "
                   "so everything that played it plays the new one. Added sounds get the next number.",
                   ui::color::dim, 20);
}

} // namespace das::app
