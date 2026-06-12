#include "APU/apu.h"
#include "APU_SPC700/snes_spc/snes_spc/SNES_SPC.h"
#include "APU_SPC700/snes_spc/snes_spc/SPC_Filter.h"

#include <algorithm>
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

} // namespace

Apu::Apu() = default;

Apu::~Apu()
{
    delete spc;
    delete filter;
}

void Apu::armOutputBuffer()
{
    if (spc) {
        spc->set_output(sampleBuffer, sampleBufferSize);
    }
}

void Apu::init()
{
    spc = new SNES_SPC;
    spc->init();
    spc->init_rom(iplRom);
    spc->reset();
    filter = new SPC_Filter;
    filter->clear();
    armOutputBuffer();
    spcTimeAccum = 0;
    spcTime = 0;
    outputSampleCount = 0;
}

void Apu::reset()
{
    if (!spc) {
        return;
    }
    spc->reset();
    filter->clear();
    armOutputBuffer();
    spcTimeAccum = 0;
    spcTime = 0;
    outputSampleCount = 0;
}

void Apu::softReset()
{
    if (!spc) {
        return;
    }
    spc->soft_reset();
    filter->clear();
    armOutputBuffer();
    spcTimeAccum = 0;
    spcTime = 0;
    outputSampleCount = 0;
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

    outputSampleCount = std::min(spc->sample_count(), sampleBufferSize);
    std::memcpy(outputSamples, sampleBuffer, static_cast<size_t>(outputSampleCount) * sizeof(int16_t));

    // High-pass/gain stage matching the SNES DAC output; removes the DC
    // offsets that otherwise pop audibly when voices key on.
    if (filter && outputSampleCount > 0) {
        filter->run(outputSamples, outputSampleCount);
    }

    // Re-arm the buffer for the next frame; this folds leftover clocks and
    // carries over any extra samples the DSP generated past sample_count().
    armOutputBuffer();
}

std::span<const int16_t> Apu::frameSamples() const
{
    return {outputSamples, static_cast<size_t>(outputSampleCount)};
}

namespace {

void copyToBuffer(unsigned char** io, void* state, size_t size)
{
    std::memcpy(*io, state, size);
    *io += size;
}

void copyFromBuffer(unsigned char** io, void* state, size_t size)
{
    std::memcpy(state, *io, size);
    *io += size;
}

} // namespace

void Apu::saveState(std::vector<uint8_t>& out)
{
    std::vector<uint8_t> buffer(SNES_SPC::state_size, 0);
    if (spc) {
        unsigned char* pos = buffer.data();
        spc->copy_state(&pos, copyToBuffer);
    }
    out.insert(out.end(), buffer.begin(), buffer.end());
    const auto* timeBytes = reinterpret_cast<const uint8_t*>(&spcTime);
    out.insert(out.end(), timeBytes, timeBytes + sizeof(spcTime));
    const auto* accumBytes = reinterpret_cast<const uint8_t*>(&spcTimeAccum);
    out.insert(out.end(), accumBytes, accumBytes + sizeof(spcTimeAccum));
}

bool Apu::loadState(const uint8_t* data, size_t size)
{
    const size_t expected = SNES_SPC::state_size + sizeof(spcTime) + sizeof(spcTimeAccum);
    if (!spc || size != expected) {
        return false;
    }
    std::vector<uint8_t> buffer(data, data + SNES_SPC::state_size);
    unsigned char* pos = buffer.data();
    spc->copy_state(&pos, copyFromBuffer);
    std::memcpy(&spcTime, data + SNES_SPC::state_size, sizeof(spcTime));
    std::memcpy(&spcTimeAccum, data + SNES_SPC::state_size + sizeof(spcTime), sizeof(spcTimeAccum));
    // copy_state invalidates the output buffer configuration.
    if (filter) {
        filter->clear();
    }
    outputSampleCount = 0;
    armOutputBuffer();
    return true;
}

int Apu::debugPc() const
{
    return spc ? spc->debug_pc() : -1;
}

const char* Apu::debugError() const
{
    return spc ? spc->debug_error() : nullptr;
}

int Apu::debugRam(int addr) const
{
    return spc ? spc->debug_ram(addr) : -1;
}

int Apu::debugOutPort(int port) const
{
    return spc ? spc->debug_out_port(port) : -1;
}

int Apu::debugInPort(int port) const
{
    return spc ? spc->debug_in_port(port) : -1;
}

} // namespace snesquik::apu
