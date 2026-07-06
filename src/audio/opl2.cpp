#include "audio/opl2.h"
#include "ymfm_opl.h"

namespace bumpy {

struct Opl2::Impl : public ymfm::ymfm_interface {
    Impl() : chip(*this) { chip.reset(); }
    ymfm::ym3812 chip;
    static constexpr std::uint32_t kClock = 3579545;
};

Opl2::Opl2() : impl_(std::make_unique<Impl>()) {}
Opl2::~Opl2() = default;
Opl2::Opl2(Opl2&&) noexcept = default;
Opl2& Opl2::operator=(Opl2&&) noexcept = default;

void Opl2::reset() { impl_->chip.reset(); }

void Opl2::write(std::uint8_t reg, std::uint8_t value) {
    impl_->chip.write_address(reg);
    impl_->chip.write_data(value);
}

float Opl2::sample() {
    ymfm::ym3812::output_data out;
    impl_->chip.generate(&out, 1);
    return static_cast<float>(out.data[0]) / 32768.0f;
}

std::uint32_t Opl2::sample_rate() const { return impl_->chip.sample_rate(Impl::kClock); }

}  // namespace bumpy
