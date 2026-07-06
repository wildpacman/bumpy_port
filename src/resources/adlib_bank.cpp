#include "resources/adlib_bank.h"
#include "resources/binary_reader.h"
#include <cstring>
#include <stdexcept>

namespace bumpy {
namespace {
std::uint16_t u16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t{p[3]} << 24));
}
AdLibOperator read_op(const std::uint8_t* f) {
    return {f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12]};
}
}  // namespace

AdLibBank AdLibBank::load(const std::filesystem::path& path) {
    return from_bytes(read_binary_file(path));
}

AdLibBank AdLibBank::from_bytes(std::vector<std::uint8_t> b) {
    if (b.size() < 28 || std::memcmp(&b[2], "ADLIB-", 6) != 0)
        throw std::runtime_error("BUMPY.BNK: bad header");
    const std::uint16_t total = u16(&b[10]);
    const std::uint32_t name_off = u32(&b[12]);
    const std::uint32_t inst_off = u32(&b[16]);
    if (name_off + std::size_t{total} * 12 > b.size() || inst_off + std::size_t{total} * 30 > b.size())
        throw std::runtime_error("BUMPY.BNK: truncated");

    AdLibBank bank;
    bank.instruments_.resize(total);
    for (std::size_t i = 0; i < total; ++i) {
        const std::uint8_t* r = &b[inst_off + i * 30];
        AdLibInstrument& in = bank.instruments_[i];
        in.mode = r[0];
        in.perc_voice = r[1];
        in.mod = read_op(r + 2);
        in.car = read_op(r + 15);
        in.wave_mod = r[28];
        in.wave_car = r[29];
    }
    // The name index doubles as the program table: record N (in file order) holds the
    // storage slot of program N (its name is "rolNNN"). Keep the slot per record so a
    // MIDI program number can be resolved to its patch (patch_for_program).
    bank.program_index_.resize(total);
    for (std::size_t i = 0; i < total; ++i) {
        const std::uint8_t* n = &b[name_off + i * 12];
        const std::uint16_t idx = u16(n);
        bank.program_index_[i] = idx;
        const std::uint8_t used = n[2];  // deleted records keep a mangled name but used==0
        if (used == 0 || idx >= total) continue;
        const char* name = reinterpret_cast<const char*>(n + 3);
        std::size_t len = 0;
        while (len < 9 && name[len] != '\0') ++len;
        bank.instruments_[idx].name.assign(name, len);
    }
    return bank;
}

const AdLibInstrument& AdLibBank::patch_for_program(int program) const {
    if (program < 0 || program_index_.empty() || instruments_.empty()) {
        return instruments_.at(0);
    }
    const std::uint16_t slot =
        program_index_[static_cast<std::size_t>(program) % program_index_.size()];
    return instruments_.at(slot % instruments_.size());
}

const AdLibInstrument* AdLibBank::by_name(std::string_view name) const {
    for (const auto& in : instruments_)
        if (in.name == name) return &in;
    return nullptr;
}

}  // namespace bumpy
