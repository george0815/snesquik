#include "core.h"

#include <cstdint>

namespace snesquik::cpu_r5a22 {

namespace operations {

namespace {

uint32_t banked(uint8_t bank, uint16_t address)
{
    return (static_cast<uint32_t>(bank) << 16) | address;
}

uint16_t maskFor(uint8_t width)
{
    return width == 1 ? 0x00ff : 0xffff;
}

uint16_t signFor(uint8_t width)
{
    return width == 1 ? 0x0080 : 0x8000;
}

uint8_t memoryWidth(CPU& cpu)
{
    return cpu.accumulatorWidth();
}

uint8_t indexWidth(CPU& cpu)
{
    return cpu.indexWidth();
}

void addWidthCycle(CPU& cpu, uint8_t width)
{
    if (width == 2) {
        cpu.addCycles(1);
    }
}

void addMemoryRmwWidthCycles(CPU& cpu, uint8_t width)
{
    if (width == 2) {
        cpu.addCycles(2);
    }
}

void compare(CPU& cpu, uint16_t lhs, uint16_t rhs, uint8_t width)
{
    const uint32_t mask = maskFor(width);
    const uint32_t result = (lhs - rhs) & mask;
    cpu.setFlag(Carry, (lhs & mask) >= (rhs & mask));
    cpu.setZN(result, width);
}

void logicResult(CPU& cpu, uint16_t value, uint8_t width)
{
    cpu.setRegA(value & maskFor(width));
    cpu.setZN(value, width);
}

uint16_t binaryAdc(CPU& cpu, uint16_t lhs, uint16_t rhs, uint8_t width)
{
    const uint32_t mask = maskFor(width);
    const uint32_t sign = signFor(width);
    const uint32_t carry = cpu.flag(Carry) ? 1 : 0;
    const uint32_t result = (lhs & mask) + (rhs & mask) + carry;
    cpu.setFlag(Carry, result > mask);
    cpu.setFlag(Overflow, (~(lhs ^ rhs) & (lhs ^ result) & sign) != 0);
    cpu.setZN(result, width);
    return static_cast<uint16_t>(result & mask);
}

uint16_t decimalAdc(CPU& cpu, uint16_t lhs, uint16_t rhs, uint8_t width)
{
    uint32_t carry = cpu.flag(Carry) ? 1 : 0;
    uint32_t result = 0;
    uint32_t carryOut = 0;
    const int nibbles = width == 1 ? 2 : 4;
    for (int i = 0; i < nibbles; ++i) {
        uint32_t digit = ((lhs >> (i * 4)) & 0x0f) + ((rhs >> (i * 4)) & 0x0f) + carry;
        if (digit > 9) {
            digit += 6;
        }
        carry = digit > 0x0f ? 1 : 0;
        result |= (digit & 0x0f) << (i * 4);
        carryOut = carry;
    }
    cpu.setFlag(Carry, carryOut != 0);
    cpu.setFlag(Overflow, (~(lhs ^ rhs) & (lhs ^ result) & signFor(width)) != 0);
    cpu.setZN(result, width);
    return static_cast<uint16_t>(result & maskFor(width));
}

uint16_t binarySbc(CPU& cpu, uint16_t lhs, uint16_t rhs, uint8_t width)
{
    const uint32_t mask = maskFor(width);
    const uint32_t sign = signFor(width);
    const uint32_t borrow = cpu.flag(Carry) ? 0 : 1;
    const uint32_t result = (lhs & mask) - (rhs & mask) - borrow;
    cpu.setFlag(Carry, (lhs & mask) >= ((rhs & mask) + borrow));
    cpu.setFlag(Overflow, ((lhs ^ rhs) & (lhs ^ result) & sign) != 0);
    cpu.setZN(result, width);
    return static_cast<uint16_t>(result & mask);
}

uint16_t decimalSbc(CPU& cpu, uint16_t lhs, uint16_t rhs, uint8_t width)
{
    int borrow = cpu.flag(Carry) ? 0 : 1;
    const uint32_t binaryResult = (lhs & maskFor(width)) - (rhs & maskFor(width)) - static_cast<uint32_t>(borrow);
    uint32_t result = 0;
    const int nibbles = width == 1 ? 2 : 4;
    for (int i = 0; i < nibbles; ++i) {
        int digit = static_cast<int>((lhs >> (i * 4)) & 0x0f) - static_cast<int>((rhs >> (i * 4)) & 0x0f) - borrow;
        if (digit < 0) {
            digit -= 6;
            borrow = 1;
        } else {
            borrow = 0;
        }
        result |= static_cast<uint32_t>(digit & 0x0f) << (i * 4);
    }
    cpu.setFlag(Carry, borrow == 0);
    cpu.setFlag(Overflow, ((lhs ^ rhs) & (lhs ^ binaryResult) & signFor(width)) != 0);
    cpu.setZN(result, width);
    return static_cast<uint16_t>(result & maskFor(width));
}

void loadRegister(CPU& cpu, const Operand& operand, uint8_t width, void (CPU::*setter)(uint16_t))
{
    addWidthCycle(cpu, width);
    const uint16_t value = cpu.loadOperand(operand, width);
    (cpu.*setter)(value);
    cpu.setZN(value, width);
}

void storeRegister(CPU& cpu, const Operand& operand, uint16_t value, uint8_t width)
{
    addWidthCycle(cpu, width);
    cpu.storeOperand(operand, value, width);
}

void shiftLeft(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    if (!operand.accumulator) {
        addMemoryRmwWidthCycles(cpu, width);
    }
    const uint16_t value = cpu.loadOperand(operand, width);
    const uint16_t result = static_cast<uint16_t>((value << 1) & maskFor(width));
    cpu.setFlag(Carry, (value & signFor(width)) != 0);
    cpu.storeOperand(operand, result, width);
    cpu.setZN(result, width);
}

void shiftRight(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    if (!operand.accumulator) {
        addMemoryRmwWidthCycles(cpu, width);
    }
    const uint16_t value = cpu.loadOperand(operand, width);
    const uint16_t result = static_cast<uint16_t>((value >> 1) & maskFor(width));
    cpu.setFlag(Carry, (value & 0x0001) != 0);
    cpu.storeOperand(operand, result, width);
    cpu.setZN(result, width);
}

} // namespace

void adc(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    addWidthCycle(cpu, width);
    const uint16_t lhs = cpu.regA();
    const uint16_t rhs = cpu.loadOperand(operand, width);
    cpu.setRegA(cpu.flag(Decimal) ? decimalAdc(cpu, lhs, rhs, width) : binaryAdc(cpu, lhs, rhs, width));
}

void and_(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    addWidthCycle(cpu, width);
    logicResult(cpu, static_cast<uint16_t>(cpu.regA() & cpu.loadOperand(operand, width)), width);
}

void asl(CPU& cpu, const Operand& operand) { shiftLeft(cpu, operand); }
void bcc(CPU& cpu, const Operand& operand) { cpu.branch(!cpu.flag(Carry), static_cast<int16_t>(operand.value), false); }
void bcs(CPU& cpu, const Operand& operand) { cpu.branch(cpu.flag(Carry), static_cast<int16_t>(operand.value), false); }
void beq(CPU& cpu, const Operand& operand) { cpu.branch(cpu.flag(Zero), static_cast<int16_t>(operand.value), false); }

void bit(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    addWidthCycle(cpu, width);
    const uint16_t value = cpu.loadOperand(operand, width);
    cpu.setFlag(Zero, (cpu.regA() & value & maskFor(width)) == 0);
    if (!operand.immediate) {
        cpu.setFlag(Negative, (value & signFor(width)) != 0);
        cpu.setFlag(Overflow, (value & (width == 1 ? 0x40 : 0x4000)) != 0);
    }
}

void bmi(CPU& cpu, const Operand& operand) { cpu.branch(cpu.flag(Negative), static_cast<int16_t>(operand.value), false); }
void bne(CPU& cpu, const Operand& operand) { cpu.branch(!cpu.flag(Zero), static_cast<int16_t>(operand.value), false); }
void bpl(CPU& cpu, const Operand& operand) { cpu.branch(!cpu.flag(Negative), static_cast<int16_t>(operand.value), false); }
void bra(CPU& cpu, const Operand& operand) { cpu.branch(true, static_cast<int16_t>(operand.value), false); }
void brk(CPU& cpu, const Operand&) { cpu.interrupt(Interrupt::BRK); }
void brl(CPU& cpu, const Operand& operand) { cpu.branch(true, static_cast<int16_t>(operand.value), true); }
void bvc(CPU& cpu, const Operand& operand) { cpu.branch(!cpu.flag(Overflow), static_cast<int16_t>(operand.value), false); }
void bvs(CPU& cpu, const Operand& operand) { cpu.branch(cpu.flag(Overflow), static_cast<int16_t>(operand.value), false); }
void clc(CPU& cpu, const Operand&) { cpu.setFlag(Carry, false); }
void cld(CPU& cpu, const Operand&) { cpu.setFlag(Decimal, false); }
void cli(CPU& cpu, const Operand&) { cpu.setFlag(InterruptDisable, false); }
void clv(CPU& cpu, const Operand&) { cpu.setFlag(Overflow, false); }

void cmp(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    addWidthCycle(cpu, width);
    compare(cpu, cpu.regA(), cpu.loadOperand(operand, width), width);
}

void cop(CPU& cpu, const Operand&) { cpu.interrupt(Interrupt::COP); }

void cpx(CPU& cpu, const Operand& operand)
{
    const uint8_t width = indexWidth(cpu);
    addWidthCycle(cpu, width);
    compare(cpu, cpu.registers().x, cpu.loadOperand(operand, width), width);
}

void cpy(CPU& cpu, const Operand& operand)
{
    const uint8_t width = indexWidth(cpu);
    addWidthCycle(cpu, width);
    compare(cpu, cpu.registers().y, cpu.loadOperand(operand, width), width);
}

void dec(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    if (!operand.accumulator) {
        addMemoryRmwWidthCycles(cpu, width);
    }
    const uint16_t value = static_cast<uint16_t>((cpu.loadOperand(operand, width) - 1) & maskFor(width));
    cpu.storeOperand(operand, value, width);
    cpu.setZN(value, width);
}

void dex(CPU& cpu, const Operand&)
{
    const uint8_t width = indexWidth(cpu);
    cpu.setRegX(static_cast<uint16_t>((cpu.registers().x - 1) & maskFor(width)));
    cpu.setZN(cpu.registers().x, width);
}

void dey(CPU& cpu, const Operand&)
{
    const uint8_t width = indexWidth(cpu);
    cpu.setRegY(static_cast<uint16_t>((cpu.registers().y - 1) & maskFor(width)));
    cpu.setZN(cpu.registers().y, width);
}

void eor(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    addWidthCycle(cpu, width);
    logicResult(cpu, static_cast<uint16_t>(cpu.regA() ^ cpu.loadOperand(operand, width)), width);
}

void inc(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    if (!operand.accumulator) {
        addMemoryRmwWidthCycles(cpu, width);
    }
    const uint16_t value = static_cast<uint16_t>((cpu.loadOperand(operand, width) + 1) & maskFor(width));
    cpu.storeOperand(operand, value, width);
    cpu.setZN(value, width);
}

void inx(CPU& cpu, const Operand&)
{
    const uint8_t width = indexWidth(cpu);
    cpu.setRegX(static_cast<uint16_t>((cpu.registers().x + 1) & maskFor(width)));
    cpu.setZN(cpu.registers().x, width);
}

void iny(CPU& cpu, const Operand&)
{
    const uint8_t width = indexWidth(cpu);
    cpu.setRegY(static_cast<uint16_t>((cpu.registers().y + 1) & maskFor(width)));
    cpu.setZN(cpu.registers().y, width);
}

void jmp(CPU& cpu, const Operand& operand)
{
    cpu.mutableRegisters().pc = static_cast<uint16_t>(operand.address);
}

void jml(CPU& cpu, const Operand& operand)
{
    cpu.mutableRegisters().pc = static_cast<uint16_t>(operand.address);
    cpu.mutableRegisters().pb = static_cast<uint8_t>(operand.address >> 16);
}

void jsr(CPU& cpu, const Operand& operand)
{
    cpu.push16(static_cast<uint16_t>(cpu.registers().pc - 1));
    cpu.mutableRegisters().pc = static_cast<uint16_t>(operand.address);
}

void jsl(CPU& cpu, const Operand& operand)
{
    cpu.push8(cpu.registers().pb);
    cpu.push16(static_cast<uint16_t>(cpu.registers().pc - 1));
    cpu.mutableRegisters().pc = static_cast<uint16_t>(operand.address);
    cpu.mutableRegisters().pb = static_cast<uint8_t>(operand.address >> 16);
}

void lda(CPU& cpu, const Operand& operand) { loadRegister(cpu, operand, memoryWidth(cpu), &CPU::setRegA); }
void ldx(CPU& cpu, const Operand& operand) { loadRegister(cpu, operand, indexWidth(cpu), &CPU::setRegX); }
void ldy(CPU& cpu, const Operand& operand) { loadRegister(cpu, operand, indexWidth(cpu), &CPU::setRegY); }
void lsr(CPU& cpu, const Operand& operand) { shiftRight(cpu, operand); }

void mvn(CPU& cpu, const Operand& operand)
{
    auto& r = cpu.mutableRegisters();
    r.db = static_cast<uint8_t>(operand.value);
    cpu.write8(banked(r.db, r.y), cpu.read8(banked(static_cast<uint8_t>(operand.extra), r.x)));
    r.x = static_cast<uint16_t>(r.x + 1);
    r.y = static_cast<uint16_t>(r.y + 1);
    r.a = static_cast<uint16_t>(r.a - 1);
    if (r.a != 0xffff) {
        r.pc = static_cast<uint16_t>(r.pc - 3);
    }
}

void mvp(CPU& cpu, const Operand& operand)
{
    auto& r = cpu.mutableRegisters();
    r.db = static_cast<uint8_t>(operand.value);
    cpu.write8(banked(r.db, r.y), cpu.read8(banked(static_cast<uint8_t>(operand.extra), r.x)));
    r.x = static_cast<uint16_t>(r.x - 1);
    r.y = static_cast<uint16_t>(r.y - 1);
    r.a = static_cast<uint16_t>(r.a - 1);
    if (r.a != 0xffff) {
        r.pc = static_cast<uint16_t>(r.pc - 3);
    }
}

void nop(CPU&, const Operand&) {}

void ora(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    addWidthCycle(cpu, width);
    logicResult(cpu, static_cast<uint16_t>(cpu.regA() | cpu.loadOperand(operand, width)), width);
}

void pea(CPU& cpu, const Operand& operand) { cpu.push16(static_cast<uint16_t>(operand.address)); }
void pei(CPU& cpu, const Operand& operand) { cpu.push16(cpu.read16(operand.address)); }
void per(CPU& cpu, const Operand& operand) { cpu.push16(static_cast<uint16_t>(cpu.registers().pc + static_cast<int16_t>(operand.value))); }

void pha(CPU& cpu, const Operand&)
{
    if (memoryWidth(cpu) == 1) {
        cpu.push8(static_cast<uint8_t>(cpu.regA()));
    } else {
        cpu.push16(cpu.regA());
        cpu.addCycles(1);
    }
}

void phb(CPU& cpu, const Operand&) { cpu.push8(cpu.registers().db); }
void phd(CPU& cpu, const Operand&) { cpu.push16(cpu.registers().d); }
void phk(CPU& cpu, const Operand&) { cpu.push8(cpu.registers().pb); }

void php(CPU& cpu, const Operand&)
{
    uint8_t p = cpu.registers().p;
    if (cpu.registers().emulation) {
        p |= 0x30;
    }
    cpu.push8(p);
}

void phx(CPU& cpu, const Operand&)
{
    if (indexWidth(cpu) == 1) {
        cpu.push8(static_cast<uint8_t>(cpu.registers().x));
    } else {
        cpu.push16(cpu.registers().x);
        cpu.addCycles(1);
    }
}

void phy(CPU& cpu, const Operand&)
{
    if (indexWidth(cpu) == 1) {
        cpu.push8(static_cast<uint8_t>(cpu.registers().y));
    } else {
        cpu.push16(cpu.registers().y);
        cpu.addCycles(1);
    }
}

void pla(CPU& cpu, const Operand&)
{
    const uint8_t width = memoryWidth(cpu);
    uint16_t value = width == 1 ? cpu.pull8() : cpu.pull16();
    addWidthCycle(cpu, width);
    cpu.setRegA(value);
    cpu.setZN(value, width);
}

void plb(CPU& cpu, const Operand&)
{
    cpu.mutableRegisters().db = cpu.pull8();
    cpu.setZN(cpu.registers().db, 1);
}

void pld(CPU& cpu, const Operand&)
{
    cpu.mutableRegisters().d = cpu.pull16();
    cpu.setZN(cpu.registers().d, 2);
}

void plp(CPU& cpu, const Operand&)
{
    cpu.mutableRegisters().p = cpu.pull8();
    cpu.normalizeEmulationRegisters();
}

void plx(CPU& cpu, const Operand&)
{
    const uint8_t width = indexWidth(cpu);
    uint16_t value = width == 1 ? cpu.pull8() : cpu.pull16();
    addWidthCycle(cpu, width);
    cpu.setRegX(value);
    cpu.setZN(value, width);
}

void ply(CPU& cpu, const Operand&)
{
    const uint8_t width = indexWidth(cpu);
    uint16_t value = width == 1 ? cpu.pull8() : cpu.pull16();
    addWidthCycle(cpu, width);
    cpu.setRegY(value);
    cpu.setZN(value, width);
}

void rep(CPU& cpu, const Operand& operand)
{
    cpu.mutableRegisters().p &= static_cast<uint8_t>(~operand.value);
    cpu.normalizeEmulationRegisters();
}

void rol(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    if (!operand.accumulator) {
        addMemoryRmwWidthCycles(cpu, width);
    }
    const uint16_t value = cpu.loadOperand(operand, width);
    const uint16_t result = static_cast<uint16_t>(((value << 1) | (cpu.flag(Carry) ? 1 : 0)) & maskFor(width));
    cpu.setFlag(Carry, (value & signFor(width)) != 0);
    cpu.storeOperand(operand, result, width);
    cpu.setZN(result, width);
}

void ror(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    if (!operand.accumulator) {
        addMemoryRmwWidthCycles(cpu, width);
    }
    const uint16_t value = cpu.loadOperand(operand, width);
    const uint16_t result = static_cast<uint16_t>((value >> 1) | (cpu.flag(Carry) ? signFor(width) : 0));
    cpu.setFlag(Carry, (value & 0x0001) != 0);
    cpu.storeOperand(operand, result, width);
    cpu.setZN(result, width);
}

void rti(CPU& cpu, const Operand&)
{
    cpu.mutableRegisters().p = cpu.pull8();
    cpu.mutableRegisters().pc = cpu.pull16();
    if (!cpu.registers().emulation) {
        cpu.mutableRegisters().pb = cpu.pull8();
        cpu.addCycles(1);
    }
    cpu.normalizeEmulationRegisters();
}

void rtl(CPU& cpu, const Operand&)
{
    cpu.mutableRegisters().pc = static_cast<uint16_t>(cpu.pull16() + 1);
    cpu.mutableRegisters().pb = cpu.pull8();
}

void rts(CPU& cpu, const Operand&)
{
    cpu.mutableRegisters().pc = static_cast<uint16_t>(cpu.pull16() + 1);
}

void sbc(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    addWidthCycle(cpu, width);
    const uint16_t lhs = cpu.regA();
    const uint16_t rhs = cpu.loadOperand(operand, width);
    cpu.setRegA(cpu.flag(Decimal) ? decimalSbc(cpu, lhs, rhs, width) : binarySbc(cpu, lhs, rhs, width));
}

void sec(CPU& cpu, const Operand&) { cpu.setFlag(Carry, true); }
void sed(CPU& cpu, const Operand&) { cpu.setFlag(Decimal, true); }
void sei(CPU& cpu, const Operand&) { cpu.setFlag(InterruptDisable, true); }

void sep(CPU& cpu, const Operand& operand)
{
    cpu.mutableRegisters().p |= static_cast<uint8_t>(operand.value);
    cpu.normalizeEmulationRegisters();
}

void sta(CPU& cpu, const Operand& operand) { storeRegister(cpu, operand, cpu.regA(), memoryWidth(cpu)); }
void stp(CPU& cpu, const Operand&) { cpu.stop(); }
void stx(CPU& cpu, const Operand& operand) { storeRegister(cpu, operand, cpu.registers().x, indexWidth(cpu)); }
void sty(CPU& cpu, const Operand& operand) { storeRegister(cpu, operand, cpu.registers().y, indexWidth(cpu)); }
void stz(CPU& cpu, const Operand& operand) { storeRegister(cpu, operand, 0, memoryWidth(cpu)); }

void tax(CPU& cpu, const Operand&)
{
    const uint8_t width = indexWidth(cpu);
    cpu.setRegX(cpu.registers().a);
    cpu.setZN(cpu.registers().x, width);
}

void tay(CPU& cpu, const Operand&)
{
    const uint8_t width = indexWidth(cpu);
    cpu.setRegY(cpu.registers().a);
    cpu.setZN(cpu.registers().y, width);
}

void tcd(CPU& cpu, const Operand&)
{
    cpu.mutableRegisters().d = cpu.registers().a;
    cpu.setZN(cpu.registers().d, 2);
}

void tcs(CPU& cpu, const Operand&)
{
    cpu.mutableRegisters().s = cpu.registers().a;
    cpu.normalizeEmulationRegisters();
}

void tdc(CPU& cpu, const Operand&)
{
    cpu.mutableRegisters().a = cpu.registers().d;
    cpu.setZN(cpu.registers().a, 2);
}

void trb(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    addMemoryRmwWidthCycles(cpu, width);
    const uint16_t value = cpu.loadOperand(operand, width);
    cpu.setFlag(Zero, (value & cpu.regA() & maskFor(width)) == 0);
    cpu.storeOperand(operand, static_cast<uint16_t>(value & ~cpu.regA()), width);
}

void tsb(CPU& cpu, const Operand& operand)
{
    const uint8_t width = memoryWidth(cpu);
    addMemoryRmwWidthCycles(cpu, width);
    const uint16_t value = cpu.loadOperand(operand, width);
    cpu.setFlag(Zero, (value & cpu.regA() & maskFor(width)) == 0);
    cpu.storeOperand(operand, static_cast<uint16_t>(value | cpu.regA()), width);
}

void tsc(CPU& cpu, const Operand&)
{
    cpu.mutableRegisters().a = cpu.registers().s;
    cpu.setZN(cpu.registers().a, 2);
}

void tsx(CPU& cpu, const Operand&)
{
    const uint8_t width = indexWidth(cpu);
    cpu.setRegX(cpu.registers().s);
    cpu.setZN(cpu.registers().x, width);
}

void txa(CPU& cpu, const Operand&)
{
    const uint8_t width = memoryWidth(cpu);
    cpu.setRegA(cpu.registers().x);
    cpu.setZN(cpu.regA(), width);
}

void txs(CPU& cpu, const Operand&)
{
    cpu.mutableRegisters().s = cpu.registers().x;
    cpu.normalizeEmulationRegisters();
}

void txy(CPU& cpu, const Operand&)
{
    const uint8_t width = indexWidth(cpu);
    cpu.setRegY(cpu.registers().x);
    cpu.setZN(cpu.registers().y, width);
}

void tya(CPU& cpu, const Operand&)
{
    const uint8_t width = memoryWidth(cpu);
    cpu.setRegA(cpu.registers().y);
    cpu.setZN(cpu.regA(), width);
}

void tyx(CPU& cpu, const Operand&)
{
    const uint8_t width = indexWidth(cpu);
    cpu.setRegX(cpu.registers().y);
    cpu.setZN(cpu.registers().x, width);
}

void wai(CPU& cpu, const Operand&) { cpu.waitForInterrupt(); }
void wdm(CPU&, const Operand&) {}

void xba(CPU& cpu, const Operand&)
{
    auto& r = cpu.mutableRegisters();
    r.a = static_cast<uint16_t>((r.a << 8) | (r.a >> 8));
    cpu.setZN(static_cast<uint8_t>(r.a), 1);
}

void xce(CPU& cpu, const Operand&)
{
    const bool oldCarry = cpu.flag(Carry);
    const bool oldEmulation = cpu.registers().emulation;
    cpu.setFlag(Carry, oldEmulation);
    cpu.setNativeMode(oldCarry == false);
}

} // namespace operations

namespace {

using AM = AddressingMode;
using OP = Operation;

#define INST(name, op, mode, cycles, bytes) \
    Instruction{name, OP::op, AM::mode, operations::op, addressing::mode, cycles, bytes}
#define INST_AND(name, mode, cycles, bytes) \
    Instruction{name, OP::and_, AM::mode, operations::and_, addressing::mode, cycles, bytes}

} // namespace

const std::array<Instruction, 256>& opcodeTable()
{
    static const std::array<Instruction, 256> table = {{
        INST("BRK", brk, immediate8, 7, 2), INST("ORA", ora, directIndirectX, 6, 2), INST("COP", cop, immediate8, 7, 2), INST("ORA", ora, stackRelative, 4, 2),
        INST("TSB", tsb, direct, 5, 2), INST("ORA", ora, direct, 3, 2), INST("ASL", asl, direct, 5, 2), INST("ORA", ora, directIndirectLong, 6, 2),
        INST("PHP", php, implied, 3, 1), INST("ORA", ora, immediateM, 2, 2), INST("ASL", asl, accumulator, 2, 1), INST("PHD", phd, implied, 4, 1),
        INST("TSB", tsb, absolute, 6, 3), INST("ORA", ora, absolute, 4, 3), INST("ASL", asl, absolute, 6, 3), INST("ORA", ora, absoluteLong, 5, 4),
        INST("BPL", bpl, relative8, 2, 2), INST("ORA", ora, directIndirectY, 5, 2), INST("ORA", ora, directIndirect, 5, 2), INST("ORA", ora, stackRelativeIndirectY, 7, 2),
        INST("TRB", trb, direct, 5, 2), INST("ORA", ora, directX, 4, 2), INST("ASL", asl, directX, 6, 2), INST("ORA", ora, directIndirectLongY, 6, 2),
        INST("CLC", clc, implied, 2, 1), INST("ORA", ora, absoluteY, 4, 3), INST("INC", inc, accumulator, 2, 1), INST("TCS", tcs, implied, 2, 1),
        INST("TRB", trb, absolute, 6, 3), INST("ORA", ora, absoluteX, 4, 3), INST("ASL", asl, absoluteX, 7, 3), INST("ORA", ora, absoluteLongX, 5, 4),
        INST("JSR", jsr, absolute, 6, 3), INST_AND("AND", directIndirectX, 6, 2), INST("JSL", jsl, absoluteLong, 8, 4), INST_AND("AND", stackRelative, 4, 2),
        INST("BIT", bit, direct, 3, 2), INST_AND("AND", direct, 3, 2), INST("ROL", rol, direct, 5, 2), INST_AND("AND", directIndirectLong, 6, 2),
        INST("PLP", plp, implied, 4, 1), INST_AND("AND", immediateM, 2, 2), INST("ROL", rol, accumulator, 2, 1), INST("PLD", pld, implied, 5, 1),
        INST("BIT", bit, absolute, 4, 3), INST_AND("AND", absolute, 4, 3), INST("ROL", rol, absolute, 6, 3), INST_AND("AND", absoluteLong, 5, 4),
        INST("BMI", bmi, relative8, 2, 2), INST_AND("AND", directIndirectY, 5, 2), INST_AND("AND", directIndirect, 5, 2), INST_AND("AND", stackRelativeIndirectY, 7, 2),
        INST("BIT", bit, directX, 4, 2), INST_AND("AND", directX, 4, 2), INST("ROL", rol, directX, 6, 2), INST_AND("AND", directIndirectLongY, 6, 2),
        INST("SEC", sec, implied, 2, 1), INST_AND("AND", absoluteY, 4, 3), INST("DEC", dec, accumulator, 2, 1), INST("TSC", tsc, implied, 2, 1),
        INST("BIT", bit, absoluteX, 4, 3), INST_AND("AND", absoluteX, 4, 3), INST("ROL", rol, absoluteX, 7, 3), INST_AND("AND", absoluteLongX, 5, 4),
        INST("RTI", rti, implied, 6, 1), INST("EOR", eor, directIndirectX, 6, 2), INST("WDM", wdm, immediate8, 2, 2), INST("EOR", eor, stackRelative, 4, 2),
        INST("MVP", mvp, blockMove, 7, 3), INST("EOR", eor, direct, 3, 2), INST("LSR", lsr, direct, 5, 2), INST("EOR", eor, directIndirectLong, 6, 2),
        INST("PHA", pha, implied, 3, 1), INST("EOR", eor, immediateM, 2, 2), INST("LSR", lsr, accumulator, 2, 1), INST("PHK", phk, implied, 3, 1),
        INST("JMP", jmp, absolute, 3, 3), INST("EOR", eor, absolute, 4, 3), INST("LSR", lsr, absolute, 6, 3), INST("EOR", eor, absoluteLong, 5, 4),
        INST("BVC", bvc, relative8, 2, 2), INST("EOR", eor, directIndirectY, 5, 2), INST("EOR", eor, directIndirect, 5, 2), INST("EOR", eor, stackRelativeIndirectY, 7, 2),
        INST("MVN", mvn, blockMove, 7, 3), INST("EOR", eor, directX, 4, 2), INST("LSR", lsr, directX, 6, 2), INST("EOR", eor, directIndirectLongY, 6, 2),
        INST("CLI", cli, implied, 2, 1), INST("EOR", eor, absoluteY, 4, 3), INST("PHY", phy, implied, 3, 1), INST("TCD", tcd, implied, 2, 1),
        INST("JML", jml, absoluteLong, 4, 4), INST("EOR", eor, absoluteX, 4, 3), INST("LSR", lsr, absoluteX, 7, 3), INST("EOR", eor, absoluteLongX, 5, 4),
        INST("RTS", rts, implied, 6, 1), INST("ADC", adc, directIndirectX, 6, 2), INST("PER", per, relative16, 6, 3), INST("ADC", adc, stackRelative, 4, 2),
        INST("STZ", stz, direct, 3, 2), INST("ADC", adc, direct, 3, 2), INST("ROR", ror, direct, 5, 2), INST("ADC", adc, directIndirectLong, 6, 2),
        INST("PLA", pla, implied, 4, 1), INST("ADC", adc, immediateM, 2, 2), INST("ROR", ror, accumulator, 2, 1), INST("RTL", rtl, implied, 6, 1),
        INST("JMP", jmp, absoluteIndirect, 5, 3), INST("ADC", adc, absolute, 4, 3), INST("ROR", ror, absolute, 6, 3), INST("ADC", adc, absoluteLong, 5, 4),
        INST("BVS", bvs, relative8, 2, 2), INST("ADC", adc, directIndirectY, 5, 2), INST("ADC", adc, directIndirect, 5, 2), INST("ADC", adc, stackRelativeIndirectY, 7, 2),
        INST("STZ", stz, directX, 4, 2), INST("ADC", adc, directX, 4, 2), INST("ROR", ror, directX, 6, 2), INST("ADC", adc, directIndirectLongY, 6, 2),
        INST("SEI", sei, implied, 2, 1), INST("ADC", adc, absoluteY, 4, 3), INST("PLY", ply, implied, 4, 1), INST("TDC", tdc, implied, 2, 1),
        INST("JMP", jmp, absoluteIndirectX, 6, 3), INST("ADC", adc, absoluteX, 4, 3), INST("ROR", ror, absoluteX, 7, 3), INST("ADC", adc, absoluteLongX, 5, 4),
        INST("BRA", bra, relative8, 2, 2), INST("STA", sta, directIndirectX, 6, 2), INST("BRL", brl, relative16, 4, 3), INST("STA", sta, stackRelative, 4, 2),
        INST("STY", sty, direct, 3, 2), INST("STA", sta, direct, 3, 2), INST("STX", stx, direct, 3, 2), INST("STA", sta, directIndirectLong, 6, 2),
        INST("DEY", dey, implied, 2, 1), INST("BIT", bit, immediateM, 2, 2), INST("TXA", txa, implied, 2, 1), INST("PHB", phb, implied, 3, 1),
        INST("STY", sty, absolute, 4, 3), INST("STA", sta, absolute, 4, 3), INST("STX", stx, absolute, 4, 3), INST("STA", sta, absoluteLong, 5, 4),
        INST("BCC", bcc, relative8, 2, 2), INST("STA", sta, directIndirectY, 6, 2), INST("STA", sta, directIndirect, 5, 2), INST("STA", sta, stackRelativeIndirectY, 7, 2),
        INST("STY", sty, directX, 4, 2), INST("STA", sta, directX, 4, 2), INST("STX", stx, directY, 4, 2), INST("STA", sta, directIndirectLongY, 6, 2),
        INST("TYA", tya, implied, 2, 1), INST("STA", sta, absoluteY, 5, 3), INST("TXS", txs, implied, 2, 1), INST("TXY", txy, implied, 2, 1),
        INST("STZ", stz, absolute, 4, 3), INST("STA", sta, absoluteX, 5, 3), INST("STZ", stz, absoluteX, 5, 3), INST("STA", sta, absoluteLongX, 5, 4),
        INST("LDY", ldy, immediateX, 2, 2), INST("LDA", lda, directIndirectX, 6, 2), INST("LDX", ldx, immediateX, 2, 2), INST("LDA", lda, stackRelative, 4, 2),
        INST("LDY", ldy, direct, 3, 2), INST("LDA", lda, direct, 3, 2), INST("LDX", ldx, direct, 3, 2), INST("LDA", lda, directIndirectLong, 6, 2),
        INST("TAY", tay, implied, 2, 1), INST("LDA", lda, immediateM, 2, 2), INST("TAX", tax, implied, 2, 1), INST("PLB", plb, implied, 4, 1),
        INST("LDY", ldy, absolute, 4, 3), INST("LDA", lda, absolute, 4, 3), INST("LDX", ldx, absolute, 4, 3), INST("LDA", lda, absoluteLong, 5, 4),
        INST("BCS", bcs, relative8, 2, 2), INST("LDA", lda, directIndirectY, 5, 2), INST("LDA", lda, directIndirect, 5, 2), INST("LDA", lda, stackRelativeIndirectY, 7, 2),
        INST("LDY", ldy, directX, 4, 2), INST("LDA", lda, directX, 4, 2), INST("LDX", ldx, directY, 4, 2), INST("LDA", lda, directIndirectLongY, 6, 2),
        INST("CLV", clv, implied, 2, 1), INST("LDA", lda, absoluteY, 4, 3), INST("TSX", tsx, implied, 2, 1), INST("TYX", tyx, implied, 2, 1),
        INST("LDY", ldy, absoluteX, 4, 3), INST("LDA", lda, absoluteX, 4, 3), INST("LDX", ldx, absoluteY, 4, 3), INST("LDA", lda, absoluteLongX, 5, 4),
        INST("CPY", cpy, immediateX, 2, 2), INST("CMP", cmp, directIndirectX, 6, 2), INST("REP", rep, immediate8, 3, 2), INST("CMP", cmp, stackRelative, 4, 2),
        INST("CPY", cpy, direct, 3, 2), INST("CMP", cmp, direct, 3, 2), INST("DEC", dec, direct, 5, 2), INST("CMP", cmp, directIndirectLong, 6, 2),
        INST("INY", iny, implied, 2, 1), INST("CMP", cmp, immediateM, 2, 2), INST("DEX", dex, implied, 2, 1), INST("WAI", wai, implied, 3, 1),
        INST("CPY", cpy, absolute, 4, 3), INST("CMP", cmp, absolute, 4, 3), INST("DEC", dec, absolute, 6, 3), INST("CMP", cmp, absoluteLong, 5, 4),
        INST("BNE", bne, relative8, 2, 2), INST("CMP", cmp, directIndirectY, 5, 2), INST("CMP", cmp, directIndirect, 5, 2), INST("CMP", cmp, stackRelativeIndirectY, 7, 2),
        INST("PEI", pei, direct, 6, 2), INST("CMP", cmp, directX, 4, 2), INST("DEC", dec, directX, 6, 2), INST("CMP", cmp, directIndirectLongY, 6, 2),
        INST("CLD", cld, implied, 2, 1), INST("CMP", cmp, absoluteY, 4, 3), INST("PHX", phx, implied, 3, 1), INST("STP", stp, implied, 3, 1),
        INST("JML", jml, absoluteIndirectLong, 6, 3), INST("CMP", cmp, absoluteX, 4, 3), INST("DEC", dec, absoluteX, 7, 3), INST("CMP", cmp, absoluteLongX, 5, 4),
        INST("CPX", cpx, immediateX, 2, 2), INST("SBC", sbc, directIndirectX, 6, 2), INST("SEP", sep, immediate8, 3, 2), INST("SBC", sbc, stackRelative, 4, 2),
        INST("CPX", cpx, direct, 3, 2), INST("SBC", sbc, direct, 3, 2), INST("INC", inc, direct, 5, 2), INST("SBC", sbc, directIndirectLong, 6, 2),
        INST("INX", inx, implied, 2, 1), INST("SBC", sbc, immediateM, 2, 2), INST("NOP", nop, implied, 2, 1), INST("XBA", xba, implied, 3, 1),
        INST("CPX", cpx, absolute, 4, 3), INST("SBC", sbc, absolute, 4, 3), INST("INC", inc, absolute, 6, 3), INST("SBC", sbc, absoluteLong, 5, 4),
        INST("BEQ", beq, relative8, 2, 2), INST("SBC", sbc, directIndirectY, 5, 2), INST("SBC", sbc, directIndirect, 5, 2), INST("SBC", sbc, stackRelativeIndirectY, 7, 2),
        INST("PEA", pea, absolute, 5, 3), INST("SBC", sbc, directX, 4, 2), INST("INC", inc, directX, 6, 2), INST("SBC", sbc, directIndirectLongY, 6, 2),
        INST("SED", sed, implied, 2, 1), INST("SBC", sbc, absoluteY, 4, 3), INST("PLX", plx, implied, 4, 1), INST("XCE", xce, implied, 2, 1),
        INST("JSR", jsr, absoluteIndirectX, 8, 3), INST("SBC", sbc, absoluteX, 4, 3), INST("INC", inc, absoluteX, 7, 3), INST("SBC", sbc, absoluteLongX, 5, 4),
    }};
    return table;
}

#undef INST_AND
#undef INST

} // namespace snesquik::cpu_r5a22
