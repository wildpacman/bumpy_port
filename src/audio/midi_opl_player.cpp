#include "audio/midi_opl_player.h"
#include <algorithm>
#include <cmath>

namespace bumpy {
namespace {
// Standard OPL2 channel -> modulator operator-slot map (channels 0..8); the
// carrier operator for channel `ch` is always `kOpModSlot[ch] + 3`.
constexpr std::array<int, 9> kOpModSlot = {0, 1, 2, 8, 9, 10, 16, 17, 18};

// note -> (fnum, block) from an equal-tempered table, per the design spec.
void note_to_fnum_block(int note, int& fnum, int& block) {
    const int semitone = ((note % 12) + 12) % 12;
    const double raw_fnum = 172.86 * std::pow(2.0, semitone / 12.0);
    fnum = std::clamp(static_cast<int>(std::lround(raw_fnum)), 0, 1023);
    block = std::clamp(note / 12 - 1, 0, 7);
}
}  // namespace

MidiOplPlayer::MidiOplPlayer(const MidiSong& song, const AdLibBank& bank, bool loop)
    : events_(song.events()),
      tempo_(song.tempo_map()),
      bank_(bank),
      division_(song.division()),
      loop_(loop),
      sample_rate_(opl_.sample_rate()) {
    program_.fill(0);
    // FUN_1000_8b45 pre-assigns program = channel+1 to channels 0..5 before the song
    // plays, so a channel that never sends its own Program Change (e.g. channel 0, the
    // main melody) still gets a real patch instead of program 0.
    for (int c = 0; c < 6; ++c) {
        program_[static_cast<std::size_t>(c)] = c + 1;
    }
    start_from_tick_zero();
}

void MidiOplPlayer::start_from_tick_zero() {
    current_tick_ = 0;
    event_cursor_ = 0;
    tempo_cursor_ = 0;
    current_usec_per_qn_ = tempo_.empty() ? 500000 : tempo_.front().usec_per_qn;
    if (!tempo_.empty() && tempo_.front().tick == 0) ++tempo_cursor_;
    while (event_cursor_ < events_.size() && events_[event_cursor_].tick <= current_tick_) {
        dispatch(events_[event_cursor_]);
        ++event_cursor_;
    }
    sample_counter_ = samples_per_tick();
}

void MidiOplPlayer::reset() {
    opl_.reset();
    voices_.fill(Voice{});
    program_.fill(0);
    // FUN_1000_8b45 pre-assigns program = channel+1 to channels 0..5 before the song
    // plays, so a channel that never sends its own Program Change (e.g. channel 0, the
    // main melody) still gets a real patch instead of program 0.
    for (int c = 0; c < 6; ++c) {
        program_[static_cast<std::size_t>(c)] = c + 1;
    }
    age_counter_ = 0;
    finished_ = false;
    start_from_tick_zero();
}

double MidiOplPlayer::samples_per_tick() const {
    return static_cast<double>(sample_rate_) * static_cast<double>(current_usec_per_qn_) /
           (1e6 * static_cast<double>(division_));
}

void MidiOplPlayer::render(float* out, std::size_t frames) {
    for (std::size_t i = 0; i < frames; ++i) {
        if (!finished_) {
            sample_counter_ -= 1.0;
            while (sample_counter_ <= 0.0) {
                ++current_tick_;
                while (tempo_cursor_ < tempo_.size() && tempo_[tempo_cursor_].tick <= current_tick_) {
                    current_usec_per_qn_ = tempo_[tempo_cursor_].usec_per_qn;
                    ++tempo_cursor_;
                }
                while (event_cursor_ < events_.size() && events_[event_cursor_].tick <= current_tick_) {
                    dispatch(events_[event_cursor_]);
                    ++event_cursor_;
                }
                if (event_cursor_ >= events_.size()) {
                    // The last event has passed: loop (reset cursor + tick clock +
                    // all-notes-off) or stop.
                    all_notes_off();
                    if (loop_) {
                        start_from_tick_zero();
                    } else {
                        finished_ = true;
                    }
                    break;
                }
                sample_counter_ += samples_per_tick();
            }
        }
        out[i] = opl_.sample();
    }
}

void MidiOplPlayer::dispatch(const MidiEvent& event) {
    const std::uint8_t hi = event.status & 0xF0;
    const int channel = event.status & 0x0F;
    switch (hi) {
        case 0x80:
            note_off(channel, event.data1);
            break;
        case 0x90:
            if (event.data2 == 0) {
                note_off(channel, event.data1);
            } else {
                note_on(channel, event.data1);
            }
            break;
        case 0xC0:
            program_[static_cast<std::size_t>(channel)] = event.data1;
            break;
        default:
            break;  // control change / pitch bend / aftertouch: not modeled
    }
}

void MidiOplPlayer::note_on(int channel, int note) {
    int slot = -1;
    for (int i = 0; i < 9; ++i) {
        if (!voices_[static_cast<std::size_t>(i)].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        // Steal the oldest active voice (smallest age = activated longest ago).
        slot = 0;
        std::uint64_t oldest = voices_[0].age;
        for (int i = 1; i < 9; ++i) {
            if (voices_[static_cast<std::size_t>(i)].age < oldest) {
                oldest = voices_[static_cast<std::size_t>(i)].age;
                slot = i;
            }
        }
    }

    // Resolve the patch through the bank's program table (name index), NOT by raw
    // storage slot -- the .BNK stores patches in a scrambled order.
    load_instrument(slot, bank_.patch_for_program(program_[static_cast<std::size_t>(channel)]));

    int fnum = 0;
    int block = 0;
    note_to_fnum_block(note, fnum, block);
    // Key off first so a stolen/reused channel's envelope retriggers cleanly.
    opl_.write(static_cast<std::uint8_t>(0xB0 + slot), 0);
    opl_.write(static_cast<std::uint8_t>(0xA0 + slot), static_cast<std::uint8_t>(fnum & 0xFF));
    opl_.write(static_cast<std::uint8_t>(0xB0 + slot),
               static_cast<std::uint8_t>((1 << 5) | (block << 2) | ((fnum >> 8) & 0x03)));

    voices_[static_cast<std::size_t>(slot)] = Voice{true, channel, note, ++age_counter_};
}

void MidiOplPlayer::note_off(int channel, int note) {
    for (int i = 0; i < 9; ++i) {
        Voice& voice = voices_[static_cast<std::size_t>(i)];
        if (voice.active && voice.midi_channel == channel && voice.note == note) {
            opl_.write(static_cast<std::uint8_t>(0xB0 + i), 0);
            voice.active = false;
            break;
        }
    }
}

void MidiOplPlayer::all_notes_off() {
    for (int i = 0; i < 9; ++i) {
        opl_.write(static_cast<std::uint8_t>(0xB0 + i), 0);
        voices_[static_cast<std::size_t>(i)] = Voice{};
    }
}

void MidiOplPlayer::load_instrument(int opl_channel, const AdLibInstrument& instrument) {
    auto write_operator = [this](int op, const AdLibOperator& o, std::uint8_t wave) {
        opl_.write(static_cast<std::uint8_t>(0x20 + op),
                   static_cast<std::uint8_t>((o.am << 7) | (o.vib << 6) | (o.eg << 5) | (o.ksr << 4) |
                                              (o.mult & 0x0F)));
        // KSL: the original driver (FUN_1000_8bc8) packs it as (byte >> 2) & 0xC0, which
        // is 0 for the small BNK key-scale values -- i.e. it effectively disables
        // key-scaling-of-level. Packing (ksl & 3) << 6 instead over-attenuates high notes.
        opl_.write(static_cast<std::uint8_t>(0x40 + op),
                   static_cast<std::uint8_t>(((o.ksl >> 2) & 0xC0) | (o.level & 0x3F)));
        opl_.write(static_cast<std::uint8_t>(0x60 + op),
                   static_cast<std::uint8_t>((o.attack << 4) | (o.decay & 0x0F)));
        opl_.write(static_cast<std::uint8_t>(0x80 + op),
                   static_cast<std::uint8_t>((o.sustain << 4) | (o.release & 0x0F)));
        opl_.write(static_cast<std::uint8_t>(0xE0 + op), static_cast<std::uint8_t>(wave & 0x03));
    };
    const int op_mod = kOpModSlot[static_cast<std::size_t>(opl_channel)];
    const int op_car = op_mod + 3;
    write_operator(op_mod, instrument.mod, instrument.wave_mod);
    write_operator(op_car, instrument.car, instrument.wave_car);
    // reg 0xC0: feedback (bits 1-3) + connection (bit 0). The original INVERTS the
    // connection bit ((patch & 1) ^ 1): the .BNK stores 1 for its FM patches, so writing
    // it straight puts every voice in additive mode (two bare summed sines) -- the thin
    // "calculator" timbre. Inverting restores real 2-op FM.
    opl_.write(static_cast<std::uint8_t>(0xC0 + opl_channel),
               static_cast<std::uint8_t>(((instrument.mod.feedback & 0x07) << 1) |
                                          ((instrument.mod.connection & 1) ^ 1)));
}

}  // namespace bumpy
