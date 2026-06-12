#include "APU/apu.h"
#include "APU_SPC700/snes_spc/snes_spc/SNES_SPC.h"

#include <cstring>

namespace snesquik::apu {

namespace {

// Standard SNES SPC700 IPL ROM (64 bytes at $FFC0-$FFFF)
// This bootstrap handles the SPC700 upload protocol used by all SNES games.
// Source: Anomie's SPC700 Doc, verified against bsnes/snes9x
constexpr uint8_t iplRom[64] = {
    0xCD, 0xEF, 0xBD, 0xE8, 0x00, 0xC6, 0x1D, 0xD0,
    0xFC, 0x8F, 0xAA, 0xF4, 0x8F, 0xBB, 0xF5, 0x78,
    0xCC, 0xF4, 0xD0, 0xFB, 0x2F, 0x19, 0xEB, 0xF4,
    0xD0, 0xFC, 0x7E, 0xF4, 0xD0, 0x0B, 0xE4, 0xF5,
    0xCB, 0xF4, 0xD7, 0x00, 0xFC, 0xD0, 0xF3, 0xAB,
    0x01, 0x10, 0xEF, 0x7E, 0xF4, 0x10, 0xEB, 0xBA,
    0xF6, 0xDA, 0x00, 0xBA, 0xF4, 0xC4, 0xF4, 0xDD,
    0x5D, 0xD0, 0xDB, 0x1F, 0x00, 0x00, 0xC0, 0xFF,
};

constexpr uint64_t kMasterClockRate = 21477272;
constexpr uint32_t kSpcClockRate = 1024000;

} // namespace

Apu::Apu() = default;

Apu::~Apu()
{
    delete spc;
}

void Apu::init()
{
    spc = new SNES_SPC;
    spc->init();
    spc->init_rom(iplRom);
    spc->set_output(nullptr, 0);
    spc->reset();
    spcTimeAccum = 0;
    spcTime = 0;
}

void Apu::reset()
{
    if (!spc) {
        return;
    }
    spc->set_output(nullptr, 0);
    spc->reset();
    spcTimeAccum = 0;
    spcTime = 0;
}

void Apu::softReset()
{
    if (!spc) {
        return;
    }
    spc->set_output(nullptr, 0);
    spc->soft_reset();
    spcTimeAccum = 0;
    spcTime = 0;
}

uint8_t Apu::readPort(int port)
{
    if (!spc || port < 0 || port > 3) {
        return 0;
    }
    return static_cast<uint8_t>(spc->read_port(spcTime, port));
}

void Apu::writePort(int port, uint8_t data)
{
    if (!spc || port < 0 || port > 3) {
        return;
    }
    spc->write_port(spcTime, port, data);
}

void Apu::step(uint32_t cpuCycles)
{
    // Convert CPU master clocks to SPC clocks.
    // cpuCycles from step() is in units where 1 cycle ≈ 6 master clocks
    // (the average for the 65816's mixed fast/slow memory accesses).
    // Master clock = cpuCycles * 6 (approximately).
    // SPC clock rate / master clock rate = 1024000 / 21477272.
    // So SPC clocks = cpuCycles * 6 * 1024000 / 21477272.
    //               = cpuCycles * 6144000 / 21477272.
    spcTimeAccum += static_cast<uint64_t>(cpuCycles) * 6144000ULL;
    const uint32_t newSpcCycles = static_cast<uint32_t>(spcTimeAccum / kMasterClockRate);
    spcTimeAccum %= kMasterClockRate;
    spcTime += newSpcCycles;

    // Run SPC700 forward to current time. read_port() internally calls
    // run_until_() which executes the SPC700 up to the given time.
    // Without this, the SPC700 would only run during port accesses,
    // leaving it frozen between them.
    if (spc && newSpcCycles > 0) {
        spc->read_port(spcTime, 0);
    }
}

void Apu::endFrame()
{
    if (!spc) {
        return;
    }
    spc->end_frame(spcTime);
    spcTime = 0;
    spcTimeAccum = 0;
}

} // namespace snesquik::apu
