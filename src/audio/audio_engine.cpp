#include "audio/audio_engine.h"

#include <algorithm>
#include <cassert>

namespace bumpy {

AudioEngine::AudioEngine(const MidiSong& song, const AdLibBank& bank) : song_(song), bank_(bank) {}

void AudioEngine::start_music() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!music_) {
        music_.emplace(song_, bank_, /*loop=*/true);
        // MidiOplPlayer renders at the Opl2 native rate; catch a drift here rather
        // than as a subtle pitch/speed bug downstream.
        assert(music_->sample_rate() == kSampleRate);
    }
    music_playing_ = true;
}

void AudioEngine::stop_music() {
    std::lock_guard<std::mutex> lock(mutex_);
    music_playing_ = false;
}

void AudioEngine::play_sfx(std::uint8_t id) {
    if (id >= 0x16 || !kSfxPresets[id].used) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t slot = 0;
    std::uint64_t oldest_age = voice_age_[0];
    for (std::size_t i = 0; i < kVoiceCount; ++i) {
        if (!voices_[i].active()) {
            slot = i;
            break;
        }
        if (i == 0 || voice_age_[i] < oldest_age) {
            oldest_age = voice_age_[i];
            slot = i;
        }
    }
    voices_[slot].start(kSfxPresets[id]);
    voice_age_[slot] = ++age_counter_;
}

void AudioEngine::render(float* out, std::size_t frames) {
    std::fill(out, out + frames, 0.0f);

    std::lock_guard<std::mutex> lock(mutex_);
    if (music_playing_ && music_) {
        // MidiOplPlayer::render() overwrites (not adds), which is exactly
        // right here since `out` is freshly zeroed.
        music_->render(out, frames);
    }
    for (auto& voice : voices_) {
        if (voice.active()) {
            voice.render_add(out, frames);
        }
    }
}

}  // namespace bumpy
