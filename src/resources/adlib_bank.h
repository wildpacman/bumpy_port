#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace bumpy {

struct AdLibOperator {
    std::uint8_t ksl, mult, feedback, attack, sustain, eg, decay, release, level, am, vib, ksr, connection;
};
struct AdLibInstrument {
    std::uint8_t mode{};
    std::uint8_t perc_voice{};
    AdLibOperator mod{};
    AdLibOperator car{};
    std::uint8_t wave_mod{};
    std::uint8_t wave_car{};
    std::string name;  // from the bank's name index (empty if unnamed)
};

// AdLib .BNK instrument bank (BUMPY.BNK). Header "ADLIB-", a 12-byte name index and
// 30-byte instrument records. Renders the intro MIDI on OPL2 (see the audio design spec).
class AdLibBank {
public:
    static AdLibBank load(const std::filesystem::path& path);
    static AdLibBank from_bytes(std::vector<std::uint8_t> bytes);

    [[nodiscard]] std::size_t size() const noexcept { return instruments_.size(); }
    [[nodiscard]] const AdLibInstrument& instrument(std::size_t index) const { return instruments_.at(index); }
    [[nodiscard]] const AdLibInstrument* by_name(std::string_view name) const;

    // Resolve a MIDI program number to its patch the way the original driver does
    // (FUN_1000_8b81): the .BNK name index is a program-ordered table whose record N
    // (name "rolNNN") holds the storage slot of program N's patch. Indexing the raw
    // instrument array by program number is WRONG -- the storage order is scrambled.
    [[nodiscard]] const AdLibInstrument& patch_for_program(int program) const;

private:
    std::vector<AdLibInstrument> instruments_;
    std::vector<std::uint16_t> program_index_;  // name-index record N -> instrument slot
};

}  // namespace bumpy
