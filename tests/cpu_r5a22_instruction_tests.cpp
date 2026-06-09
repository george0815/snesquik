#include "CPU_R5A22/core.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace snesquik::cpu_r5a22;

namespace {

class TestBus final : public Bus {
public:
    uint8_t read8(uint32_t address) override
    {
        return memory[address & 0x00ffffff];
    }

    void write8(uint32_t address, uint8_t value) override
    {
        memory[address & 0x00ffffff] = value;
    }

    void load(uint32_t address, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t byte : bytes) {
            write8(address++, byte);
        }
    }

private:
    std::vector<uint8_t> memory = std::vector<uint8_t>(0x01000000);
};

struct Harness {
    TestBus bus;
    CPU cpu{bus};

    Harness()
    {
        bus.write8(0xfffc, 0x00);
        bus.write8(0xfffd, 0x80);
        cpu.reset();
    }
};

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireEq(uint32_t actual, uint32_t expected, const std::string& message)
{
    if (actual != expected) {
        throw std::runtime_error(message + ": got " + std::to_string(actual) + ", expected " + std::to_string(expected));
    }
}

void run(const char* name, void (*test)())
{
    test();
    std::cout << "[pass] " << name << '\n';
}

void testOpcodeTableHasEveryOfficialOpcode()
{
    static constexpr std::array<const char*, 256> expected = {{
        "BRK", "ORA", "COP", "ORA", "TSB", "ORA", "ASL", "ORA", "PHP", "ORA", "ASL", "PHD", "TSB", "ORA", "ASL", "ORA",
        "BPL", "ORA", "ORA", "ORA", "TRB", "ORA", "ASL", "ORA", "CLC", "ORA", "INC", "TCS", "TRB", "ORA", "ASL", "ORA",
        "JSR", "AND", "JSL", "AND", "BIT", "AND", "ROL", "AND", "PLP", "AND", "ROL", "PLD", "BIT", "AND", "ROL", "AND",
        "BMI", "AND", "AND", "AND", "BIT", "AND", "ROL", "AND", "SEC", "AND", "DEC", "TSC", "BIT", "AND", "ROL", "AND",
        "RTI", "EOR", "WDM", "EOR", "MVP", "EOR", "LSR", "EOR", "PHA", "EOR", "LSR", "PHK", "JMP", "EOR", "LSR", "EOR",
        "BVC", "EOR", "EOR", "EOR", "MVN", "EOR", "LSR", "EOR", "CLI", "EOR", "PHY", "TCD", "JML", "EOR", "LSR", "EOR",
        "RTS", "ADC", "PER", "ADC", "STZ", "ADC", "ROR", "ADC", "PLA", "ADC", "ROR", "RTL", "JMP", "ADC", "ROR", "ADC",
        "BVS", "ADC", "ADC", "ADC", "STZ", "ADC", "ROR", "ADC", "SEI", "ADC", "PLY", "TDC", "JMP", "ADC", "ROR", "ADC",
        "BRA", "STA", "BRL", "STA", "STY", "STA", "STX", "STA", "DEY", "BIT", "TXA", "PHB", "STY", "STA", "STX", "STA",
        "BCC", "STA", "STA", "STA", "STY", "STA", "STX", "STA", "TYA", "STA", "TXS", "TXY", "STZ", "STA", "STZ", "STA",
        "LDY", "LDA", "LDX", "LDA", "LDY", "LDA", "LDX", "LDA", "TAY", "LDA", "TAX", "PLB", "LDY", "LDA", "LDX", "LDA",
        "BCS", "LDA", "LDA", "LDA", "LDY", "LDA", "LDX", "LDA", "CLV", "LDA", "TSX", "TYX", "LDY", "LDA", "LDX", "LDA",
        "CPY", "CMP", "REP", "CMP", "CPY", "CMP", "DEC", "CMP", "INY", "CMP", "DEX", "WAI", "CPY", "CMP", "DEC", "CMP",
        "BNE", "CMP", "CMP", "CMP", "PEI", "CMP", "DEC", "CMP", "CLD", "CMP", "PHX", "STP", "JML", "CMP", "DEC", "CMP",
        "CPX", "SBC", "SEP", "SBC", "CPX", "SBC", "INC", "SBC", "INX", "SBC", "NOP", "XBA", "CPX", "SBC", "INC", "SBC",
        "BEQ", "SBC", "SBC", "SBC", "PEA", "SBC", "INC", "SBC", "SED", "SBC", "PLX", "XCE", "JSR", "SBC", "INC", "SBC",
    }};

    const auto& table = opcodeTable();
    std::unordered_set<std::string> mnemonics;
    for (size_t opcode = 0; opcode < table.size(); ++opcode) {
        require(table[opcode].operation != nullptr, "opcode operation pointer is null");
        require(table[opcode].addressing != nullptr, "opcode addressing pointer is null");
        require(table[opcode].mnemonic != nullptr, "opcode mnemonic is null");
        require(std::string(table[opcode].mnemonic) == expected[opcode], "opcode mnemonic mismatch");
        require(table[opcode].baseBytes >= 1 && table[opcode].baseBytes <= 4, "opcode byte count out of range");
        mnemonics.insert(table[opcode].mnemonic);
    }

    requireEq(static_cast<uint32_t>(mnemonics.size()), 92, "unique instruction mnemonic count");
}

