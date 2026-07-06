#pragma once

#include <SDL3/SDL.h>

#include "audio/audio_engine.h"

#include <vector>

namespace bumpy {

// RAII wrapper around an SDL3 audio stream that pulls mixed samples from an
// AudioEngine. Opens a mono float32 stream at AudioEngine::kSampleRate on the
// default playback device and drives it from a pull callback (the audio
// thread calls back into `engine.render`); closes/destroys the stream on
// destruction. `engine` must outlive this object.
class SdlAudio {
public:
    explicit SdlAudio(AudioEngine& engine);
    ~SdlAudio();

    SdlAudio(const SdlAudio&) = delete;
    SdlAudio& operator=(const SdlAudio&) = delete;

private:
    static void callback(void* userdata, SDL_AudioStream* stream, int additional_amount,
                         int total_amount);

    AudioEngine& engine_;
    SDL_AudioStream* stream_{};
    // Scratch render buffer, reused across callback invocations. SDL only ever
    // drives one stream's callback from a single audio thread, so this needs
    // no locking.
    std::vector<float> scratch_;
};

}  // namespace bumpy
