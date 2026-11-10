// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plays the games' WAV sound effects and their MIDI music (through our own
// synthesizer, das/audio/midi.hpp).
#pragma once

#include "das/audio/midi.hpp"
#include "das/data/binary.hpp"

#include <SDL3/SDL.h>

#include <mutex>
#include <vector>

namespace das::app {

class Audio {
public:
    Audio() = default;
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    ~Audio();

    // Opens the default output; without one, sounds are silently skipped.
    void open();
    // A WAV sound, or a MIDI song (which replaces the song playing).
    void play(const data::Bytes& sound);
    void stopAll();
    void stopMusic();
    bool musicPlaying();
    // 0 to 1.
    void setVolume(float volume);
    void setMusicVolume(float volume);
    // Frees sounds that finished playing.
    void update();

private:
    static constexpr int kMusicRate = 44100;
    static void SDLCALL feedMusic(void* self, SDL_AudioStream* stream, int additional, int total);

    SDL_AudioDeviceID device_ = 0;
    std::vector<SDL_AudioStream*> playing_;
    float effectsVolume_ = 1.0f;
    SDL_AudioStream* music_ = nullptr;
    std::mutex musicMutex_;
    audio::Synth synth_{kMusicRate};
    std::vector<float> musicBuffer_;
};

} // namespace das::app
