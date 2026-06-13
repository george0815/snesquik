#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace snesquik::dsp {

// NEC uPD7725 / uPD77C25 DSP, as used by the SNES DSP-1 / DSP-1A / DSP-1B
// coprocessor (and the same core is reusable for DSP-2/3/4). Low-level
// emulation: the chip's behavior lives in a proprietary on-die program +
// data ROM that the user supplies as an 8192-byte dump (6144 bytes program,
// 2048 bytes data). The S-CPU talks to it through two registers, DR (data)
// and SR (status), via the RQM handshake.
//
// Sizes are the uPD7725 configuration: program 2048x24, data ROM 1024x16,
// data RAM 256x16.
class NecDsp {
public:
    static constexpr size_t programWords = 2048;
    static constexpr size_t dataRomWords = 1024;
    static constexpr size_t dataRamWords = 2048;
    static constexpr size_t dumpBytes = programWords * 3 + dataRomWords * 2; // 8192

    void power();
    // Load the combined 8192-byte program+data ROM dump. Returns false if the
    // file is missing or the wrong size (the caller leaves the DSP inert).
    bool loadRom(const std::string& path);
    bool loaded() const { return romLoaded; }

    // Run roughly `cycles` DSP instructions (used for boot/self-test progress).
    void step(uint32_t cycles);

    // S-CPU facing interface.
    uint8_t readDR();
    uint8_t readSR() const { return static_cast<uint8_t>(sr >> 8); }
    void writeDR(uint8_t value);
    void writeSR(uint8_t /*value*/) {} // SR is read-only to the S-CPU

    void saveState(std::vector<uint8_t>& out) const;
    bool loadState(const uint8_t* data, size_t size);

    // SR bit layout (16-bit; the S-CPU sees the high byte).
    static constexpr uint16_t srRqm = 0x8000; // request for master transfer
    static constexpr uint16_t srUsf1 = 0x4000;
    static constexpr uint16_t srUsf0 = 0x2000;
    static constexpr uint16_t srDrs = 0x1000;  // data-register transfer step
    static constexpr uint16_t srDma = 0x0800;
    static constexpr uint16_t srDrc = 0x0400;  // 0 = 16-bit transfer, 1 = 8-bit
    static constexpr uint16_t srPc = 0x0200;
    static constexpr uint16_t srSic = 0x0100;
    static constexpr uint16_t srSoc = 0x0080;

    // One ALU flag set (per accumulator).
    struct Flag {
        bool s0 = false;  // sign
        bool s1 = false;  // sign accounting for overflow stack
        bool c = false;   // carry
        bool z = false;   // zero
        bool ov0 = false; // overflow
        bool ov1 = false; // overflow stack
    };

    struct Registers {
        uint16_t pc = 0;
        uint16_t rp = 0;   // data ROM pointer
        uint16_t dp = 0;   // data RAM pointer
        uint16_t a = 0;    // accumulator A
        uint16_t b = 0;    // accumulator B
        Flag flagA;
        Flag flagB;
        uint16_t tr = 0;
        uint16_t trb = 0;
        uint16_t k = 0;
        uint16_t l = 0;
        uint16_t m = 0;
        uint16_t n = 0;
        uint16_t sgn = 0;
        uint16_t si = 0;   // serial in (unused on SNES)
        uint16_t so = 0;   // serial out (unused on SNES)
        std::array<uint16_t, 16> stack{}; // call stack
        uint8_t sp = 0;
    };

    // Exposed for unit tests / introspection (matches the GSU/SA-1 style).
    Registers regs;
    uint16_t sr = 0;
    uint16_t dr = 0;
    std::array<uint16_t, dataRamWords> dataRam{};
    std::array<uint32_t, programWords> programRom{}; // 24-bit words
    std::array<uint16_t, dataRomWords> dataRom{};
    bool romLoaded = false;

    // Single-instruction execution (public for tests).
    void exec();

    // True once the DSP has executed a taken JRQM (it is blocked waiting for
    // the S-CPU to complete a DR transfer). Used to pace synchronous catch-up.
    bool dspWaiting = false;

private:
    void execOp(uint32_t opcode);
    void execRt(uint32_t opcode);
    void execJp(uint32_t opcode);
    void execLd(uint32_t opcode);
    uint16_t readSrc(uint8_t src);
    void writeDst(uint8_t dst, uint16_t idb);
    // Run until RQM is asserted again (bounded), so results are ready when the
    // S-CPU next polls SR after a DR transfer completes.
    void runToRequest();
};

} // namespace snesquik::dsp
