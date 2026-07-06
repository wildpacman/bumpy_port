#include "audio/speaker_sfx.h"

#include <algorithm>
#include <cmath>

namespace bumpy {

float sfx_tone_hz(std::uint16_t divisor) {
    return 1193182.0f / static_cast<float>(divisor == 0 ? 1 : divisor);
}

void SpeakerVoice::start(const SfxPreset& preset) {
    kind_ = preset.kind;
    divisor_step_ = static_cast<std::int16_t>(preset.divisor_step);
    rate_step_ = static_cast<std::int16_t>(preset.rate_step);
    rate_count_ = std::max<std::int32_t>(1, preset.rate_count);
    rate_left_ = rate_count_;
    steps_left_ = preset.steps;
    isr_period_ = std::max(1.0, static_cast<double>(preset.rate_seed));
    lfsr_ = kNoiseLfsrSeed;
    phase_ = 0.0;

    if (kind_ == SweepKind::tone) {
        divisor_ = std::max<std::int32_t>(1, preset.init_divisor);
    } else {
        // Noise voices reseed their frequency from the LFSR immediately, rather than
        // starting from `init_divisor` (which is always 0 for noise presets) -- the
        // original ISR's own first tick is what actually picks a divisor.
        lfsr_ = static_cast<std::uint16_t>(lfsr_ * kNoiseLfsrMul + kNoiseLfsrAdd);
        divisor_ = std::max<std::int32_t>(1, lfsr_);
    }

    samples_until_tick_ = samples_per_tick();
    active_ = steps_left_ > 0;
}

double SpeakerVoice::samples_per_tick() const {
    return std::max(1.0, static_cast<double>(kSampleRate) * isr_period_ / kSfxIsrBaseHz);
}

void SpeakerVoice::tick() {
    if (kind_ == SweepKind::tone) {
        divisor_ = std::max<std::int32_t>(1, divisor_ + divisor_step_);
    } else {
        lfsr_ = static_cast<std::uint16_t>(lfsr_ * kNoiseLfsrMul + kNoiseLfsrAdd);
        divisor_ = std::max<std::int32_t>(1, lfsr_);
    }

    if (steps_left_ > 0) {
        --steps_left_;
    }
    if (steps_left_ == 0) {
        active_ = false;
    }

    if (--rate_left_ <= 0) {
        isr_period_ = std::max(1.0, isr_period_ + rate_step_);
        rate_left_ = rate_count_;
    }
}

float SpeakerVoice::current_hz() const {
    const auto clamped = std::clamp<std::int32_t>(divisor_, 1, 0xffff);
    return sfx_tone_hz(static_cast<std::uint16_t>(clamped));
}

void SpeakerVoice::render_add(float* out, std::size_t frames) {
    constexpr float kAmplitude = 0.25f;
    constexpr int kMaxTicksPerSample = 256;  // safety net; ticks are clamped to >=1 sample

    for (std::size_t i = 0; i < frames; ++i) {
        if (!active_) {
            break;
        }
        samples_until_tick_ -= 1.0;
        int guard = 0;
        while (samples_until_tick_ <= 0.0 && active_ && guard < kMaxTicksPerSample) {
            tick();
            samples_until_tick_ += samples_per_tick();
            ++guard;
        }
        if (!active_) {
            break;
        }

        const float hz = current_hz();
        phase_ += static_cast<double>(hz) / static_cast<double>(kSampleRate);
        phase_ -= std::floor(phase_);
        out[i] += (phase_ < 0.5) ? kAmplitude : -kAmplitude;
    }
}

}  // namespace bumpy
