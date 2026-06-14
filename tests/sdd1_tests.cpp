// Tests for the S-DD1 coprocessor: MMC bank mapping, register interface, and
// the graphics decompressor (validated against Street Fighter Alpha 2 data).

#include "SDD1/sdd1.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

std::vector<uint8_t> makeRom(size_t size)
{
    std::vector<uint8_t> rom(size, 0);
    // Tag each 1 MB block so MMC mapping is observable.
    for (size_t block = 0; block * 0x100000 < size; ++block) {
        rom[block * 0x100000] = static_cast<uint8_t>(0xA0 + block);
    }
    return rom;
}

void testRegistersAndMmc()
{
    using snesquik::sdd1::Sdd1;
    Sdd1 chip;
    chip.power();
    auto rom = makeRom(8 * 1024 * 1024);
    chip.attachRom(rom);

    // Reset defaults map the first four 1 MB blocks linearly to $C0-$FF.
    CHECK(chip.readRom(0xC00000) == 0xA0); // block 0
    CHECK(chip.readRom(0xD00000) == 0xA1); // block 1
    CHECK(chip.readRom(0xE00000) == 0xA2); // block 2
    CHECK(chip.readRom(0xF00000) == 0xA3); // block 3

    // Remap the $C0-$CF quarter ($4804) to block 5.
    chip.writeRegister(0x4804, 0x05);
    CHECK(chip.readRegister(0x4804) == 0x05);
    CHECK(chip.readRom(0xC00000) == 0xA5);
    // Only the low three bits select the block.
    chip.writeRegister(0x4807, 0xF6);
    CHECK(chip.readRegister(0x4807) == 0x06);
    CHECK(chip.readRom(0xF00000) == 0xA6);

    // DMA arming registers round-trip.
    chip.writeRegister(0x4800, 0x01);
    chip.writeRegister(0x4801, 0x01);
    CHECK(chip.channelArmed(0));
    CHECK(!chip.channelArmed(1));
    chip.clearChannelArm(0);
    CHECK(!chip.channelArmed(0));     // $4801 bit cleared
    CHECK(chip.readRegister(0x4800) == 0x01); // $4800 enable persists
}

void testDecompression()
{
    using snesquik::sdd1::Sdd1;
    std::ifstream file("tests/roms/sfa2.sfc", std::ios::binary);
    if (!file) {
        std::printf("SKIP decompression test (tests/roms/sfa2.sfc not found)\n");
        return;
    }
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    CHECK(rom.size() == 4u * 1024 * 1024);

    Sdd1 chip;
    chip.power();
    chip.attachRom(rom);

    // The first compressed graphics block (CAPCOM logo / intro art). $DA1C01
    // resolves through the reset MMC to ROM offset 0x1A1C01.
    auto decode = [&]() {
        chip.decompressBegin(0xDA1C01);
        std::vector<uint8_t> out(2048);
        for (auto& b : out) {
            b = chip.decompressReadByte();
        }
        return out;
    };

    const std::vector<uint8_t> out = decode();

    // Golden values captured from the validated decoder (the block renders as
    // the CAPCOM logo on screen). These lock the exact algorithm.
    int zeros = 0;
    for (uint8_t b : out) {
        if (b == 0) {
            ++zeros;
        }
    }
    CHECK(zeros == 488);
    CHECK(out[37] == 0x80);
    CHECK(out[39] == 0x86);
    CHECK(out[40] == 0x86);
    CHECK(out[41] == 0xcf);
    CHECK(out[42] == 0xcc);

    // Decompression is deterministic when restarted from the same source.
    CHECK(decode() == out);
}

} // namespace

int main()
{
    testRegistersAndMmc();
    testDecompression();
    if (g_failures == 0) {
        std::printf("All S-DD1 tests passed\n");
        return 0;
    }
    std::printf("%d S-DD1 test(s) failed\n", g_failures);
    return 1;
}