void testLoadStoreAndTransferInstructions()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.p = Memory8 | Index8;
    r.a = 0x1200;

    h.bus.load(0x8000, {0xa9, 0x44, 0xaa, 0xa8, 0x8d, 0x34, 0x12, 0xa2, 0x55, 0xa0, 0x66, 0x8e, 0x35, 0x12, 0x8c, 0x36, 0x12,
                        0x64, 0x37, 0x9c, 0x38, 0x12, 0x9a, 0x9b, 0x98, 0xbb, 0xba, 0x8a});
    for (int i = 0; i < 18; ++i) {
        h.cpu.step();
    }

    requireEq(r.a & 0xff, r.x & 0xff, "TXA transfers X to A");
    requireEq(r.y & 0xff, 0x55, "TXY transfers X to Y");
    requireEq(h.bus.read8(0x001234), 0x44, "STA writes accumulator");
    requireEq(h.bus.read8(0x001235), 0x55, "STX writes X");
    requireEq(h.bus.read8(0x001236), 0x66, "STY writes Y");
    requireEq(h.bus.read8(0x000037), 0x00, "STZ direct writes zero");
    requireEq(h.bus.read8(0x001238), 0x00, "STZ absolute writes zero");
}

void testArithmeticLogicCompareAndBitInstructions()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.p = Memory8 | Index8;
    h.bus.load(0x8000, {0x18, 0xa9, 0x10, 0x69, 0x22, 0xe9, 0x02, 0x29, 0x0f, 0x09, 0x80, 0x49, 0xff, 0xc9, 0x70,
                        0xe0, 0x00, 0xc0, 0x00, 0x89, 0x01, 0x24, 0x20, 0x04, 0x21, 0x14, 0x21});
    h.bus.write8(0x20, 0xc0);
    h.bus.write8(0x21, 0x0f);
    for (int i = 0; i < 16; ++i) {
        h.cpu.step();
    }

    requireEq(r.a & 0xff, 0x70, "ADC/SBC/AND/ORA/EOR result");
    require(h.cpu.flag(Carry), "CMP sets carry for equal values");
    require(!h.cpu.flag(Zero), "TRB clears zero when A and memory overlap");
    requireEq(h.bus.read8(0x21), 0x0f, "TSB/TRB memory round trip");
}

