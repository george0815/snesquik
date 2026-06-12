#include "GSU/gsu.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using snesquik::gsu::Gsu;

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

void requireEq(uint32_t got, uint32_t expected, const char* message)
{
    if (got != expected) {
        std::printf("[fail] %s: got %04x, expected %04x\n", message, got, expected);
        ++failures;
    } else {
        std::printf("[pass] %s\n", message);
    }
}

// Builds a GSU with the given code placed at $0000 in ROM bank 0 and runs
// it until STOP (or a cycle cap).
Gsu makeGsu(const std::vector<uint8_t>& code, std::vector<uint8_t>* romOut = nullptr)
{
    static std::vector<uint8_t> rom;
    rom.assign(0x10000, 0x00);
    for (size_t i = 0; i < code.size(); ++i) {
        rom[i] = code[i];
    }
    if (romOut) {
        *romOut = rom;
    }
    Gsu gsu;
    gsu.power();
    gsu.attachRom(rom);
    gsu.setRamSize(64 * 1024);
    return gsu;
}

void start(Gsu& gsu, uint16_t pc = 0)
{
    gsu.writeIo(0x303a, 0x18); // SCMR: RON + RAN
    gsu.writeIo(0x301e, static_cast<uint8_t>(pc));
    gsu.writeIo(0x301f, static_cast<uint8_t>(pc >> 8)); // sets GO
}

void runUntilStop(Gsu& gsu)
{
    for (int i = 0; i < 100000 && gsu.running(); ++i) {
        gsu.run(64);
    }
}

void testIwtAndAdd()
{
    // IWT R1,#$1234 ; IWT R2,#$0102 ; FROM R1 ; TO R3 ; ADD R2 ; STOP
    Gsu gsu = makeGsu({0xf1, 0x34, 0x12, 0xf2, 0x02, 0x01, 0xb1, 0x13, 0x52, 0x00});
    start(gsu);
    runUntilStop(gsu);
    require(!gsu.running(), "GSU stops on STOP");
    requireEq(gsu.reg(1), 0x1234, "IWT loads immediate word");
    requireEq(gsu.reg(3), 0x1336, "FROM/TO/ADD computes R3 = R1 + R2");
}

void testAltImmediateOps()
{
    // IWT R1,#$00F0 ; FROM R1 ; TO R2 ; ALT2 ; ADD #5 ; STOP
    Gsu gsu = makeGsu({0xf1, 0xf0, 0x00, 0xb1, 0x12, 0x3e, 0x55, 0x00});
    start(gsu);
    runUntilStop(gsu);
    requireEq(gsu.reg(2), 0x00f5, "ALT2 ADD uses immediate operand");
}

void testBranchDelaySlot()
{
    // IWT R3,#$0001 ; BRA +2 ; INC R3 (delay slot) ; INC R3 (skipped) ; STOP
    // Layout: f3 01 00 | 05 02 | d3 | d3 | 00 ... branch displacement +2
    // lands after the skipped INC.
    Gsu gsu = makeGsu({0xf3, 0x01, 0x00, 0x05, 0x02, 0xd3, 0xd3, 0x00, 0x00});
    start(gsu);
    runUntilStop(gsu);
    // Delay-slot INC executes, the branched-over INC does not:
    requireEq(gsu.reg(3), 0x0002, "branch executes delay slot and skips target gap");
}

void testLoop()
{
    // IWT R12,#3 ; IWT R13,#loopTop ; loopTop: INC R3 ; LOOP ; NOP(delay) ; STOP
    // loopTop = 6 (after the two IWTs).
    Gsu gsu = makeGsu({0xfc, 0x03, 0x00, 0xfd, 0x06, 0x00, 0xd3, 0x3c, 0x01, 0x00});
    start(gsu);
    runUntilStop(gsu);
    requireEq(gsu.reg(3), 0x0003, "LOOP repeats body R12 times");
}

