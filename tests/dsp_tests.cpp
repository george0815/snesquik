#include "DSP/necdsp.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

using snesquik::dsp::NecDsp;

namespace {

int failures = 0;

void require(bool condition, const char* message)
{
    std::printf("%s %s\n", condition ? "[pass]" : "[fail]", message);
    if (!condition) {
        ++failures;
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

// Opcode encoders for the µPD7725 instruction classes.
uint32_t ld(uint16_t imm, uint8_t dst)
{
    return (0x3u << 22) | (static_cast<uint32_t>(imm) << 6) | (dst & 0x0f);
}
uint32_t op(uint8_t pselect, uint8_t alu, uint8_t asl, uint8_t src, uint8_t dst)
{
    return (0x0u << 22) | ((pselect & 3u) << 20) | ((alu & 0xfu) << 16)
         | ((asl & 1u) << 15) | ((src & 0xfu) << 4) | (dst & 0xfu);
}
uint32_t jp(uint16_t brch, uint16_t na)
{
    return (0x2u << 22) | ((brch & 0x1ffu) << 13) | ((na & 0x7ffu) << 2);
}

// Build a DSP with a hand-written program and run `count` instructions.
NecDsp makeDsp(std::initializer_list<uint32_t> program)
{
    NecDsp dsp;
    dsp.power();
    size_t i = 0;
    for (uint32_t word : program) {
        dsp.programRom[i++] = word;
    }
    dsp.romLoaded = true;
    return dsp;
}

void testLoadImmediate()
{
    NecDsp dsp = makeDsp({ld(0x1234, 1), ld(0x5678, 2)});
    dsp.exec();
    dsp.exec();
    requireEq(dsp.regs.a, 0x1234, "LD #$1234 -> A");
    requireEq(dsp.regs.b, 0x5678, "LD #$5678 -> B");
}

void testAluAddSub()
{
    // A = 5; A = A + K(3) -> 8; flags clear.
    NecDsp dsp = makeDsp({op(1, 5, 0, 13, 0)}); // pselect=idb, ADD, src=K, dst=NON
    dsp.regs.a = 5;
    dsp.regs.k = 3;
    dsp.exec();
    requireEq(dsp.regs.a, 8, "ALU ADD A=5+3");
    require(!dsp.regs.flagA.z && !dsp.regs.flagA.s0 && !dsp.regs.flagA.c, "ADD flags clear");

    // A = 3; A = A - K(3) -> 0; zero flag set.
    NecDsp dsp2 = makeDsp({op(1, 4, 0, 13, 0)}); // SUB
    dsp2.regs.a = 3;
    dsp2.regs.k = 3;
    dsp2.exec();
    requireEq(dsp2.regs.a, 0, "ALU SUB A=3-3");
    require(dsp2.regs.flagA.z, "SUB zero flag set");
}

void testAluLogic()
{
    NecDsp dsp = makeDsp({op(1, 2, 0, 13, 0)}); // AND
    dsp.regs.a = 0xff0f;
    dsp.regs.k = 0x0ff0;
    dsp.exec();
    requireEq(dsp.regs.a, 0x0f00, "ALU AND");
}

void testMultiplier()
{
    // K * L = 0x4000 * 2 = 0x8000; M = result>>15 = 1, N = result<<1 = 0.
    NecDsp dsp = makeDsp({op(0, 0, 0, 0, 0)}); // any OP refreshes M:N
    dsp.regs.k = 0x4000;
    dsp.regs.l = 0x0002;
    dsp.exec();
    requireEq(dsp.regs.m, 1, "multiplier M");
    requireEq(dsp.regs.n, 0, "multiplier N");
}

void testJump()
{
    NecDsp dsp = makeDsp({jp(0x100, 0x010)}); // unconditional JMP $010
    dsp.exec();
    requireEq(dsp.regs.pc, 0x010, "unconditional JMP");

    // JNZA taken when A zero flag is clear.
    NecDsp dsp2 = makeDsp({jp(0x088, 0x020)});
    dsp2.regs.flagA.z = false;
    dsp2.exec();
    requireEq(dsp2.regs.pc, 0x020, "JNZA taken (z=0)");

    NecDsp dsp3 = makeDsp({jp(0x088, 0x020)});
    dsp3.regs.flagA.z = true;
    dsp3.exec();
    requireEq(dsp3.regs.pc, 0x001, "JNZA not taken (z=1) -> fallthrough");
}

void testCallReturn()
{
    // CALL $005 then (at $005) RT. PC should return to instruction after CALL.
    NecDsp dsp;
    dsp.power();
    dsp.programRom[0] = jp(0x140, 0x005); // CALL $005
    dsp.programRom[5] = (0x1u << 22);     // RT (type 1, no-op OP body)
    dsp.romLoaded = true;
    dsp.exec(); // CALL: pc 0->stack, pc=5
    requireEq(dsp.regs.pc, 0x005, "CALL sets PC");
    dsp.exec(); // RT: pc=stack(1)
    requireEq(dsp.regs.pc, 0x001, "RT restores PC after CALL");
}

void testDrSrHandshake()
{
    // Interface-only (no program): 16-bit DR read low-then-high, clearing RQM.
    NecDsp dsp;
    dsp.power();           // romLoaded == false, so runToRequest() is a no-op
    dsp.sr = NecDsp::srRqm; // RQM set, 16-bit mode (DRC=0), DRS=0
    dsp.dr = 0x1234;
    require((dsp.readSR() & 0x80) != 0, "SR high byte exposes RQM as bit7");
    requireEq(dsp.readDR(), 0x34, "DR read low byte first");
    requireEq(dsp.readDR(), 0x12, "DR read high byte second");
    require((dsp.readSR() & 0x80) == 0, "RQM cleared after 16-bit DR read");

    // 16-bit DR write low-then-high.
    NecDsp dsp2;
    dsp2.power();
    dsp2.sr = NecDsp::srRqm;
    dsp2.writeDR(0x78);
    dsp2.writeDR(0x56);
    requireEq(dsp2.dr, 0x5678, "DR write assembles 16-bit word");
    require((dsp2.readSR() & 0x80) == 0, "RQM cleared after 16-bit DR write");
}

void testDumpBackedBoot()
{
    NecDsp dsp;
    dsp.power();
    if (!dsp.loadRom("tests/roms/dsp1b.rom") && !dsp.loadRom("tests/roms/dsp1.rom")) {
        std::printf("[skip] DSP-1 dump not present; skipping ROM-backed boot test\n");
        return;
    }
    // After reset the real DSP-1 program runs its init and raises RQM to ask
    // the S-CPU for the first transfer. If our decode is sound, RQM appears.
    dsp.step(100000);
    require((dsp.readSR() & 0x80) != 0, "DSP-1 ROM boots and raises RQM");
}

} // namespace

int main()
{
    testLoadImmediate();
    testAluAddSub();
    testAluLogic();
    testMultiplier();
    testJump();
    testCallReturn();
    testDrSrHandshake();
    testDumpBackedBoot();

    if (failures == 0) {
        std::printf("\nAll DSP tests passed.\n");
        return 0;
    }
    std::printf("\n%d DSP test(s) failed.\n", failures);
    return 1;
}
