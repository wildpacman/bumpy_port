#pragma once
#include "audio/opl2.h"
#include "resources/adlib_bank.h"
#include "resources/midi_song.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bumpy {

// Sequences a parsed MidiSong onto 9 OPL2 melodic channels, loading AdLib patches
// from an AdLibBank per MIDI Program Change. See the audio design spec (Task 5,
// docs/superpowers/plans/2026-07-06-audio-sound-system.md) for the register
// formulas and the fractional sample-per-tick tempo clock this implements.
class MidiOplPlayer {
public:
    MidiOplPlayer(const MidiSong& song, const AdLibBank& bank, bool loop);

    // Fills `out` with `frames` mono samples at opl_.sample_rate(), advancing the
    // MIDI tick clock and dispatching due events as it goes.
    void render(float* out, std::size_t frames);
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    void reset();
    // The OPL2 native rate render() produces samples at (see Opl2::sample_rate()).
    [[nodiscard]] std::uint32_t sample_rate() const noexcept { return sample_rate_; }

private:
    struct Voice {
        bool active = false;
        int midi_channel = -1;
        int note = -1;
        std::uint64_t age = 0;
    };

    void dispatch(const MidiEvent& event);
    void note_on(int channel, int note);
    void note_off(int channel, int note);
    void all_notes_off();
    void load_instrument(int opl_channel, const AdLibInstrument& instrument);
    [[nodiscard]] double samples_per_tick() const;
    // Rewinds the tick clock to 0, re-derives the tempo, and dispatches every
    // event at tick 0 (used both at construction and on loop wraparound).
    void start_from_tick_zero();

    // Song data (copied so the player owns its lifetime independent of the caller).
    std::vector<MidiEvent> events_;
    std::vector<TempoChange> tempo_;
    AdLibBank bank_;
    int division_;
    bool loop_;

    Opl2 opl_;
    std::uint32_t sample_rate_;

    std::uint32_t current_tick_ = 0;
    std::uint32_t current_usec_per_qn_ = 500000;
    double sample_counter_ = 0.0;  // samples remaining until current_tick_ advances
    std::size_t event_cursor_ = 0;
    std::size_t tempo_cursor_ = 0;
    bool finished_ = false;

    std::array<Voice, 9> voices_{};
    std::array<int, 16> program_{};  // current Program Change per MIDI channel
    std::uint64_t age_counter_ = 0;
};

}  // namespace bumpy
