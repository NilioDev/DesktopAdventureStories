// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plays the games' WAV sound effects. MIDI music comes later.
#pragma once

#include "das/data/binary.hpp"

#include <SDL3/SDL.h>

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
    void play(const data::Bytes& wav);
    void stopAll();
    // 0 to 1.
    void setVolume(float volume);
    // Frees sounds that finished playing.
    void update();

private:
    SDL_AudioDeviceID device_ = 0;
    std::vector<SDL_AudioStream*> playing_;
};

} // namespace das::app
