#include "audio/speaker_sfx.h"

#include <algorithm>
#include <cmath>

namespace bumpy {

float sfx_tone_hz(std::uint16_t divisor) {
    return 1193182.0f / static_cast<float>(divisor == 0 ? 1 : divisor);
}

void SpeakerVoice::start(const SfxPreset& preset) {
    kind_ = preset.kind;
    steps_left_ = preset.steps;
    period_ = std::max<std::int32_t>(1, preset.rate_seed);
    rate_step_ = static_cast<std::int16_t>(preset.rate_step);
    rate_count_ = std::max<std::int32_t>(1, preset.rate_count);
    glide_left_ = rate_count_;
    acc_ = 0;
    samples_until_base_tick_ = static_cast<double>(kSampleRate) / kBaseTickHz;
    phase_ = 0.0;

    if (kind_ == SweepKind::tone) {
        divisor_ = std::max<std::int32_t>(1, preset.init_divisor);
        divisor_step_ = static_cast<std::int16_t>(preset.divisor_step);
    } else {
        // The 16-bit noise registers persist across sounds in the original (BSS, only
        // reseeded, never re-zeroed), so we deliberately do NOT reset l1_/l3_ here; the
        // arming stub only reloads the reseed cadence counter.
        noise_reseed_ctr_ = 0x0f;
        noise_level_ = 0.0f;
    }

    active_ = steps_left_ > 0;
}

float SpeakerVoice::current_hz() const {
    const auto clamped = std::clamp<std::int32_t>(divisor_, 1, 0xffff);
    return sfx_tone_hz(static_cast<std::uint16_t>(clamped));
}

void SpeakerVoice::fire_tone() {
    // FUN_1000_9631: dec the outer step counter first; the terminating fire ends the
    // sound WITHOUT a final tone step (so `steps` yields steps-1 pitch updates).
    if (--steps_left_ <= 0) {
        active_ = false;
        return;
    }
    divisor_ = std::max<std::int32_t>(1, divisor_ + divisor_step_);
    // Glide sub-divider: every rate_count fires nudge the DDS period and re-install the
    // slot, which zeroes the accumulator.
    if (--glide_left_ <= 0) {
        glide_left_ = rate_count_;
        period_ = std::max<std::int32_t>(1, period_ + rate_step_);
        acc_ = 0;
    }
}

void SpeakerVoice::fire_noise() {
    // FUN_1000_96c4: reseed the shift register every 16 fires (counter starts 0x0f,
    // increments, reseed when bit4 sets), then reset the counter.
    if ((++noise_reseed_ctr_ & 0x10) != 0) {
        noise_reseed_ctr_ = 0;
        l1_ = static_cast<std::uint16_t>((l1_ + kNoiseAddA) ^ l3_);
        l3_ = static_cast<std::uint16_t>(l1_ + kNoiseAddB);
    }
    // Every fire: rotate L1 left 1 and clock bit 8 out to the speaker-data line.
    l1_ = static_cast<std::uint16_t>((l1_ << 1) | (l1_ >> 15));
    noise_level_ = ((l1_ >> 8) & 1) ? kAmplitude : -kAmplitude;
    // Outer step + glide run off the same rate_count sub-divider for noise.
    if (--glide_left_ <= 0) {
        glide_left_ = rate_count_;
        if (--steps_left_ <= 0) {
            active_ = false;
            return;
        }
        period_ = std::max<std::int32_t>(1, period_ + rate_step_);
        acc_ = 0;
    }
}

void SpeakerVoice::base_tick() {
    // The fixed-rate ISR: advance the DDS accumulator; on overflow past the 500
    // threshold, fire the active step handler once.
    acc_ += period_;
    if (acc_ >= kDdsThreshold) {
        acc_ -= kDdsThreshold;
        if (kind_ == SweepKind::tone) {
            fire_tone();
        } else {
            fire_noise();
        }
    }
}

void SpeakerVoice::render_add(float* out, std::size_t frames) {
    constexpr int kMaxTicksPerSample = 8;  // base tick is ~99 samples apart; safety net

    const double samples_per_base_tick = static_cast<double>(kSampleRate) / kBaseTickHz;

    for (std::size_t i = 0; i < frames; ++i) {
        if (!active_) {
            break;
        }
        samples_until_base_tick_ -= 1.0;
        int guard = 0;
        while (samples_until_base_tick_ <= 0.0 && active_ && guard < kMaxTicksPerSample) {
            base_tick();
            samples_until_base_tick_ += samples_per_base_tick;
            ++guard;
        }
        if (!active_) {
            break;
        }

        if (kind_ == SweepKind::tone) {
            const float hz = current_hz();
            phase_ += static_cast<double>(hz) / static_cast<double>(kSampleRate);
            phase_ -= std::floor(phase_);
            out[i] += (phase_ < 0.5) ? kAmplitude : -kAmplitude;
        } else {
            out[i] += noise_level_;
        }
    }
}

}  // namespace bumpy
