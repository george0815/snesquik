// 65816 addressing-mode resolvers. Each consumes the instruction's operand
// bytes via fetch8/fetch16/fetch24, applies the mode's index/indirection
// rules (including the emulation-mode wrapping quirks), charges any
// mode-dependent penalty cycles, and returns a resolved Operand for the
// operation to load/store through.
#include "core.h"

namespace snesquik::cpu_r5a22::addressing {

namespace {

uint32_t banked(uint8_t bank, uint16_t address)
{
    return (static_cast<uint32_t>(bank) << 16) | address;
}

uint32_t bankedIndexed(uint8_t bank, uint16_t address, uint16_t index)
{
    return (banked(bank, address) + index) & 0x00ffffff;
}

// Direct-page addresses are D + offset in bank 0. Hardware charges one extra
// cycle whenever D's low byte is non-zero (the address adder can't shortcut),
// which is why games keep D page-aligned in hot code.
uint16_t directAddress(CPU& cpu, uint8_t offset)
{
    auto& r = cpu.mutableRegisters();
    const uint16_t address = static_cast<uint16_t>(r.d + offset);
    if ((r.d & 0x00ff) != 0) {
        cpu.addCycles(1);
    }
    return address;
}

uint16_t directIndexed(CPU& cpu, uint8_t offset, uint16_t index)
{
    auto& r = cpu.mutableRegisters();
    if ((r.d & 0x00ff) != 0) {
        cpu.addCycles(1);
    }
    // 6502 compatibility: in emulation mode with a page-aligned D, dp,X/dp,Y
    // wrap within the direct page (zp,X behavior). With DL != 0 the 65816
    // drops the wrap and indexes linearly.
    if (r.emulation && (r.d & 0x00ff) == 0) {
        return static_cast<uint16_t>((r.d & 0xff00) | static_cast<uint8_t>(offset + index));
    }
    return static_cast<uint16_t>(r.d + offset + index);
}

uint16_t readDirect16(CPU& cpu, uint16_t address)
{
    auto& r = cpu.mutableRegisters();
    // The 16-bit pointer fetch wraps within the direct page only in
    // emulation mode with DL = 0; otherwise it wraps at the bank-0
    // 16-bit boundary.
    if (r.emulation && (r.d & 0x00ff) == 0) {
        const uint16_t page = address & 0xff00;
        const uint16_t lo = cpu.read8(address);
        const uint16_t hi = cpu.read8(page | static_cast<uint8_t>(address + 1));
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    return cpu.read16BankWrap(0, address);
}

uint32_t readDirect24(CPU& cpu, uint16_t address)
{
    return cpu.read24(address);
}

} // namespace

Operand implied(CPU&)
{
    return {};
}

Operand accumulator(CPU&)
{
    Operand operand;
    operand.accumulator = true;
    return operand;
}

Operand immediateM(CPU& cpu)
{
    Operand operand;
    operand.immediate = true;
    operand.size = cpu.accumulatorWidth();
    operand.value = operand.size == 1 ? cpu.fetch8() : cpu.fetch16();
    return operand;
}

Operand immediateX(CPU& cpu)
{
    Operand operand;
    operand.immediate = true;
    operand.size = cpu.indexWidth();
    operand.value = operand.size == 1 ? cpu.fetch8() : cpu.fetch16();
    return operand;
}

Operand immediate8(CPU& cpu)
{
    Operand operand;
    operand.immediate = true;
    operand.size = 1;
    operand.value = cpu.fetch8();
    return operand;
}

Operand direct(CPU& cpu)
{
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = directAddress(cpu, cpu.fetch8());
    return operand;
}

Operand directX(CPU& cpu)
{
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = directIndexed(cpu, cpu.fetch8(), cpu.registers().x);
    return operand;
}

Operand directY(CPU& cpu)
{
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = directIndexed(cpu, cpu.fetch8(), cpu.registers().y);
    return operand;
}

Operand directIndirect(CPU& cpu)
{
    auto& r = cpu.mutableRegisters();
    const uint16_t ptr = directAddress(cpu, cpu.fetch8());
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = banked(r.db, readDirect16(cpu, ptr));
    return operand;
}

Operand directIndirectX(CPU& cpu)
{
    auto& r = cpu.mutableRegisters();
    const uint16_t ptr = directIndexed(cpu, cpu.fetch8(), r.x);
    uint16_t base;
    if (r.emulation && (r.d & 0x00ff) != 0) {
        // Undocumented (dp,X) quirk: the low pointer byte is read without
        // page wrapping, but the +1 for the high byte wraps within the
        // page. Applies only to this addressing mode.
        const uint16_t lo = cpu.read8(ptr);
        const uint16_t hi = cpu.read8((ptr & 0xff00) | static_cast<uint8_t>(ptr + 1));
        base = static_cast<uint16_t>(lo | (hi << 8));
    } else {
        base = readDirect16(cpu, ptr);
    }
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = banked(r.db, base);
    return operand;
}

Operand directIndirectY(CPU& cpu)
{
    auto& r = cpu.mutableRegisters();
    const uint16_t ptr = directAddress(cpu, cpu.fetch8());
    const uint16_t base = readDirect16(cpu, ptr);
    const uint16_t indexed = static_cast<uint16_t>(base + r.y);
    if ((base & 0xff00) != (indexed & 0xff00) || cpu.indexWidth() == 2) {
        cpu.addCycles(1);
    }

    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = bankedIndexed(r.db, base, r.y);
    return operand;
}

Operand directIndirectLong(CPU& cpu)
{
    const uint16_t ptr = directAddress(cpu, cpu.fetch8());
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = readDirect24(cpu, ptr);
    return operand;
}

Operand directIndirectLongY(CPU& cpu)
{
    auto& r = cpu.mutableRegisters();
    const uint32_t base = readDirect24(cpu, directAddress(cpu, cpu.fetch8()));
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = (base + r.y) & 0x00ffffff;
    return operand;
}

Operand absolute(CPU& cpu)
{
    auto& r = cpu.mutableRegisters();
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = banked(r.db, cpu.fetch16());
    return operand;
}

// Indexed absolute modes pay one extra cycle when the index is 16-bit or the
// add crosses a page boundary — the CPU needs the extra bus cycle to fix up
// the high address byte. Indexing may carry past the bank into DB+1
// (bankedIndexed masks to 24 bits), unlike the bank-wrapped pointer fetches.
Operand absoluteX(CPU& cpu)
{
    auto& r = cpu.mutableRegisters();
    const uint16_t base = cpu.fetch16();
    const uint16_t indexed = static_cast<uint16_t>(base + r.x);
    if ((base & 0xff00) != (indexed & 0xff00) || cpu.indexWidth() == 2) {
        cpu.addCycles(1);
    }

    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = bankedIndexed(r.db, base, r.x);
    return operand;
}

Operand absoluteY(CPU& cpu)
{
    auto& r = cpu.mutableRegisters();
    const uint16_t base = cpu.fetch16();
    const uint16_t indexed = static_cast<uint16_t>(base + r.y);
    if ((base & 0xff00) != (indexed & 0xff00) || cpu.indexWidth() == 2) {
        cpu.addCycles(1);
    }

    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = bankedIndexed(r.db, base, r.y);
    return operand;
}

Operand absoluteIndirect(CPU& cpu)
{
    const uint16_t pointer = cpu.fetch16();
    Operand operand;
    operand.hasAddress = true;
    operand.address = cpu.read16BankWrap(0, pointer);
    return operand;
}

Operand absoluteIndirectLong(CPU& cpu)
{
    const uint16_t pointer = cpu.fetch16();
    Operand operand;
    operand.hasAddress = true;
    operand.address = cpu.read24(pointer);
    return operand;
}

Operand absoluteIndirectX(CPU& cpu)
{
    // JMP/JSR (a,x): unlike plain (a), the pointer table is read from the
    // PROGRAM bank, not bank 0 — this mode exists for jump tables embedded
    // next to the code that uses them.
    auto& r = cpu.mutableRegisters();
    const uint16_t pointer = static_cast<uint16_t>(cpu.fetch16() + r.x);
    Operand operand;
    operand.hasAddress = true;
    operand.address = cpu.read16BankWrap(r.pb, pointer);
    return operand;
}

Operand absoluteLong(CPU& cpu)
{
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = cpu.fetch24();
    return operand;
}

Operand absoluteLongX(CPU& cpu)
{
    auto& r = cpu.mutableRegisters();
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = (cpu.fetch24() + r.x) & 0x00ffffff;
    return operand;
}

Operand relative8(CPU& cpu)
{
    Operand operand;
    operand.value = static_cast<uint16_t>(static_cast<int8_t>(cpu.fetch8()));
    return operand;
}

Operand relative16(CPU& cpu)
{
    Operand operand;
    operand.value = cpu.fetch16();
    return operand;
}

Operand stackRelative(CPU& cpu)
{
    // sr,S addresses S + offset in bank 0 — how code reaches arguments and
    // locals pushed on the stack without a frame pointer.
    auto& r = cpu.mutableRegisters();
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = static_cast<uint16_t>(r.s + cpu.fetch8());
    return operand;
}

Operand stackRelativeIndirectY(CPU& cpu)
{
    auto& r = cpu.mutableRegisters();
    const uint16_t pointer = static_cast<uint16_t>(r.s + cpu.fetch8());
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = bankedIndexed(r.db, cpu.read16BankWrap(0, pointer), r.y);
    return operand;
}

Operand blockMove(CPU& cpu)
{
    Operand operand;
    operand.value = cpu.fetch8();
    operand.extra = cpu.fetch8();
    operand.size = 2;
    return operand;
}

} // namespace snesquik::cpu_r5a22::addressing
