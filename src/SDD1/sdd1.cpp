#include "SDD1/sdd1.h"

#include <cstring>

namespace snesquik::sdd1 {

namespace {

// Probability/run evolution state machine (33 states). For each state:
//   order   - Golomb code order (0-7) used by the bit generator
//   nextMps - next state when the run completes on a most-probable symbol
//   nextLps - next state when the run terminates on a least-probable symbol
//   invert  - toggle the context's MPS on an LPS (fast-adaptation states only)
// States 0 and >=25 are "fast adaptation" states reachable only at the start.
struct EvoEntry {
    uint8_t order;
    uint8_t nextMps;
    uint8_t nextLps;
    bool invert;
};

constexpr EvoEntry kEvo[33] = {
    /* 0*/ {0, 25, 25, true},
    /* 1*/ {0, 2, 1, true},
    /* 2*/ {0, 3, 1, false},
    /* 3*/ {0, 4, 2, false},
    /* 4*/ {0, 5, 3, false},
    /* 5*/ {1, 6, 4, false},
    /* 6*/ {1, 7, 5, false},
    /* 7*/ {1, 8, 6, false},
    /* 8*/ {1, 9, 7, false},
    /* 9*/ {2, 10, 8, false},
    /*10*/ {2, 11, 9, false},
    /*11*/ {2, 12, 10, false},
    /*12*/ {2, 13, 11, false},
    /*13*/ {3, 14, 12, false},
    /*14*/ {3, 15, 13, false},
    /*15*/ {3, 16, 14, false},
    /*16*/ {3, 17, 15, false},
    /*17*/ {4, 18, 16, false},
    /*18*/ {4, 19, 17, false},
    /*19*/ {5, 20, 18, false},
    /*20*/ {5, 21, 19, false},
    /*21*/ {6, 22, 20, false},
    /*22*/ {6, 23, 21, false},
    /*23*/ {7, 24, 22, false},
    /*24*/ {7, 24, 23, false},
    /*25*/ {0, 26, 1, false},
    /*26*/ {1, 27, 2, false},
    /*27*/ {2, 28, 4, false},
    /*28*/ {3, 29, 8, false},
    /*29*/ {4, 30, 12, false},
    /*30*/ {5, 31, 16, false},
    /*31*/ {6, 32, 18, false},
    /*32*/ {7, 24, 22, false},
};

// Maps the value bits that follow a "run terminated by 1" codeword to the run
// length (the number of zero bits before the terminating one, +1). This is the
// chip's intrinsic decode table for the variable-length Golomb codeword field.
constexpr uint8_t kRunTable[128] = {
    128,  64,  96,  32, 112,  48,  80,  16, 120,  56,  88,  24, 104,  40,  72,
      8, 124,  60,  92,  28, 108,  44,  76,  12, 116,  52,  84,  20, 100,  36,
     68,   4, 126,  62,  94,  30, 110,  46,  78,  14, 118,  54,  86,  22, 102,
     38,  70,   6, 122,  58,  90,  26, 106,  42,  74,  10, 114,  50,  82,  18,
     98,  34,  66,   2, 127,  63,  95,  31, 111,  47,  79,  15, 119,  55,  87,
     23, 103,  39,  71,   7, 123,  59,  91,  27, 107,  43,  75,  11, 115,  51,
     83,  19,  99,  35,  67,   3, 125,  61,  93,  29, 109,  45,  77,  13, 117,
     53,  85,  21, 101,  37,  69,   5, 121,  57,  89,  25, 105,  41,  73,   9,
    113,  49,  81,  17,  97,  33,  65,   1
};

} // namespace

// ---------------------------------------------------------------------------
// Chip top level: registers, MMC, save state.
// ---------------------------------------------------------------------------

void Sdd1::power()
{
    mmc[0] = 0;
    mmc[1] = 1;
    mmc[2] = 2;
    mmc[3] = 3;
    r4800 = 0;
    r4801 = 0;
    r4802 = 0;
    r4803 = 0;
}

void Sdd1::attachRom(std::span<const uint8_t> romBytes)
{
    rom = romBytes;
}

uint8_t Sdd1::readRegister(uint16_t offset) const
{
    switch (offset & 0x07) {
    case 0x00: return r4800;
    case 0x01: return r4801;
    case 0x02: return r4802;
    case 0x03: return r4803;
    case 0x04: return mmc[0];
    case 0x05: return mmc[1];
    case 0x06: return mmc[2];
    case 0x07: return mmc[3];
    }
    return 0;
}

void Sdd1::writeRegister(uint16_t offset, uint8_t value)
{
    switch (offset & 0x07) {
    case 0x00: r4800 = value; break;
    case 0x01: r4801 = value; break;
    case 0x02: r4802 = value; break;
    case 0x03: r4803 = value; break;
    case 0x04: mmc[0] = value & 0x07; break;
    case 0x05: mmc[1] = value & 0x07; break;
    case 0x06: mmc[2] = value & 0x07; break;
    case 0x07: mmc[3] = value & 0x07; break;
    }
}

size_t Sdd1::mmcOffset(uint32_t address) const
{
    // Banks $C0-$FF form a 4 MB window split into four 1 MB quarters; each
    // quarter maps to a 1 MB ROM block selected by mmc[].
    const uint8_t quarter = static_cast<uint8_t>((address >> 20) & 0x03);
    return (static_cast<size_t>(mmc[quarter]) << 20) | (address & 0x000fffff);
}

uint8_t Sdd1::romByte(size_t linearOffset) const
{
    if (rom.empty()) {
        return 0;
    }
    return rom[linearOffset % rom.size()];
}

uint8_t Sdd1::readRom(uint32_t address) const
{
    return romByte(mmcOffset(address));
}

void Sdd1::decompressBegin(uint32_t address)
{
    decoder.reset(rom, mmcOffset(address));
}

uint8_t Sdd1::decompressReadByte()
{
    return decoder.nextByte();
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

void Sdd1::Decoder::reset(std::span<const uint8_t> romBytes, size_t base)
{
    rom = romBytes;
    const uint8_t b0 = rom.empty() ? 0 : rom[base % rom.size()];
    const uint8_t b1 = rom.empty() ? 0 : rom[(base + 1) % rom.size()];

    // Header (first byte): bits 7-6 select the bitplane structure; bits 5-4
    // select the context-model neighbour set.
    bitplaneType = static_cast<uint8_t>(b0 >> 6);
    switch (b0 & 0x30) {
    case 0x00: highContext = 0x01c0; lowContext = 0x0001; break; // left, up-right, up, up-left
    case 0x10: highContext = 0x0180; lowContext = 0x0001; break; // left, up, up-left
    case 0x20: highContext = 0x00c0; lowContext = 0x0001; break; // left, up-right, up
    case 0x30: highContext = 0x0180; lowContext = 0x0003; break; // left, left2, up, up-left
    }

    // Prime the bit reservoir from the first two bytes; codewords begin after.
    inStream = static_cast<uint16_t>((b0 << 11) | (b1 << 3));
    validBits = 5;
    inOffset = base + 2;

    std::memset(bitCtr, 0, sizeof(bitCtr));
    std::memset(contextState, 0, sizeof(contextState));
    std::memset(contextMps, 0, sizeof(contextMps));
    std::memset(prevBits, 0, sizeof(prevBits));
    plane = 0;
    iCounter = 0;
    hasPending = false;
}

uint8_t Sdd1::Decoder::pullInByte()
{
    if (rom.empty()) {
        return 0;
    }
    return rom[inOffset++ % rom.size()];
}

// Read one variable-length Golomb codeword of the given order from the input
// reservoir and return the bit-generator's run descriptor: 0x80|(1<<order) for
// a full run of (1<<order) zeros, otherwise the run length from kRunTable.
int Sdd1::Decoder::getCodeword(int order)
{
    if (validBits == 0) {
        inStream |= pullInByte();
        validBits = 8;
    }
    inStream <<= 1;
    --validBits;
    inStream ^= 0x8000;
    if (inStream & 0x8000) {
        return 0x80 + (1 << order);
    }
    const uint8_t tmp = static_cast<uint8_t>((inStream >> 8) | (0x7f >> order));
    inStream <<= order;
    validBits -= order;
    if (validBits < 0) {
        inStream |= static_cast<uint16_t>(pullInByte() << (-validBits));
        validBits += 8;
    }
    return kRunTable[tmp];
}

// Bit generator for one Golomb order. Returns 0 (zero, mid-run), 1 (terminating
// one), or 2 (the final zero that closes a full-length run of zeros).
uint8_t Sdd1::Decoder::golombGetBit(int order)
{
    if (bitCtr[order] == 0) {
        bitCtr[order] = getCodeword(order);
    }
    --bitCtr[order];
    if (bitCtr[order] == 0x80) {
        bitCtr[order] = 0;
        return 2;
    }
    return (bitCtr[order] == 0) ? 1 : 0;
}

// Probability estimation: resolve one binary decision for a context, returning
// the decoded bit and advancing the context's state and MPS.
uint8_t Sdd1::Decoder::probGetBit(uint8_t context)
{
    const uint8_t state = contextState[context];
    const uint8_t g = golombGetBit(kEvo[state].order);
    if (g == 0) {
        return contextMps[context];
    }
    if (g == 2) {
        contextState[context] = kEvo[state].nextMps;
        return contextMps[context];
    }
    // g == 1: least-probable symbol terminates the run.
    const uint8_t decoded = static_cast<uint8_t>(contextMps[context] ^ 1);
    if (kEvo[state].invert) {
        contextMps[context] ^= 1;
    }
    contextState[context] = kEvo[state].nextLps;
    return decoded;
}

// Decode one bit of a bitplane: build the context from that bitplane's running
// history, decode, and shift the new bit into the history.
uint8_t Sdd1::Decoder::getBit(uint8_t bitplane)
{
    const uint16_t hist = prevBits[bitplane];
    const uint8_t context = static_cast<uint8_t>(
        ((bitplane & 1) << 4)
        | ((hist & highContext) >> 5)
        | (hist & lowContext));
    const uint8_t bit = probGetBit(context);
    prevBits[bitplane] = static_cast<uint16_t>((hist << 1) | bit);
    return bit;
}

uint8_t Sdd1::Decoder::nextByte()
{
    // Chunky ("mode 7") layout: one output byte is one pixel, one bit per plane.
    if (bitplaneType == 3) {
        uint8_t value = 0;
        for (uint8_t p = 0; p < 8; ++p) {
            if (getBit(p)) {
                value |= static_cast<uint8_t>(1u << p);
            }
        }
        return value;
    }

    // Planar layout: two interleaved planes per pass produce two output bytes;
    // the second is buffered and returned on the following call.
    if (hasPending) {
        hasPending = false;
        const uint8_t b = pendingByte;
        if (bitplaneType == 1) {
            iCounter += 32; // 8bpp: advance to the next plane pair every 16 bytes
            if (iCounter == 0) {
                plane = static_cast<uint8_t>((plane + 2) & 7);
            }
        } else if (bitplaneType == 2) {
            iCounter += 32; // 4bpp: alternate plane pairs (0,1)<->(2,3)
            if (iCounter == 0) {
                plane ^= 2;
            }
        }
        return b;
    }

    uint8_t byte1 = 0;
    uint8_t byte2 = 0;
    for (uint8_t bit = 0x80; bit; bit >>= 1) {
        if (getBit(plane)) {
            byte1 |= bit;
        }
        if (getBit(static_cast<uint8_t>(plane + 1))) {
            byte2 |= bit;
        }
    }
    pendingByte = byte2;
    hasPending = true;
    return byte1;
}

// ---------------------------------------------------------------------------
// Save state
// ---------------------------------------------------------------------------

void Sdd1::saveState(std::vector<uint8_t>& out) const
{
    out.insert(out.end(), {mmc[0], mmc[1], mmc[2], mmc[3], r4800, r4801, r4802, r4803});
}

bool Sdd1::loadState(const uint8_t* data, size_t size)
{
    if (size < 8) {
        return false;
    }
    mmc[0] = data[0];
    mmc[1] = data[1];
    mmc[2] = data[2];
    mmc[3] = data[3];
    r4800 = data[4];
    r4801 = data[5];
    r4802 = data[6];
    r4803 = data[7];
    return true;
}

} // namespace snesquik::sdd1
