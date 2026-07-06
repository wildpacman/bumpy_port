// Offline audio render/verification harness (dev tool, not shipped in the game).
// Renders the intro music and each SFX preset to WAV via the real audio engine, and
// prints the measured SFX durations so they can be checked against the recovered
// hardware timings. Usage: run from the repo root (needs BUMPY.MID / BUMPY.BNK).
#include "audio/audio_engine.h"
#include "audio/speaker_sfx.h"
#include "resources/adlib_bank.h"
#include "resources/midi_song.h"
#include "resources/sfx_tables.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kRate = bumpy::AudioEngine::kSampleRate;

void write_wav(const std::string& path, const std::vector<float>& mono) {
    std::vector<std::int16_t> pcm(mono.size());
    for (std::size_t i = 0; i < mono.size(); ++i) {
        float s = mono[i];
        s = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
        pcm[i] = static_cast<std::int16_t>(s * 32767.0f);
    }
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(pcm.size() * 2);
    const std::uint32_t byte_rate = kRate * 2;
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + data_bytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(kRate); u32(byte_rate); u16(2); u16(16);
    f.write("data", 4); u32(data_bytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(data_bytes));
}
}  // namespace

int main() {
    const auto song = bumpy::MidiSong::load("BUMPY.MID");
    const auto bank = bumpy::AdLibBank::load("BUMPY.BNK");

    // --- music ---
    {
        bumpy::AudioEngine engine(song, bank);
        engine.start_music();
        std::vector<float> buf(kRate * 12);  // 12 s
        engine.render(buf.data(), buf.size());
        write_wav("scratch_music.wav", buf);
        std::printf("music -> scratch_music.wav (12 s)\n");
    }

    // --- SFX: measure raw voice duration + render through the engine (with low-pass) ---
    const std::uint8_t ids[] = {0x01, 0x02, 0x03, 0x08, 0x0b};
    for (std::uint8_t id : ids) {
        // duration via the bare voice
        bumpy::SpeakerVoice v;
        v.start(bumpy::kSfxPresets[id]);
        std::vector<float> probe(kRate * 3, 0.0f);
        std::size_t last = 0;
        for (std::size_t i = 0; i < probe.size() && v.active(); ++i) {
            v.render_add(&probe[i], 1);
            if (probe[i] != 0.0f) last = i;
        }
        const double dur_ms = 1000.0 * static_cast<double>(last + 1) / kRate;

        // audible render through the engine (low-pass applied)
        bumpy::AudioEngine engine(song, bank);
        engine.play_sfx(id);
        std::vector<float> buf(kRate * 2, 0.0f);
        engine.render(buf.data(), buf.size());
        char name[64];
        std::snprintf(name, sizeof(name), "scratch_sfx_%02x.wav", id);
        write_wav(name, buf);
        std::printf("sfx 0x%02x: %.1f ms  -> %s\n", id, dur_ms, name);
    }
    return 0;
}
