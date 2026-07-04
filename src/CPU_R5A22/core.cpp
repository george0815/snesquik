// 65816 core (the CPU inside the Ricoh 5A22). This file owns the register
// file, memory/stack access helpers, interrupt entry, and the fetch/decode/
// execute step; instruction semantics live in operations.cpp and the
// addressing-mode resolvers in addressing_modes.cpp. The 5A22's system glue
// (DMA, H/V timers, mul/div, joypad auto-read) is NOT here — it lives in
// SnesBus, matching the physical split between the 65816 core and the rest
// of the 5A22 package.
#include "core.h"

#include <cassert>

namespace snesquik::cpu_r5a22 {

namespace {

constexpr uint32_t mask24(uint32_t address)
{
    return address & 0x00ffffff;
}

constexpr uint32_t banked(uint8_t bank, uint16_t address)
{
    return (static_cast<uint32_t>(bank) << 16) | address;
}

// The 65816 has two interrupt vector sets in bank 0: native-mode vectors at
// $FFE4-$FFEE and emulation-mode (6502-style) vectors at $FFF4-$FFFE. Which
// set is used depends on the E flag at the moment the interrupt is taken —
// this is why games must switch to native mode before enabling NMIs if their
// handlers assume native-mode stack frames.
constexpr uint16_t nativeVector(Interrupt type)
{
    switch (type) {
    case Interrupt::COP: return 0xffe4;
    case Interrupt::BRK: return 0xffe6;
    case Interrupt::Abort: return 0xffe8;
    case Interrupt::NMI: return 0xffea;
    case Interrupt::IRQ: return 0xffee;
    }
    return 0xffee;
}

constexpr uint16_t emulationVector(Interrupt type)
{
    switch (type) {
    case Interrupt::COP: return 0xfff4;
    case Interrupt::Abort: return 0xfff8;
    case Interrupt::NMI: return 0xfffa;
    case Interrupt::BRK:
    case Interrupt::IRQ: return 0xfffe;
    }
    return 0xfffe;
}

} // namespace

CPU::CPU(Bus& bus)
    : bus(bus)
{
    instruction = &opcodeTable()[0xea];
}

void CPU::reset()
{
    // Hardware reset state: the CPU comes up in emulation mode with 8-bit
    // A/X/Y, interrupts masked, stack in page 1, D/DB/PB zeroed, and fetches
    // the program counter from the emulation-mode reset vector at $00:FFFC.
    r = {};
    r.s = 0x01ff;
    r.p = Memory8 | Index8 | InterruptDisable;
    r.emulation = true;
    r.pc = read16(0xfffc);
    r.pb = 0;
    r.db = 0;
    cycles = 0;
    irqLine = false;
    nmiPending = false;
    isStopped = false;
    isWaiting = false;
}

// Execute one instruction (or take one interrupt) and return the cycles it
// consumed. Interrupts are checked BETWEEN instructions only — this core is
// instruction-granular, so an interrupt asserted mid-instruction is taken
// after the current instruction retires, which matches how the real CPU
// samples its interrupt lines at instruction boundaries.
uint32_t CPU::step()
{
    const uint64_t before = cycles;

    // NMI is edge-triggered and unmaskable: it wins over IRQ, wakes both WAI
    // and STP (on hardware only a reset truly leaves STP, but NMI-wakes are
    // harmless and keep a stuck machine debuggable).
    if (nmiPending) {
        nmiPending = false;
        isWaiting = false;
        isStopped = false;
        interrupt(Interrupt::NMI);
        addCycles(r.emulation ? 7 : 8);
        return static_cast<uint32_t>(cycles - before);
    }

    // STP halts the processor until reset; burn time so the frame loop's
    // dot accounting keeps advancing and the PPU still produces frames.
    if (isStopped) {
        addCycles(1);
        return static_cast<uint32_t>(cycles - before);
    }

    // IRQ is level-sensitive and maskable by the I flag. The bus re-asserts
    // the line every step (H/V timer, GSU, SA-1), so a still-pending source
    // re-enters the handler as soon as the game clears I — exactly the
    // hardware behavior games rely on for chained raster IRQs.
    if (irqLine && !flag(InterruptDisable)) {
        isWaiting = false;
        interrupt(Interrupt::IRQ);
        addCycles(r.emulation ? 7 : 8);
        return static_cast<uint32_t>(cycles - before);
    }

    if (isWaiting) {
        // WAI resumes whenever the IRQ line is asserted (level-sensitive), not
        // only on a fresh rising edge. The case above (irqLine && !I) already
        // vectored; reaching here with irqLine high means interrupts are masked
        // (I set), so WAI resumes and execution continues past it without
        // vectoring. Otherwise keep waiting.
        if (irqLine) {
            isWaiting = false;
        } else {
            addCycles(1);
            return static_cast<uint32_t>(cycles - before);
        }
    }

    opcode = fetch8();
    instruction = &opcodeTable()[opcode];
    addCycles(instruction->baseCycles);
    Operand operand = instruction->addressing(*this);
    instruction->operation(*this, operand);
    normalizeEmulationRegisters();
    return static_cast<uint32_t>(cycles - before);
}

CPU::SaveState CPU::saveState() const
{
    SaveState state;
    state.registers = r;
    state.cycles = cycles;
    state.irqLine = irqLine;
    state.nmiPending = nmiPending;
    state.isStopped = isStopped;
    state.isWaiting = isWaiting;
    return state;
}

void CPU::loadState(const SaveState& state)
{
    r = state.registers;
    cycles = state.cycles;
    irqLine = state.irqLine;
    nmiPending = state.nmiPending;
    isStopped = state.isStopped;
    isWaiting = state.isWaiting;
}

void CPU::requestIRQ()
{
    irqLine = true;
    isWaiting = false;
}

void CPU::setIrqLine(bool level)
{
    // WAI resumes when the IRQ line is asserted even with interrupts
    // disabled (execution continues after the WAI without vectoring).
    if (level && !irqLine) {
        isWaiting = false;
    }
    irqLine = level;
}

void CPU::requestNMI()
{
    nmiPending = true;
    isWaiting = false;
}

void CPU::clearIRQ()
{
    irqLine = false;
}

void CPU::stop()
{
    isStopped = true;
}

void CPU::waitForInterrupt()
{
    isWaiting = true;
}

uint8_t CPU::read8(uint32_t address)
{
    return bus.read8(mask24(address));
}

uint16_t CPU::read16(uint32_t address)
{
    const uint16_t lo = read8(address);
    const uint16_t hi = read8(address + 1);
    return static_cast<uint16_t>(lo | (hi << 8));
}

// 16-bit read whose high byte wraps within the given bank ($xx:FFFF ->
// $xx:0000) instead of spilling into the next bank. The 65816 uses this for
// pointer fetches that are architecturally bank-0-bound: direct-page
// indirection, absolute-indirect jumps, stack-relative pointers, PEI.
uint16_t CPU::read16BankWrap(uint8_t bank, uint16_t address)
{
    const uint16_t lo = read8(banked(bank, address));
    const uint16_t hi = read8(banked(bank, static_cast<uint16_t>(address + 1)));
    return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t CPU::read24(uint32_t address)
{
    const uint32_t lo = read8(address);
    const uint32_t hi = read8(address + 1);
    const uint32_t bank = read8(address + 2);
    return lo | (hi << 8) | (bank << 16);
}

void CPU::write8(uint32_t address, uint8_t value)
{
    bus.write8(mask24(address), value);
}

void CPU::write16(uint32_t address, uint16_t value)
{
    write8(address, static_cast<uint8_t>(value));
    write8(address + 1, static_cast<uint8_t>(value >> 8));
}

uint8_t CPU::fetch8()
{
    const uint8_t value = read8(banked(r.pb, r.pc));
    ++r.pc;
    return value;
}

uint16_t CPU::fetch16()
{
    const uint16_t lo = fetch8();
    const uint16_t hi = fetch8();
    return static_cast<uint16_t>(lo | (hi << 8));
}

uint32_t CPU::fetch24()
{
    const uint32_t lo = fetch8();
    const uint32_t hi = fetch8();
    const uint32_t bank = fetch8();
    return lo | (hi << 8) | (bank << 16);
}

// The stack always lives in bank 0. In emulation mode it is additionally
// pinned to page 1 ($0100-$01FF) and the pointer's low byte wraps within
// that page, exactly like a 6502 — old instructions honor this; the "new"
// 65816 instructions use the NoWrap variants below instead.
void CPU::push8(uint8_t value)
{
    write8(r.emulation ? banked(0, static_cast<uint16_t>(0x0100 | (r.s & 0x00ff))) : r.s, value);
    r.s = r.emulation ? static_cast<uint16_t>(0x0100 | static_cast<uint8_t>(r.s - 1)) : static_cast<uint16_t>(r.s - 1);
}

void CPU::push16(uint16_t value)
{
    push8(static_cast<uint8_t>(value >> 8));
    push8(static_cast<uint8_t>(value));
}

uint8_t CPU::pull8()
{
    r.s = r.emulation ? static_cast<uint16_t>(0x0100 | static_cast<uint8_t>(r.s + 1)) : static_cast<uint16_t>(r.s + 1);
    return read8(r.emulation ? banked(0, static_cast<uint16_t>(0x0100 | (r.s & 0x00ff))) : r.s);
}

uint16_t CPU::pull16()
{
    const uint16_t lo = pull8();
    const uint16_t hi = pull8();
    return static_cast<uint16_t>(lo | (hi << 8));
}

void CPU::push8NoWrap(uint8_t value)
{
    write8(banked(0, r.s), value);
    r.s = static_cast<uint16_t>(r.s - 1);
}

void CPU::push16NoWrap(uint16_t value)
{
    push8NoWrap(static_cast<uint8_t>(value >> 8));
    push8NoWrap(static_cast<uint8_t>(value));
}

uint8_t CPU::pull8NoWrap()
{
    r.s = static_cast<uint16_t>(r.s + 1);
    return read8(banked(0, r.s));
}

uint16_t CPU::pull16NoWrap()
{
    const uint16_t lo = pull8NoWrap();
    const uint16_t hi = pull8NoWrap();
    return static_cast<uint16_t>(lo | (hi << 8));
}

bool CPU::flag(StatusFlag flag) const
{
    return (r.p & flag) != 0;
}

void CPU::setFlag(StatusFlag flag, bool value)
{
    if (value) {
        r.p |= flag;
    } else {
        r.p &= static_cast<uint8_t>(~flag);
    }
}

void CPU::setZN(uint32_t value, uint8_t width)
{
    const uint32_t mask = width == 1 ? 0xff : 0xffff;
    const uint32_t sign = width == 1 ? 0x80 : 0x8000;
    setFlag(Zero, (value & mask) == 0);
    setFlag(Negative, (value & sign) != 0);
}

void CPU::addCycles(uint32_t count)
{
    cycles += count;
}

void CPU::branch(bool condition, int16_t offset, bool longBranch)
{
    if (!condition) {
        return;
    }

    // A taken branch costs one extra cycle; short branches in emulation mode
    // cost one more when the destination crosses a page boundary (a 6502
    // artifact the 65816 preserves only with E set). BRL never pays it.
    const uint16_t oldPc = r.pc;
    r.pc = static_cast<uint16_t>(r.pc + offset);
    addCycles(1);
    if (!longBranch && r.emulation && ((oldPc & 0xff00) != (r.pc & 0xff00))) {
        addCycles(1);
    }
}

// Common interrupt/BRK/COP entry sequence. Native mode pushes PB:PC:P (4
// bytes); emulation mode pushes only PC:P (3 bytes, 6502-style) and the
// handler is entered with PB forced to 0 in both modes.
void CPU::interrupt(Interrupt type)
{
    const bool native = !r.emulation;
    if (native) {
        push8(r.pb);
    }
    push16(r.pc);

    uint8_t pushed = r.p;
    if (r.emulation) {
        // Emulation-mode P pushes: bit 5 is always 1; bit 4 is the B flag,
        // set only for BRK (clear for IRQ/NMI so handlers can tell them apart).
        pushed |= Memory8;
        if (type == Interrupt::BRK) {
            pushed |= 0x10;
        } else {
            pushed &= static_cast<uint8_t>(~0x10);
        }
    }
    push8(pushed);

    setFlag(Decimal, false);
    setFlag(InterruptDisable, true);
    r.pb = 0;
    r.pc = read16(r.emulation ? emulationVector(type) : nativeVector(type));
}

void CPU::setNativeMode(bool nativeMode)
{
    r.emulation = !nativeMode;
    if (r.emulation) {
        r.p |= Memory8 | Index8;
    }
    normalizeEmulationRegisters();
}

// Re-establish the architectural invariants after every instruction (and
// after P-modifying ops): emulation mode hard-wires M and X set and keeps S
// in page 1; whenever X is set the index-register high bytes read as zero.
// Centralizing this here means individual operations never have to reason
// about mode transitions — REP/SEP/PLP/XCE/TCS just mutate state and let
// this fix it up, the same way the silicon gates those bits.
void CPU::normalizeEmulationRegisters()
{
    if (r.emulation) {
        r.p |= Memory8 | Index8;
        r.s = static_cast<uint16_t>(0x0100 | (r.s & 0x00ff));
    }
    if (flag(Index8)) {
        r.x &= 0x00ff;
        r.y &= 0x00ff;
    }
}

uint8_t CPU::accumulatorWidth() const
{
    return flag(Memory8) ? 1 : 2;
}

uint8_t CPU::indexWidth() const
{
    return flag(Index8) ? 1 : 2;
}

uint16_t CPU::accumulatorMask() const
{
    return flag(Memory8) ? 0x00ff : 0xffff;
}

uint16_t CPU::indexMask() const
{
    return flag(Index8) ? 0x00ff : 0xffff;
}

uint16_t CPU::regA() const
{
    return flag(Memory8) ? static_cast<uint8_t>(r.a) : r.a;
}

// With M set, only the low byte of the accumulator participates; the high
// byte (the "hidden" B accumulator) is preserved across 8-bit operations so
// XBA and a later REP #$20 can recover it — a trick games use constantly.
void CPU::setRegA(uint16_t value)
{
    r.a = flag(Memory8) ? static_cast<uint16_t>((r.a & 0xff00) | (value & 0x00ff)) : value;
}

void CPU::setRegX(uint16_t value)
{
    r.x = value & indexMask();
}

void CPU::setRegY(uint16_t value)
{
    r.y = value & indexMask();
}

uint16_t CPU::loadOperand(const Operand& operand, uint8_t width)
{
    if (operand.immediate) {
        return operand.value;
    }
    if (operand.accumulator) {
        return regA();
    }
    assert(operand.hasAddress);
    return width == 1 ? read8(operand.address) : read16(operand.address);
}

void CPU::storeOperand(const Operand& operand, uint16_t value, uint8_t width)
{
    if (operand.accumulator) {
        setRegA(value);
        return;
    }
    assert(operand.hasAddress);
    if (width == 1) {
        write8(operand.address, static_cast<uint8_t>(value));
    } else {
        write16(operand.address, value);
    }
}

} // namespace snesquik::cpu_r5a22
