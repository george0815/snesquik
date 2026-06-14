#include "SA1/sa1.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using snesquik::sa1::Sa1;

namespace {

int failures = 0;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::printf("[fail] %s\n", message);
        ++failures;
    } else {
        std::printf("[pass] %s\n", message);
    }
}

void requireEq(uint64_t got, uint64_t expected, const char* message)
{
    if (got != expected) {
        std::printf("[fail] %s: got %08llx, expected %08llx\n", message,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(expected));
        ++failures;
    } else {
        std::printf("[pass] %s\n", message);
    }
}

// A 256 KB ROM whose byte at offset N is (N & 0xff) ^ (N >> 8 & 0xff), giving
// a position-dependent value so MMC mapping can be verified.
std::vector<uint8_t> makeMarkerRom()
{
    std::vector<uint8_t> rom(0x40000, 0);
    for (size_t i = 0; i < rom.size(); ++i) {
        rom[i] = static_cast<uint8_t>((i & 0xff) ^ ((i >> 8) & 0xff));
    }
    return rom;
}

uint8_t marker(uint32_t offset)
{
    return static_cast<uint8_t>((offset & 0xff) ^ ((offset >> 8) & 0xff));
}

// Helper: write a 16-bit value to a pair of consecutive MMIO registers.
void writeIo16(Sa1& sa1, uint16_t reg, uint16_t value)
{
    sa1.cpuWriteIo(reg, static_cast<uint8_t>(value));
    sa1.cpuWriteIo(reg + 1, static_cast<uint8_t>(value >> 8));
}

void testArithmetic()
{
    Sa1 sa1;
    sa1.power();

    // Multiply (signed 16x16 -> 32): 3 * 4 = 12.
    sa1.cpuWriteIo(0x2250, 0x00); // mul mode
    writeIo16(sa1, 0x2251, 3);    // MA
    writeIo16(sa1, 0x2253, 4);    // MB (high write triggers)
    requireEq(sa1.cpuReadIo(0x2306) | (sa1.cpuReadIo(0x2307) << 8)
                  | (sa1.cpuReadIo(0x2308) << 16) | (sa1.cpuReadIo(0x2309) << 24),
              12, "multiply 3*4");

    // Multiply with negatives: -1 * 2 = -2.
    sa1.cpuWriteIo(0x2250, 0x00);
    writeIo16(sa1, 0x2251, 0xffff);
    writeIo16(sa1, 0x2253, 2);
    requireEq(sa1.cpuReadIo(0x2306) | (sa1.cpuReadIo(0x2307) << 8)
                  | (sa1.cpuReadIo(0x2308) << 16)
                  | (static_cast<uint32_t>(sa1.cpuReadIo(0x2309)) << 24),
              0xfffffffe, "multiply -1*2");

    // Divide: 10 / 3 -> quotient 3, remainder 1.
    sa1.cpuWriteIo(0x2250, 0x01); // divide mode
    writeIo16(sa1, 0x2251, 10);
    writeIo16(sa1, 0x2253, 3);
    requireEq(sa1.cpuReadIo(0x2306) | (sa1.cpuReadIo(0x2307) << 8), 3, "divide 10/3 quotient");
    requireEq(sa1.cpuReadIo(0x2308) | (sa1.cpuReadIo(0x2309) << 8), 1, "divide 10/3 remainder");

    // Floored divide with negative dividend: -10 / 3 -> quotient -4, remainder 2.
    sa1.cpuWriteIo(0x2250, 0x01);
    writeIo16(sa1, 0x2251, 0xfff6); // -10
    writeIo16(sa1, 0x2253, 3);
    requireEq(sa1.cpuReadIo(0x2306) | (sa1.cpuReadIo(0x2307) << 8), 0xfffc, "divide -10/3 quotient");
    requireEq(sa1.cpuReadIo(0x2308) | (sa1.cpuReadIo(0x2309) << 8), 2, "divide -10/3 remainder");

    // Cumulative sum (multiply-accumulate): 2*3 + 4*5 = 26.
    sa1.cpuWriteIo(0x2250, 0x02); // sum mode (clears accumulator)
    writeIo16(sa1, 0x2251, 2);
    writeIo16(sa1, 0x2253, 3);
    writeIo16(sa1, 0x2251, 4);
    writeIo16(sa1, 0x2253, 5);
    requireEq(sa1.cpuReadIo(0x2306) | (sa1.cpuReadIo(0x2307) << 8)
                  | (static_cast<uint64_t>(sa1.cpuReadIo(0x2308)) << 16)
                  | (static_cast<uint64_t>(sa1.cpuReadIo(0x2309)) << 24)
                  | (static_cast<uint64_t>(sa1.cpuReadIo(0x230a)) << 32),
              26, "cumulative sum 2*3+4*5");
    require((sa1.cpuReadIo(0x230b) & 0x80) == 0, "cumulative sum no overflow");
}