void testIndexedAddressingCarriesAcrossDataBank()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.emulation = false;
    r.p = Carry;
    r.a = 0x1234;
    r.y = 0x1100;
    r.db = 0x7e;
    r.d = 0xffff;
    h.bus.load(0x8000, {0x71, 0x34});
    h.bus.write8(0x000033, 0xdc);
    h.bus.write8(0x000034, 0xfe);
    h.bus.write8(0x7f0fdc, 0xcb);
    h.bus.write8(0x7f0fdd, 0xed);

    h.cpu.step();
    requireEq(r.a, 0x0000, "ADC (dp),Y carries effective address into next data bank");
    requireEq(r.p & (Carry | Zero), Carry | Zero, "ADC (dp),Y result flags");

    h.cpu.reset();
    auto& r2 = h.cpu.mutableRegisters();
    r2.emulation = false;
    r2.p = Carry;
    r2.a = 0x1234;
    r2.y = 0x0300;
    r2.db = 0x7e;
    h.bus.load(0x8000, {0x79, 0xff, 0xff});
    h.bus.write8(0x7f02ff, 0xcb);
    h.bus.write8(0x7f0300, 0xed);

    h.cpu.step();
    requireEq(r2.a, 0x0000, "ADC absolute,Y carries effective address into next data bank");
}

void testDirectIndexedIndirectWrapsFullDirectPage()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.emulation = false;
    r.p = Carry;
    r.a = 0x1234;
    r.x = 0x0123;
    r.y = 0x5678;
    r.db = 0x7e;
    r.d = 0xff00;

    h.bus.load(0x8000, {0x61, 0x10});
    h.bus.write8(0x000033, 0xff);
    h.bus.write8(0x000034, 0xff);
    h.bus.write8(0x7effff, 0xcb);
    h.bus.write8(0x7f0000, 0xed);

    h.cpu.step();
    requireEq(r.a, 0x0000, "ADC (dp,X) direct pointer wraps across $FFFF to $0000");
    requireEq(r.p & (Carry | Zero), Carry | Zero, "ADC (dp,X) wrapped pointer result flags");
}

void testEmulationDirectIndexedIndirectWrapsPageAlignedDirectPage()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.emulation = true;
    r.p = Memory8 | Index8 | Carry;
    r.a = 0x1112;
    r.x = 0x0010;
    r.y = 0x5678;
    r.db = 0x7f;
    r.d = 0x0100;

    h.bus.load(0x8000, {0x61, 0xf0});
    h.bus.write8(0x000100, 0x34);
    h.bus.write8(0x000101, 0x12);
    h.bus.write8(0x7f1234, 0xed);

    h.cpu.step();
    requireEq(r.a, 0x1100, "emulation ADC (dp,X) wraps page-aligned direct indexed pointer");
    requireEq(r.p & (Carry | Zero | Negative), Carry | Zero, "emulation ADC (dp,X) wrapped result flags");
}

void testEmulationDirectIndexedIndirectWrapsPointerHighByteWithinPage()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.emulation = true;
    r.p = Memory8 | Index8 | Carry;
    r.a = 0x1112;
    r.x = 0x00ee;
    r.y = 0x5678;
    r.db = 0x7f;
    r.d = 0x011a;

    h.bus.load(0x8000, {0x61, 0xf7});
    h.bus.write8(0x0002ff, 0x34);
    h.bus.write8(0x000200, 0x12);
    h.bus.write8(0x7f1234, 0xed);

    h.cpu.step();
    requireEq(r.a, 0x1100, "emulation ADC (dp,X) wraps pointer high byte within page");
    requireEq(r.p & (Carry | Zero | Negative), Carry | Zero, "emulation ADC (dp,X) page-wrapped pointer result flags");
}

void testEmulationDirectLongIndirectDoesNotWrapPointerBytesWithinPage()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.emulation = true;
    r.p = Memory8 | Index8 | Carry;
    r.a = 0x1112;
    r.x = 0x3456;
    r.y = 0x5678;
    r.d = 0x0100;

    h.bus.load(0x8000, {0x67, 0xff});
    h.bus.write8(0x0001ff, 0x34);
    h.bus.write8(0x000200, 0x12);
    h.bus.write8(0x000201, 0x7f);
    h.bus.write8(0x7f1234, 0xed);

    h.cpu.step();
    requireEq(r.a, 0x1100, "emulation ADC [dp] reads 24-bit pointer across direct page");
    requireEq(r.p & (Carry | Zero | Negative), Carry | Zero, "emulation ADC [dp] long pointer result flags");
}

