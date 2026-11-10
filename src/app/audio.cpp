// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.hpp"

namespace das::app {

Audio::~Audio() {
    stopAll();
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
    }
}

void Audio::open() {
    if (device_ != 0) {
        return;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No audio: %s", SDL_GetError());
        return;
    }
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (device_ == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No audio device: %s", SDL_GetError());
    }
}

void Audio::setVolume(float volume) {
    if (device_ != 0) {
        SDL_SetAudioDeviceGain(device_, volume);
    }
}

void Audio::play(const data::Bytes& wav) {
    if (device_ == 0 || wav.empty()) {
        return;
    }
    SDL_AudioSpec spec{};
    Uint8* samples = nullptr;
    Uint32 length = 0;
    SDL_IOStream* io = SDL_IOFromConstMem(wav.data(), wav.size());
    if (!SDL_LoadWAV_IO(io, true, &spec, &samples, &length)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot play sound: %s", SDL_GetError());
        return;
    }
    SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, nullptr);
    if (stream && SDL_BindAudioStream(device_, stream)) {
        SDL_PutAudioStreamData(stream, samples, static_cast<int>(length));
        SDL_FlushAudioStream(stream);
        playing_.push_back(stream);
    } else if (stream) {
        SDL_DestroyAudioStream(stream);
    }
    SDL_free(samples);
}

void Audio::stopAll() {
    for (auto* stream : playing_) {
        SDL_DestroyAudioStream(stream);
    }
    playing_.clear();
}

void Audio::update() {
    std::erase_if(playing_, [](SDL_AudioStream* stream) {
        if (SDL_GetAudioStreamQueued(stream) > 0) {
            return false;
        }
        SDL_DestroyAudioStream(stream);
        return true;
    });
}

} // namespace das::app
