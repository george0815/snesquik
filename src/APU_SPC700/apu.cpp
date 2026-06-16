#include "APU_SPC700/apu.h"
#include "APU_SPC700/snes_spc/snes_spc/SNES_SPC.h"
#include "APU_SPC700/snes_spc/snes_spc/SPC_Filter.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// TEMP diagnostic: RAM-write watchpoint state defined in SNES_SPC.cpp.
extern unsigned g_curPc;
extern int g_wpTarget;
extern unsigned g_wpPc[64], g_wpVal[64], g_wpIdx;
extern unsigned g_echoEsa, g_echoEdl, g_echoFlg, g_echoBase, g_echoSeen;
extern unsigned g_x0A0A[64], g_idx0A0A;
extern int g_stackHit; extern unsigned g_stackHitX, g_stackHitTgt;
extern unsigned g_stkPc[64], g_stkVal[64], g_stkIdx;
extern int g_retSeen; extern unsigned g_retSp, g_retLo, g_retHi;
extern int g_feSeen; extern unsigned g_feWriterPc, g_fePrev;
extern unsigned g_dspRegPc[128], g_dspRegAddr[128], g_dspRegVal[128], g_dspRegIdx;

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
    lastCpuCycle = 0;
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
    lastCpuCycle = 0;
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
    lastCpuCycle = 0;
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

namespace {
void initWatchpoint()
{
    static bool done = false;
    if (done) return;
    done = true;
    if (const char* s = std::getenv("SPCWP")) {
        g_wpTarget = static_cast<int>(std::strtol(s, nullptr, 16));
    }
}
void maybeDumpSpcTrace(SNES_SPC* spc)
{
    static const bool on = std::getenv("SPCTRACE") != nullptr;
    static bool dumped = false;
    if (!on || dumped || !spc || !spc->debug_error()) {
        return;
    }
    dumped = true;
    std::fprintf(stderr, "=== RET@$0B5F: seen=%d SP=$%02X pops return=$%02X%02X ===\n",
                 g_retSeen, g_retSp & 0xff, g_retHi & 0xff, g_retLo & 0xff);
    std::fprintf(stderr, "=== slot $01FE/$01FF -> $FFC0: seen=%d writerPC=$%04X (prev slot $%04X) ===\n",
                 g_feSeen, g_feWriterPc & 0xffff, g_fePrev & 0xffff);
    std::fprintf(stderr, "=== PUSH16 writes to $01FE (PC=>value), last %u ===\n",
                 g_stkIdx < 64 ? g_stkIdx : 64);
    {
        const unsigned n = g_stkIdx < 64 ? g_stkIdx : 64;
        const unsigned start = g_stkIdx < 64 ? 0 : (g_stkIdx & 63);
        for (unsigned i = 0; i < n; ++i) {
            const unsigned k = (start + i) & 63;
            if (g_stkPc[k] & 0x80000000u)
                std::fprintf(stderr, "  [PUSH8] PC=%04X addr=$%02X val=$%02X\n",
                             g_stkPc[k] & 0xffff, (g_stkVal[k] >> 8) & 0xff, g_stkVal[k] & 0xff);
            else
                std::fprintf(stderr, "  [PUSH16] PC=%04X -> $%04X\n", g_stkPc[k] & 0xffff, g_stkVal[k] & 0xffff);
        }
    }
    std::fprintf(stderr, "=== $0A0A store: stackHit=%d X=%02X tgt=$%04X; recent X at $0A0A: ===\n",
                 g_stackHit, g_stackHitX & 0xff, g_stackHitTgt & 0xffff);
    {
        const unsigned n = g_idx0A0A < 64 ? g_idx0A0A : 64;
        const unsigned start = g_idx0A0A < 64 ? 0 : (g_idx0A0A & 63);
        for (unsigned i = 0; i < n; ++i) std::fprintf(stderr, "%02X ", g_x0A0A[(start + i) & 63] & 0xff);
        std::fprintf(stderr, "\n");
    }
    std::fprintf(stderr, "=== ECHO low-RAM events=%u: ESA=%02X (base $%04X) EDL=%X FLG=%02X ===\n",
                 g_echoSeen, g_echoEsa & 0xff, g_echoBase & 0xffff, g_echoEdl & 0xf, g_echoFlg & 0xff);
    {
        std::fprintf(stderr, "=== echo-reg writes (SPC PC: reg=val), %u total ===\n", g_dspRegIdx);
        const unsigned n = g_dspRegIdx < 128 ? g_dspRegIdx : 128;
        const unsigned start = g_dspRegIdx < 128 ? 0 : (g_dspRegIdx & 127);
        for (unsigned i = 0; i < n; ++i) {
            const unsigned k = (start + i) & 127;
            const unsigned a = g_dspRegAddr[k];
            const char* nm = a == 0x6C ? "FLG" : a == 0x6D ? "ESA" : a == 0x7D ? "EDL"
                           : a == 0x0D ? "EFB" : a == 0x4D ? "EON" : "?";
            std::fprintf(stderr, "  PC=%04X %s($%02X)=%02X\n",
                         g_dspRegPc[k] & 0xffff, nm, a, g_dspRegVal[k] & 0xff);
        }
    }
    std::fprintf(stderr, "=== SPC crash pc=%04X; last writes to $%04X (PC=value), %u total ===\n",
                 spc->debug_pc() & 0xffff, g_wpTarget & 0xffff, g_wpIdx);
    const unsigned n = g_wpIdx < 64 ? g_wpIdx : 64;
    const unsigned start = g_wpIdx < 64 ? 0 : (g_wpIdx & 63);
    for (unsigned i = 0; i < n; ++i) {
        const unsigned k = (start + i) & 63;
        std::fprintf(stderr, "  PC=%04X val=%02X\n", g_wpPc[k] & 0xffff, g_wpVal[k] & 0xff);
    }
}
} // namespace

void Apu::advance(uint32_t cpuCycles)
{
    initWatchpoint();
    // Convert CPU master clocks to SPC clocks.
    // cpuCycles is in units where 1 cycle ≈ 6 master clocks (the average for
    // the 65816's mixed fast/slow memory accesses).
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
        maybeDumpSpcTrace(spc);
    }
}

void Apu::step(uint32_t cpuCycles)
{
    advance(cpuCycles);
}

void Apu::syncToCpuCycle(uint64_t cpuCycle, uint32_t extraCycles)
{
    uint32_t delta = 0;
    if (cpuCycle > lastCpuCycle) {
        const uint64_t d = cpuCycle - lastCpuCycle;
        // A single instruction never spans more than a few dozen CPU cycles,
        // so a huge delta means the baseline is stale (e.g. right after a
        // state load before resyncCpuBaseline ran). Drop it rather than
        // fast-forwarding the SPC by a bogus amount. DMA halt time arrives via
        // extraCycles and is intentionally not clamped.
        delta = d > 0xffffu ? 0 : static_cast<uint32_t>(d);
    }
    lastCpuCycle = cpuCycle;
    advance(delta + extraCycles);
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
