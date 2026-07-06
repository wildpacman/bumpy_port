#include "platform_sdl3/sdl_audio.h"

#include <stdexcept>

namespace bumpy {

void SdlAudio::callback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                        int /*total_amount*/) {
    if (additional_amount <= 0) {
        return;
    }
    auto* self = static_cast<SdlAudio*>(userdata);
    const std::size_t frames = static_cast<std::size_t>(additional_amount) / sizeof(float);
    if (frames == 0) {
        return;
    }
    if (self->scratch_.size() < frames) {
        self->scratch_.resize(frames);
    }
    self->engine_.render(self->scratch_.data(), frames);
    SDL_PutAudioStreamData(stream, self->scratch_.data(),
                           static_cast<int>(frames * sizeof(float)));
}

SdlAudio::SdlAudio(AudioEngine& engine) : engine_(engine) {
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = static_cast<int>(AudioEngine::kSampleRate);
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlAudio::callback, this);
    if (!stream_) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        throw std::runtime_error(SDL_GetError());
    }
}

SdlAudio::~SdlAudio() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
    }
}

}  // namespace bumpy