void testMmcRom()
{
    Sa1 sa1;
    sa1.power();
    const std::vector<uint8_t> rom = makeMarkerRom();
    sa1.attachRom(rom);

    // Default identity banks: $00:8000 -> rom[0], $01:8000 -> rom[0x8000].
    requireEq(sa1.cpuReadRom(0x008000), marker(0x0000), "MMC $00:8000 -> rom[0]");
    requireEq(sa1.cpuReadRom(0x008001), marker(0x0001), "MMC $00:8001 -> rom[1]");
    requireEq(sa1.cpuReadRom(0x018000), marker(0x8000), "MMC $01:8000 -> rom[0x8000]");
    // HiROM-style view: $C0:0000 -> super-bank CXB (0) offset 0 -> rom[0].
    requireEq(sa1.cpuReadRom(0xc00000), marker(0x0000), "MMC $C0:0000 -> rom[0]");
    requireEq(sa1.cpuReadRom(0xc08000), marker(0x8000), "MMC $C0:8000 -> rom[0x8000]");

    // Remap DXB ($2221) to super-bank 0; $20:8000 should then read rom[0].
    sa1.cpuWriteIo(0x2221, 0x00);
    requireEq(sa1.cpuReadRom(0x208000), marker(0x0000), "MMC remap $20:8000 -> super-bank 0");
}

void testInternalRamAndBwRam()
{
    Sa1 sa1;
    sa1.power();
    sa1.setBwRamSize(256 * 1024);

    // I-RAM is shared: write through the S-CPU window at $3000, read through
    // the SA-1 CPU's $0000 and $3000 views.
    sa1.cpuWriteIram(0x3010, 0x5a);
    requireEq(sa1.read8(0x000010), 0x5a, "I-RAM $00:0010 (SA-1 zero page)");
    requireEq(sa1.read8(0x003010), 0x5a, "I-RAM $00:3010 (both CPUs)");

    // BW-RAM linear: write via S-CPU $40:1234, read via SA-1 $40:1234.
    sa1.cpuWriteBwLinear(0x1234, 0x99);
    requireEq(sa1.read8(0x401234), 0x99, "BW-RAM linear $40:1234");

    // BW-RAM 8 KB window: S-CPU block 2 ($2224) maps $6000-$7FFF onto
    // linear offset 2*0x2000 + (offset & 0x1fff).
    sa1.cpuWriteIo(0x2224, 0x02); // BMAPS = block 2
    sa1.cpuWriteBwWindow(0x6000, 0xab);
    requireEq(sa1.cpuReadBwLinear(0x4000), 0xab, "BW-RAM window block 2 -> linear 0x4000");
}

