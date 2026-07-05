#include "resources/midi_song.h"
#include "resources/binary_reader.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace bumpy {
namespace {
std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
// Read an SMF variable-length quantity; advances `pos`.
std::uint32_t read_vlq(const std::vector<std::uint8_t>& b, std::size_t& pos) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint8_t c = b.at(pos++);
        v = (v << 7) | (c & 0x7F);
        if (!(c & 0x80)) break;
    }
    return v;
}
}  // namespace

MidiSong MidiSong::load(const std::filesystem::path& path) { return from_bytes(read_binary_file(path)); }

MidiSong MidiSong::from_bytes(std::vector<std::uint8_t> b) {
    if (b.size() < 14 || std::memcmp(&b[0], "MThd", 4) != 0) throw std::runtime_error("BUMPY.MID: no MThd");
    const std::uint16_t ntrks = static_cast<std::uint16_t>((b[10] << 8) | b[11]);
    MidiSong song;
    song.division_ = (b[12] << 8) | b[13];

    std::size_t pos = 8 + be32(&b[4]);  // past the header chunk
    for (std::uint16_t t = 0; t < ntrks; ++t) {
        if (pos + 8 > b.size() || std::memcmp(&b[pos], "MTrk", 4) != 0) break;
        const std::uint32_t len = be32(&b[pos + 4]);
        std::size_t p = pos + 8;
        const std::size_t end = p + len;
        std::uint32_t tick = 0;
        std::uint8_t running = 0;
        while (p < end) {
            tick += read_vlq(b, p);
            song.end_tick_ = std::max(song.end_tick_, tick);
            std::uint8_t status = b.at(p);
            if (status & 0x80) { ++p; if (status < 0xF0) running = status; }
            else status = running;  // running status
            if (status == 0xFF) {           // meta
                std::uint8_t type = b.at(p++);
                std::uint32_t mlen = read_vlq(b, p);
                if (type == 0x51 && mlen == 3)
                    song.tempo_.push_back({tick, static_cast<std::uint32_t>((b.at(p) << 16) | (b.at(p + 1) << 8) | b.at(p + 2))});
                p += mlen;
            } else if (status == 0xF0 || status == 0xF7) {  // sysex
                std::uint32_t mlen = read_vlq(b, p);
                p += mlen;
            } else {                        // channel voice
                const std::uint8_t hi = status & 0xF0;
                const std::uint8_t d1 = b.at(p++);
                const std::uint8_t d2 = (hi == 0xC0 || hi == 0xD0) ? 0 : b.at(p++);
                song.events_.push_back({tick, status, d1, d2});
            }
        }
        pos = end;
    }
    std::stable_sort(song.events_.begin(), song.events_.end(),
                     [](const MidiEvent& a, const MidiEvent& c) { return a.tick < c.tick; });
    std::stable_sort(song.tempo_.begin(), song.tempo_.end(),
                     [](const TempoChange& a, const TempoChange& c) { return a.tick < c.tick; });
    if (song.tempo_.empty() || song.tempo_.front().tick != 0)
        song.tempo_.insert(song.tempo_.begin(), {0, 500000});
    return song;
}

}  // namespace bumpy
