#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace bumpy {

struct MidiEvent { std::uint32_t tick; std::uint8_t status; std::uint8_t data1; std::uint8_t data2; };
struct TempoChange { std::uint32_t tick; std::uint32_t usec_per_qn; };

// Standard MIDI File (BUMPY.MID, format 1, 7 tracks). All tracks merged into one
// tick-sorted channel-voice event stream + a tempo map. SysEx and non-tempo meta dropped.
class MidiSong {
public:
    static MidiSong load(const std::filesystem::path& path);
    static MidiSong from_bytes(std::vector<std::uint8_t> bytes);

    [[nodiscard]] int division() const noexcept { return division_; }
    [[nodiscard]] const std::vector<MidiEvent>& events() const noexcept { return events_; }
    [[nodiscard]] const std::vector<TempoChange>& tempo_map() const noexcept { return tempo_; }
    [[nodiscard]] std::uint32_t end_tick() const noexcept { return end_tick_; }

private:
    int division_{};
    std::uint32_t end_tick_{};
    std::vector<MidiEvent> events_;
    std::vector<TempoChange> tempo_;
};

}  // namespace bumpy