void testIrqMessaging()
{
    Sa1 sa1;
    sa1.power();

    // SA-1 -> S-CPU IRQ: needs SIE enable ($2201 bit7) and SCNT request
    // ($2209 bit7) to assert; SIC ($2202 bit7) acknowledges.
    sa1.cpuWriteIo(0x2201, 0x80); // SIE: cpu_irqen
    require(!sa1.irqToScpuPending(), "S-CPU IRQ not pending before request");
    sa1.cpuWriteIo(0x2209, 0x80); // SCNT: cpu_irq request
    require(sa1.irqToScpuPending(), "S-CPU IRQ pending after SCNT request");
    require((sa1.cpuReadIo(0x2300) & 0x80) != 0, "SFR reflects SA-1 -> S-CPU IRQ flag");
    sa1.cpuWriteIo(0x2202, 0x80); // SIC: acknowledge
    require(!sa1.irqToScpuPending(), "S-CPU IRQ cleared by SIC");

    // S-CPU -> SA-1 message visible to the SA-1 via CFR ($2301) low nibble.
    sa1.cpuWriteIo(0x2200, 0x2a); // CCNT: message=0x0a, reset still held (bit5)
    requireEq(sa1.cpuReadIo(0x2301) & 0x0f, 0x0a, "CFR carries S-CPU message to SA-1");
}

void testSa1Execution()
{
    Sa1 sa1;
    sa1.power();
    sa1.setBwRamSize(256 * 1024);

    std::vector<uint8_t> rom(0x40000, 0xff);
    // Program at rom[0] (reached via CRV = $8000): LDA #$42; STA $3000; STP.
    rom[0] = 0xa9; rom[1] = 0x42;       // LDA #$42
    rom[2] = 0x8d; rom[3] = 0x00; rom[4] = 0x30; // STA $3000
    rom[5] = 0xdb;                      // STP
    sa1.attachRom(rom);

    writeIo16(sa1, 0x2203, 0x8000); // CRV reset vector -> $00:8000
    // Release the SA-1 from reset (CCNT bit5 = 0); other control bits clear.
    sa1.cpuWriteIo(0x2200, 0x00);
    requireEq(sa1.cpu().registers().pc, 0x8000, "SA-1 PC starts at CRV after reset release");

    sa1.stepSa1(2000); // plenty of master clocks to run 3 instructions + STP
    require(sa1.cpu().stopped(), "SA-1 reached STP");
    requireEq(sa1.cpuReadIram(0x3000), 0x42, "SA-1 executed LDA/STA into I-RAM");
}

void testSaveStateRoundTrip()
{
    Sa1 sa1;
    sa1.power();
    sa1.setBwRamSize(256 * 1024);
    sa1.cpuWriteIram(0x3001, 0x77);
    sa1.cpuWriteBwLinear(0x20, 0x88);
    sa1.cpuWriteIo(0x2224, 0x05);

    std::vector<uint8_t> blob;
    sa1.saveState(blob);

    Sa1 restored;
    restored.power();
    restored.setBwRamSize(256 * 1024);
    require(restored.loadState(blob.data(), blob.size()), "SA-1 loadState succeeds");
    requireEq(restored.cpuReadIram(0x3001), 0x77, "save state preserves I-RAM");
    requireEq(restored.cpuReadBwLinear(0x20), 0x88, "save state preserves BW-RAM");
}