void testAccumulatorToIndexTransfersUseFullAccumulator()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.emulation = false;
    r.p = Memory8;
    r.a = 0x1234;
    h.bus.load(0x8000, {0xaa, 0xa8});

    h.cpu.step();
    requireEq(r.x, 0x1234, "TAX copies full internal accumulator when X is 16-bit");
    h.cpu.step();
    requireEq(r.y, 0x1234, "TAY copies full internal accumulator when Y is 16-bit");
}

void testDecimalSbcUsesBinaryOverflowFlag()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.emulation = false;
    r.p = Decimal | Carry;
    r.a = 0x1000;
    h.bus.load(0x8000, {0xe9, 0x00, 0x90});

    h.cpu.step();
    requireEq(r.a, 0x2000, "decimal SBC computes BCD-adjusted result");
    require(h.cpu.flag(Overflow), "decimal SBC overflow follows binary subtraction");
    require(!h.cpu.flag(Carry), "decimal SBC clears carry on borrow");
    require(!h.cpu.flag(Negative), "decimal SBC negative follows adjusted result");
}

void testDecimalAdcUsesAdjustedOverflowFlag()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.emulation = false;
    r.p = Decimal;
    r.a = 0x3550;
    h.bus.load(0x8000, {0x69, 0x70, 0x44});

    h.cpu.step();
    requireEq(r.a, 0x8020, "decimal ADC computes BCD-adjusted result");
    require(h.cpu.flag(Overflow), "decimal ADC overflow follows adjusted result");
    require(h.cpu.flag(Negative), "decimal ADC negative follows adjusted result");
    require(!h.cpu.flag(Carry), "decimal ADC leaves carry clear without BCD carry-out");
}

void testShiftIncrementFlagAndWidthInstructions()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.p = Memory8 | Index8;
    h.bus.load(0x8000, {0xa9, 0x81, 0x0a, 0x4a, 0x2a, 0x6a, 0x1a, 0x3a, 0xe8, 0xc8, 0xca, 0x88, 0x38, 0x18, 0xf8, 0xd8,
                        0x78, 0x58, 0xb8, 0xe2, 0x01, 0xc2, 0x01});
    for (int i = 0; i < 20; ++i) {
        h.cpu.step();
    }

    require(!h.cpu.flag(Carry), "CLC/REP clear carry");
    require(!h.cpu.flag(Decimal), "CLD clears decimal");
    require(!h.cpu.flag(InterruptDisable), "CLI clears interrupt disable");
    require(!h.cpu.flag(Overflow), "CLV clears overflow");
    requireEq(r.a & 0xff, 0x01, "ASL/LSR/ROL/ROR/INC/DEC accumulator sequence");
}

void testBranchJumpReturnAndInterruptInstructions()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.p = Memory8 | Index8;
    h.bus.write8(0xfffe, 0x00);
    h.bus.write8(0xffff, 0x90);
    h.bus.load(0x8000, {0x80, 0x02, 0xea, 0xea, 0x82, 0x00, 0x00, 0x00});
    h.bus.load(0x8007, {0x20, 0x20, 0x80});
    h.bus.load(0x8020, {0x60});
    h.cpu.step();
    requireEq(r.pc, 0x8004, "BRA branches relative");
    h.cpu.step();
    requireEq(r.pc, 0x8007, "BRL branches relative long");
    h.cpu.step();
    requireEq(r.pc, 0x8020, "JSR jumps to subroutine");
    h.cpu.step();
    requireEq(r.pc, 0x800a, "RTS returns to caller");

    h.bus.load(0x800a, {0x00, 0x00});
    h.cpu.step();
    requireEq(r.pc, 0x9000, "BRK uses emulation IRQ/BRK vector");
}

