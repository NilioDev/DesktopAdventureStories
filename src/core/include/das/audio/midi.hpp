// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standard MIDI files (the games' music) and a small synthesizer of our own
// that plays them: two-operator FM voices with a sound per General MIDI
// instrument family, and synthesized drums. See docs/music.md.
#pragma once

#include "das/data/binary.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace das::audio {

// A channel message (note on/off, controller, program, pitch bend) at a
// time in seconds from the start of the song.
struct MidiEvent {
    double time = 0;
    std::uint8_t status = 0; // message kind (high nibble) and channel
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

struct MidiSong {
    std::vector<MidiEvent> events; // in time order
    double length = 0;             // seconds, up to the last event
};

bool isMidi(const data::Bytes& bytes);
// Reads a format 0 or 1 file. Throws data::DataError when it is damaged.
MidiSong parseMidi(const data::Bytes& bytes);

class Synth {
public:
    explicit Synth(int sampleRate = 48000);

    // Starts a song from its beginning, replacing the one playing.
    void play(MidiSong song);
    void stop();
    bool playing() const { return playing_; }

    // Adds `frames` stereo frames (left, right, ...) to `out`.
    void render(float* out, int frames);

private:
    struct Channel {
        int program = 0;
        float volume = 100.0f / 127.0f;
        float expression = 1.0f;
        float pan = 0.5f;  // 0 left, 1 right
        float bend = 0.0f; // in semitones
        bool sustain = false;
    };
    struct Voice {
        bool active = false;
        int channel = 0;
        int note = 0;
        float velocity = 0;
        double carrier = 0; // phases, in turns
        double modulator = 0;
        double frequency = 0;
        float env = 0;    // amplitude envelope
        float modEnv = 1; // modulation depth envelope
        int stage = 0;    // 0 attack, 1 decay/sustain, 2 release
        bool held = true; // key still down
        int drum = -1;    // a drum sound, or -1
        double age = 0;   // seconds since the note began
        std::uint32_t noise = 1;
    };

    void apply(const MidiEvent& e);
    void noteOn(int channel, int note, int velocity);
    void noteOff(int channel, int note);
    float voiceSample(Voice& v);

    int rate_;
    MidiSong song_;
    std::size_t next_ = 0;
    double time_ = 0;
    bool playing_ = false;
    std::array<Channel, 16> channels_{};
    std::array<Voice, 32> voices_{};
};

} // namespace das::audio
