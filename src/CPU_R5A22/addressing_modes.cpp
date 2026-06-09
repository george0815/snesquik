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
    if (r.emulation && (r.d & 0x00ff) == 0) {
        return static_cast<uint16_t>((r.d & 0xff00) | static_cast<uint8_t>(offset + index));
    }
    return static_cast<uint16_t>(r.d + offset + index);
}

uint16_t readDirect16(CPU& cpu, uint16_t address)
{
    auto& r = cpu.mutableRegisters();
    if (r.emulation || (r.d & 0x00ff) == 0) {
        const uint16_t page = address & 0xff00;
        const uint16_t lo = cpu.read8(address);
        const uint16_t hi = cpu.read8(page | static_cast<uint8_t>(address + 1));
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    return cpu.read16(address);
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
    Operand operand;
    operand.hasAddress = true;
    operand.size = cpu.accumulatorWidth();
    operand.address = banked(r.db, readDirect16(cpu, ptr));
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
    operand.address = bankedIndexed(r.db, cpu.read16(pointer), r.y);
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