void testRamStoreLoad()
{
    // IWT R1,#$4444 ; IWT R2,#$0100 ; FROM R1 ; STW (R2) ; IWT R3,#0 ;
    // TO R3 ; LDW (R2) ; STOP
    Gsu gsu = makeGsu({0xf1, 0x44, 0x44, 0xf2, 0x00, 0x01,
                       0xb1, 0x32, // FROM R1, STW (R2)
                       0xf3, 0x00, 0x00,
                       0x13, 0x42, // TO R3, LDW (R2)
                       0x00});
    start(gsu);
    runUntilStop(gsu);
    requireEq(gsu.reg(3), 0x4444, "STW/LDW round-trips through GSU RAM");
    requireEq(gsu.readRam(0x100), 0x44, "RAM contains stored low byte");
    requireEq(gsu.readRam(0x101), 0x44, "RAM contains stored high byte");
}

void testMultiply()
{
    // IWT R1,#$0007 ; IWT R2,#$0006 ; FROM R1 ; TO R3 ; MULT R2 ; STOP
    Gsu gsu = makeGsu({0xf1, 0x07, 0x00, 0xf2, 0x06, 0x00, 0xb1, 0x13, 0x82, 0x00});
    start(gsu);
    runUntilStop(gsu);
    requireEq(gsu.reg(3), 42, "MULT computes signed 8x8 product");
}

void testPlotWritesBitplanes()
{
    // COLOR from R5=3, plot 8 pixels at (0,0)..(7,0) in 4bpp mode.
    // IWT R5,#3 ; FROM R5 ; COLOR ; IWT R1,#0 ; IWT R2,#0 ;
    // PLOT x8 ; STOP
    std::vector<uint8_t> code = {0xf5, 0x03, 0x00, 0xb5, 0x4e,
                                 0xf1, 0x00, 0x00, 0xf2, 0x00, 0x00};
    for (int i = 0; i < 8; ++i) {
        code.push_back(0x4c);
    }
    // The pixel cache flushes lazily; RPIX forces both caches out.
    code.push_back(0x3d); // ALT1
    code.push_back(0x4c); // RPIX
    code.push_back(0x00);
    Gsu gsu = makeGsu(code);
    gsu.writeIo(0x3038, 0x00); // SCBR = 0
    gsu.writeIo(0x303a, 0x18 | 0x01); // RON+RAN, MD=1 (4bpp)
    gsu.writeIo(0x301e, 0x00);
    gsu.writeIo(0x301f, 0x00);
    runUntilStop(gsu);
    // 8 pixels of color 3 => bitplane 0 = $FF, bitplane 1 = $FF, planes 2/3 = 0.
    requireEq(gsu.readRam(0x0000), 0xff, "PLOT fills bitplane 0");
    requireEq(gsu.readRam(0x0001), 0xff, "PLOT fills bitplane 1");
    requireEq(gsu.readRam(0x0010), 0x00, "PLOT leaves bitplane 2 clear");
    requireEq(gsu.reg(1), 8, "PLOT increments R1 per pixel");
}

void testMmioRegisterAccess()
{
    Gsu gsu = makeGsu({0x00});
    gsu.writeIo(0x3006, 0xcd);
    gsu.writeIo(0x3007, 0xab);
    requireEq(gsu.reg(3), 0xabcd, "MMIO register write assembles 16-bit value");
    requireEq(gsu.readIo(0x3006), 0xcd, "MMIO register read low byte");
    requireEq(gsu.readIo(0x3007), 0xab, "MMIO register read high byte");
    require(!gsu.running(), "GSU idle before R15 write");
    start(gsu);
    require(gsu.running(), "R15 high write sets GO");
    runUntilStop(gsu);
    require(!gsu.running(), "STOP clears GO");
    require(gsu.irqPending(), "STOP raises IRQ");
    gsu.readIo(0x3031);
    require(!gsu.irqPending(), "SFR high read acknowledges IRQ");
}

void run(const char* name, void (*test)())
{
    std::printf("== %s ==\n", name);
    test();
}

} // namespace

int main()
{
    run("IWT and ADD", testIwtAndAdd);
    run("ALT immediate ops", testAltImmediateOps);
    run("branch delay slot", testBranchDelaySlot);
    run("LOOP", testLoop);
    run("RAM store/load", testRamStoreLoad);
    run("multiply", testMultiply);
    run("PLOT bitplanes", testPlotWritesBitplanes);
    run("MMIO register access", testMmioRegisterAccess);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all GSU tests passed\n");
    return 0;
}
