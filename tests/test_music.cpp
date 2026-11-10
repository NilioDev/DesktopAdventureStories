// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "das/audio/midi.hpp"
#include "das/data/game_files.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace das;

namespace {

// A format 0 file: 96 ticks per quarter note, tempo 500000 us (120 bpm), a
// piano note of one quarter, then a drum hit using running status.
data::Bytes tinySong() {
    const std::vector<std::uint8_t> track = {
        0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20, // tempo
        0x00, 0xc0, 0x00,                         // program 0 on channel 0
        0x00, 0x90, 60, 100,                      // note on
        0x60, 0x80, 60, 0,                        // note off after 96 ticks
        0x00, 0x99, 36, 120,                      // drum on channel 9
        0x30, 36, 0,                              // running status: velocity 0 = off
        0x00, 0xff, 0x2f, 0x00,                   // end
    };
    data::Bytes out = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k', 0, 0, 0,
                       static_cast<std::uint8_t>(track.size())};
    out.insert(out.end(), track.begin(), track.end());
    return out;
}

} // namespace

DAS_TEST("a MIDI file is read into timed channel messages") {
    const auto song = audio::parseMidi(tinySong());
    DAS_CHECK(song.events.size() == 5);
    DAS_CHECK(song.events[1].status == 0x90 && song.events[1].data1 == 60);
    DAS_CHECK(std::abs(song.events[2].time - 0.5) < 1e-9); // one quarter at 120 bpm
    DAS_CHECK(song.events[4].status == 0x99 && song.events[4].data2 == 0);
    DAS_CHECK(std::abs(song.length - 0.75) < 1e-9);

    data::Bytes damaged = tinySong();
    damaged.resize(damaged.size() - 6);
    bool threw = false;
    try {
        (void)audio::parseMidi(damaged);
    } catch (const data::DataError&) {
        threw = true;
    }
    DAS_CHECK(threw);
}

DAS_TEST("the synthesizer plays a song to its end") {
    audio::Synth synth(8000);
    synth.play(audio::parseMidi(tinySong()));
    std::vector<float> buffer(2 * 8000 * 4, 0.0f);
    synth.render(buffer.data(), 8000 * 4);
    float loudest = 0;
    for (const float s : buffer) {
        DAS_CHECK(std::isfinite(s) && std::abs(s) <= 1.0f);
        loudest = std::max(loudest, std::abs(s));
    }
    DAS_CHECK(loudest > 0.05f);
    DAS_CHECK(!synth.playing()); // the notes have died away within four seconds
}

DAS_TEST("the games' music files can be read") {
    const char* dir = std::getenv("DAS_GAMEDATA_DIR");
    if (!dir) {
        DAS_SKIP("DAS_GAMEDATA_DIR not set");
    }
    int count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        std::string ext = entry.path().extension().string();
        for (auto& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext != ".mid") {
            continue;
        }
        const auto song = audio::parseMidi(data::readFile(entry.path()));
        DAS_CHECK(!song.events.empty());
        DAS_CHECK(song.length > 0.5);
        ++count;
    }
    DAS_CHECK(count > 0);
}
