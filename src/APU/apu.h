#pragma once

#include <cstdint>
#include <cstddef>

struct SNES_SPC;

namespace snesquik::apu {

class Apu {
public:
    Apu();
    ~Apu();

    Apu(const Apu&) = delete;
    Apu& operator=(const Apu&) = delete;

    void init();
    void reset();
    void softReset();

    uint8_t readPort(int port);
    void writePort(int port, uint8_t data);

    void step(uint32_t cpuCycles);
    void endFrame();

    static constexpr int clockRate = 1024000;
    static constexpr uint64_t masterClockRate = 21477272;

private:
    SNES_SPC* spc = nullptr;
    uint64_t spcTimeAccum = 0;
    uint32_t spcTime = 0;
};

} // namespace snesquik::apu