void testStackAndReturnInstructions()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.p = Memory8 | Index8;
    r.a = 0x0034;
    r.x = 0x0056;
    r.y = 0x0078;
    r.d = 0x1234;
    r.db = 0x7e;
    r.pb = 0x80;

    h.bus.load(0x8000, {0x48, 0xda, 0x5a, 0x8b, 0x4b, 0x0b, 0x08, 0x28, 0x2b, 0xab, 0x7a, 0xfa, 0x68});
    for (int i = 0; i < 13; ++i) {
        h.cpu.step();
    }

    requireEq(r.a & 0xff, 0x34, "PHA/PLA restore A");
    requireEq(r.x & 0xff, 0x56, "PHX/PLX restore X");
    requireEq(r.y & 0xff, 0x78, "PHY/PLY restore Y");
    requireEq(r.db, 0x7e, "PHB/PLB restore DBR");
    requireEq(r.d, 0x1234, "PHD/PLD restore direct page");
}

void testRegisterTransferAndModeInstructions()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.p = Memory8 | Index8;
    r.a = 0x3456;
    r.s = 0x01f0;
    h.bus.load(0x8000, {0x5b, 0x7b, 0x1b, 0x3b, 0xeb, 0x18, 0xfb, 0x38, 0xfb});
    for (int i = 0; i < 9; ++i) {
        h.cpu.step();
    }

    require(r.emulation, "XCE can return to emulation mode");
    requireEq(r.d, 0x3456, "TCD stores A in direct page");
    requireEq(r.a, 0x5601, "XBA swaps accumulator bytes");
}

void testBlockMoveWaitStopAndNoOpInstructions()
{
    Harness h;
    auto& r = h.cpu.mutableRegisters();
    r.p = Memory8 | Index8;
    r.a = 0;
    r.x = 0x1000;
    r.y = 0x2000;
    h.bus.write8(0x011000, 0xaa);
    h.bus.load(0x8000, {0x54, 0x7e, 0x01, 0xea, 0x42, 0x00, 0xcb});
    h.cpu.step();
    requireEq(h.bus.read8(0x7e2000), 0xaa, "MVN copies one byte");
    h.cpu.step();
    requireEq(r.pc, 0x8004, "NOP advances PC");
    h.cpu.step();
    requireEq(r.pc, 0x8006, "WDM consumes signature byte");
    h.cpu.step();
    require(h.cpu.waiting(), "WAI enters wait state");
    h.cpu.requestIRQ();
    h.cpu.clearIRQ();
    h.bus.load(r.pb << 16 | r.pc, {0xdb});
    h.cpu.step();
    require(h.cpu.stopped(), "STP enters stopped state");
}

} // namespace

int main()
{
    try {
        run("opcode table covers every official opcode", testOpcodeTableHasEveryOfficialOpcode);
        run("load/store/transfer instructions", testLoadStoreAndTransferInstructions);
        run("arithmetic/logic/compare/bit instructions", testArithmeticLogicCompareAndBitInstructions);
        run("indexed addressing carries across data bank", testIndexedAddressingCarriesAcrossDataBank);
        run("direct indexed indirect wraps full direct page", testDirectIndexedIndirectWrapsFullDirectPage);
        run("emulation direct indexed indirect wraps page-aligned direct page", testEmulationDirectIndexedIndirectWrapsPageAlignedDirectPage);
        run("emulation direct indexed indirect wraps pointer high byte", testEmulationDirectIndexedIndirectWrapsPointerHighByteWithinPage);
        run("emulation direct long indirect does not wrap pointer bytes", testEmulationDirectLongIndirectDoesNotWrapPointerBytesWithinPage);
        run("accumulator to index transfer width", testAccumulatorToIndexTransfersUseFullAccumulator);
        run("decimal SBC binary overflow flag", testDecimalSbcUsesBinaryOverflowFlag);
        run("decimal ADC adjusted overflow flag", testDecimalAdcUsesAdjustedOverflowFlag);
        run("shift/increment/flag/width instructions", testShiftIncrementFlagAndWidthInstructions);
        run("branch/jump/return/interrupt instructions", testBranchJumpReturnAndInterruptInstructions);
        run("stack instructions", testStackAndReturnInstructions);
        run("register transfer and mode instructions", testRegisterTransferAndModeInstructions);
        run("block move/wait/stop/no-op instructions", testBlockMoveWaitStopAndNoOpInstructions);
    } catch (const std::exception& error) {
        std::cerr << "[fail] " << error.what() << '\n';
        return 1;
    }
}
