#pragma once
#include <cstdint>
#include <memory>

namespace bumpy {

// Thin wrapper over the ymfm YM3812 (OPL2) core. Register-level interface: write() sets a
// register, sample() advances the chip one output sample. Native rate = clock/72 = 49715 Hz.
class Opl2 {
public:
    Opl2();
    ~Opl2();
    Opl2(Opl2&&) noexcept;
    Opl2& operator=(Opl2&&) noexcept;

    void reset();
    void write(std::uint8_t reg, std::uint8_t value);
    float sample();
    [[nodiscard]] std::uint32_t sample_rate() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bumpy