void testNormalDma()
{
    Sa1 sa1;
    sa1.power();
    sa1.setBwRamSize(256 * 1024);

    // I-RAM -> BW-RAM: copy 8 bytes from I-RAM $100 to BW-RAM $200.
    for (int i = 0; i < 8; ++i) {
        sa1.cpuWriteIram(0x100 + i, static_cast<uint8_t>(0x11 * (i + 1)));
    }
    sa1.cpuWriteIo(0x2230, 0x86); // DMAEN, SD=I-RAM(2), DD=BW-RAM(1), CDEN=0
    sa1.cpuWriteIo(0x2232, 0x00); // DSA = $000100
    sa1.cpuWriteIo(0x2233, 0x01);
    sa1.cpuWriteIo(0x2234, 0x00);
    sa1.cpuWriteIo(0x2238, 0x08); // DTC = 8
    sa1.cpuWriteIo(0x2239, 0x00);
    sa1.cpuWriteIo(0x2235, 0x00); // DDA = $000200
    sa1.cpuWriteIo(0x2236, 0x02);
    sa1.cpuWriteIo(0x2237, 0x00); // high-byte write triggers (dest BW-RAM)
    bool ok = true;
    for (int i = 0; i < 8; ++i) {
        ok = ok && sa1.cpuReadBwLinear(0x200 + i) == static_cast<uint8_t>(0x11 * (i + 1));
    }
    require(ok, "normal DMA I-RAM -> BW-RAM copies bytes");

    // BW-RAM -> I-RAM: copy 4 bytes from BW-RAM $300 to I-RAM $10.
    for (int i = 0; i < 4; ++i) {
        sa1.cpuWriteBwLinear(0x300 + i, static_cast<uint8_t>(0xA0 + i));
    }
    sa1.cpuWriteIo(0x2230, 0x81); // DMAEN, SD=BW-RAM(1), DD=I-RAM(0)
    sa1.cpuWriteIo(0x2232, 0x00); // DSA = $000300
    sa1.cpuWriteIo(0x2233, 0x03);
    sa1.cpuWriteIo(0x2234, 0x00);
    sa1.cpuWriteIo(0x2238, 0x04); // DTC = 4
    sa1.cpuWriteIo(0x2239, 0x00);
    sa1.cpuWriteIo(0x2235, 0x10); // DDA = $000010
    sa1.cpuWriteIo(0x2236, 0x00); // mid-byte write triggers (dest I-RAM)
    bool ok2 = true;
    for (int i = 0; i < 4; ++i) {
        ok2 = ok2 && sa1.cpuReadIram(0x10 + i) == static_cast<uint8_t>(0xA0 + i);
    }
    require(ok2, "normal DMA BW-RAM -> I-RAM copies bytes");
}

void testCharConvType2()
{
    Sa1 sa1;
    sa1.power();
    sa1.setBwRamSize(256 * 1024);

    // 2bpp character-conversion type 2: feed one pixel row [0,1,2,3,0,1,2,3]
    // through the BRF and check the planar bitplane bytes in I-RAM.
    sa1.cpuWriteIo(0x2230, 0xa0); // DMAEN, CDEN, CDSEL=0 (type 2)
    sa1.cpuWriteIo(0x2231, 0x02); // DMACB=2 -> 2bpp, DMASIZE=0
    sa1.cpuWriteIo(0x2235, 0x00); // DDA = 0 (I-RAM tile base)
    sa1.cpuWriteIo(0x2236, 0x00);
    sa1.cpuWriteIo(0x2237, 0x00);
    const uint8_t row[8] = {0, 1, 2, 3, 0, 1, 2, 3};
    for (int i = 0; i < 8; ++i) {
        sa1.cpuWriteIo(0x2240 + i, row[i]); // writing $2247 triggers conversion
    }
    // plane0 = bit0 of each pixel, MSB-first: 0 1 0 1 0 1 0 1 = 0x55
    // plane1 = bit1 of each pixel, MSB-first: 0 0 1 1 0 0 1 1 = 0x33
    requireEq(sa1.cpuReadIram(0x0000), 0x55, "CC type2 2bpp plane0");
    requireEq(sa1.cpuReadIram(0x0001), 0x33, "CC type2 2bpp plane1");
}

} // namespace

int main()
{
    testArithmetic();
    testMmcRom();
    testInternalRamAndBwRam();
    testIrqMessaging();
    testSa1Execution();
    testSaveStateRoundTrip();
    testNormalDma();
    testCharConvType2();

    if (failures == 0) {
        std::printf("\nAll SA-1 tests passed.\n");
        return 0;
    }
    std::printf("\n%d SA-1 test(s) failed.\n", failures);
    return 1;
}
