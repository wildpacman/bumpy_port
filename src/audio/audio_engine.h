#pragma once
#include "audio/midi_opl_player.h"
#include "audio/speaker_sfx.h"
#include "resources/adlib_bank.h"
#include "resources/midi_song.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace bumpy {

// Mixes the intro-music OPL2 player with a fixed pool of PC-speaker SFX voices
// into one mono output stream at the shared engine sample rate. `start_music`/
// `stop_music`/`play_sfx` are expected to be called from the game thread while
// `render` runs on the audio thread; a mutex guards the shared music-enabled
// flag + voice pool, held only briefly on either side (the (heavier)
// MidiOplPlayer itself is constructed under the lock, in `start_music`, since
// that is what keeps it from racing a concurrent `render`).
class AudioEngine {
public:
    static constexpr std::uint32_t kSampleRate = 49715;
    static constexpr std::size_t kVoiceCount = 6;
    // One-pole low-pass cutoff modelling the PC-speaker cone. The 1-bit sweep output is
    // otherwise a hard square whose high harmonics (and >Nyquist aliasing on the fast
    // sweeps) sound harsh/"частотный"; the real beeper rolls them off. Tunable by ear.
    static constexpr double kSpeakerLowpassHz = 5000.0;

    // `song`/`bank` must outlive the engine (the caller -- typically `main` --
    // owns them for the program's lifetime); the engine only ever reads them
    // to (re)construct the music player.
    AudioEngine(const MidiSong& song, const AdLibBank& bank);

    // (Re)starts/unmutes the looping intro-music player. Lazily constructs it
    // on first use; a subsequent call after `stop_music` resumes playback from
    // wherever the tick clock was left (stop_music pauses, it does not rewind).
    void start_music();
    // Mutes the music player. It keeps its position, so a following
    // `start_music` resumes rather than restarts.
    void stop_music();

    // Starts `kSfxPresets[id]` on an inactive voice from the pool, or steals
    // the oldest active one if all are busy. Ignores unknown/unused ids.
    void play_sfx(std::uint8_t id);

    // Enable/disable the SFX bus (Tab overlay AUDIO page). When disabled, play_sfx
    // starts no voice. Music is unaffected (it has its own start/stop).
    void set_sfx_enabled(bool enabled);

    // Audio-thread entry point: zeroes `out`, then mixes in the music (if
    // playing) and every active SFX voice.
    void render(float* out, std::size_t frames);

private:
    const MidiSong& song_;
    const AdLibBank& bank_;

    std::mutex mutex_;
    std::optional<MidiOplPlayer> music_;
    bool music_playing_ = false;
    bool sfx_enabled_ = true;

    std::array<SpeakerVoice, kVoiceCount> voices_{};
    std::array<std::uint64_t, kVoiceCount> voice_age_{};
    std::uint64_t age_counter_ = 0;

    // SFX bus: voices mix here, get low-pass filtered as one, then add to the output.
    std::vector<float> sfx_scratch_;
    float sfx_lowpass_ = 0.0f;  // persistent one-pole filter state
};

static_assert(AudioEngine::kSampleRate == SpeakerVoice::kSampleRate,
              "SFX and engine sample rates must match");

}  // namespace bumpy
