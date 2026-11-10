// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.hpp"

namespace das::app {

Audio::~Audio() {
    stopAll();
    if (music_) {
        SDL_DestroyAudioStream(music_);
    }
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
        return;
    }
    // Music: SDL asks for more whenever it runs low, and the synthesizer
    // makes it.
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kMusicRate};
    music_ = SDL_CreateAudioStream(&spec, nullptr);
    if (music_) {
        SDL_SetAudioStreamGetCallback(music_, feedMusic, this);
        if (!SDL_BindAudioStream(device_, music_)) {
            SDL_DestroyAudioStream(music_);
            music_ = nullptr;
        }
    }
}

void SDLCALL Audio::feedMusic(void* self, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto& audio = *static_cast<Audio*>(self);
    const int frames = additional / static_cast<int>(2 * sizeof(float));
    if (frames <= 0) {
        return;
    }
    std::lock_guard lock(audio.musicMutex_);
    audio.musicBuffer_.assign(static_cast<std::size_t>(frames) * 2, 0.0f);
    audio.synth_.render(audio.musicBuffer_.data(), frames);
    SDL_PutAudioStreamData(stream, audio.musicBuffer_.data(),
                           static_cast<int>(audio.musicBuffer_.size() * sizeof(float)));
}

void Audio::stopMusic() {
    std::lock_guard lock(musicMutex_);
    synth_.stop();
}

bool Audio::musicPlaying() {
    std::lock_guard lock(musicMutex_);
    return synth_.playing();
}

void Audio::setMusicVolume(float volume) {
    if (music_) {
        SDL_SetAudioStreamGain(music_, volume);
    }
}

void Audio::setVolume(float volume) {
    effectsVolume_ = volume;
    for (auto* stream : playing_) {
        SDL_SetAudioStreamGain(stream, volume);
    }
}

void Audio::play(const data::Bytes& wav) {
    if (device_ == 0 || wav.empty()) {
        return;
    }
    if (audio::isMidi(wav)) {
        try {
            audio::MidiSong song = audio::parseMidi(wav);
            std::lock_guard lock(musicMutex_);
            synth_.play(std::move(song));
        } catch (const std::exception& e) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Cannot play music: %s", e.what());
        }
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
        SDL_SetAudioStreamGain(stream, effectsVolume_);
        SDL_PutAudioStreamData(stream, samples, static_cast<int>(length));
        SDL_FlushAudioStream(stream);
        playing_.push_back(stream);
    } else if (stream) {
        SDL_DestroyAudioStream(stream);
    }
    SDL_free(samples);
}

void Audio::stopAll() {
    stopMusic();
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
