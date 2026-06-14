#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace snesquik::sdd1 {

// S-DD1 graphics decompressor (Street Fighter Alpha 2, Star Ocean).
//
// The chip provides two services to the S-CPU:
//   1. A memory-map controller (MMC) that maps four selectable 1 MB ROM
//      segments into banks $C0-$FF, controlled by registers $4804-$4807.
//   2. On-the-fly decompression of graphics during DMA: a DMA channel that
//      the program has "armed" through $4800 reads its source bytes from the
//      S-DD1 decompressor instead of directly from ROM.
//
// This is an original implementation built from the published S-DD1 algorithm
// description (the chip's run-length / context-modelled arithmetic-style
// coder), not a port of another emulator.
class Sdd1 {
public:
    void power();
    void attachRom(std::span<const uint8_t> romBytes);

    // $4800-$4807 register interface (mirrored across banks $00-$3F/$80-$BF).
    uint8_t readRegister(uint16_t offset) const;
    void writeRegister(uint16_t offset, uint8_t value);

    // Read ROM through the MMC ($C0-$FF banked window). `address` is the full
    // 24-bit CPU address.
    uint8_t readRom(uint32_t address) const;

    // A channel decompresses when it is enabled in $4800 and armed in $4801.
    // $4801 is written immediately before each compressed transfer and is
    // cleared once that transfer completes (so plain DMA on the same channel
    // is unaffected).
    bool channelArmed(uint8_t channel) const
    {
        const uint8_t bit = static_cast<uint8_t>(1u << channel);
        return (r4800 & bit) != 0 && (r4801 & bit) != 0;
    }
    void clearChannelArm(uint8_t channel) { r4801 &= static_cast<uint8_t>(~(1u << channel)); }

    // Begin a decompression stream at the given 24-bit CPU source address
    // (resolved through the MMC), then pull decompressed bytes one at a time.
    void decompressBegin(uint32_t address);
    uint8_t decompressReadByte();

    void saveState(std::vector<uint8_t>& out) const;
    bool loadState(const uint8_t* data, size_t size);

private:
    // Resolve a 24-bit CPU address in the $C0-$FF window to a linear ROM offset.
    size_t mmcOffset(uint32_t address) const;
    uint8_t romByte(size_t linearOffset) const;

    std::span<const uint8_t> rom;

    // $4804-$4807: each selects which 1 MB block maps to one quarter of the
    // $C0-$FF window. Reset defaults map the first 4 MB linearly (0,1,2,3).
    uint8_t mmc[4] = {0, 1, 2, 3};
    uint8_t r4800 = 0; // per-channel DMA decompression enable
    uint8_t r4801 = 0;
    uint8_t r4802 = 0;
    uint8_t r4803 = 0;

    // --- Decompressor state (see sdd1.cpp) ---
    struct Decoder {
        std::span<const uint8_t> rom;
        size_t inOffset = 0;   // next compressed byte to pull into the reservoir
        uint16_t inStream = 0; // 16-bit input bit reservoir (codewords drawn MSB-first)
        int validBits = 0;

        // Per-Golomb-order run state (holds the current codeword's residual run).
        int bitCtr[8] = {};

        // Context state index + current MPS value, up to 32 contexts.
        uint8_t contextState[32] = {};
        uint8_t contextMps[32] = {};

        // Per-bitplane history: a running 16-bit shift register of the last
        // bits decoded for that bitplane (newest in bit 0). bit7 = pixel above,
        // bit8 = above-left, bit6 = above-right (rows are 8 pixels wide).
        uint16_t prevBits[8] = {};

        uint8_t bitplaneType = 0;   // header bits 7-6: 0=2bpp,1=8bpp,2=4bpp,3=chunky
        uint16_t highContext = 0;   // context-mode neighbour masks (header bits 5-4)
        uint16_t lowContext = 0;

        // Output assembly state machine.
        uint8_t plane = 0;
        uint8_t iCounter = 0;
        uint8_t pendingByte = 0;
        bool hasPending = false;

        void reset(std::span<const uint8_t> romBytes, size_t base);
        uint8_t pullInByte();
        int getCodeword(int order);
        uint8_t golombGetBit(int order);
        uint8_t probGetBit(uint8_t context);
        uint8_t getBit(uint8_t bitplane);
        uint8_t nextByte();
    } decoder;
};

} // namespace snesquik::sdd1
