#pragma once

#include <array>
#include <cstdint>

namespace snesquik::cpu_r5a22 {

class Bus {
public:
    virtual ~Bus() = default;
    virtual uint8_t read8(uint32_t address) = 0;
    virtual void write8(uint32_t address, uint8_t value) = 0;
};

class CPU;

struct Operand {
    uint32_t address = 0;
    uint16_t value = 0;
    uint16_t extra = 0;
    uint8_t size = 0;
    bool hasAddress = false;
    bool immediate = false;
    bool accumulator = false;
};

using AddressingFn = Operand (*)(CPU&);
using OperationFn = void (*)(CPU&, const Operand&);

enum class AddressingMode : uint8_t {
    implied,
    accumulator,
    immediateM,
    immediateX,
    immediate8,
    direct,
    directX,
    directY,
    directIndirect,
    directIndirectX,
    directIndirectY,
    directIndirectLong,
    directIndirectLongY,
    absolute,
    absoluteX,
    absoluteY,
    absoluteIndirect,
    absoluteIndirectLong,
    absoluteIndirectX,
    absoluteLong,
    absoluteLongX,
    relative8,
    relative16,
    stackRelative,
    stackRelativeIndirectY,
    blockMove,
};

enum class Operation : uint8_t {
    adc, and_, asl, bcc, bcs, beq, bit, bmi, bne, bpl, bra, brk, brl, bvc, bvs,
    clc, cld, cli, clv, cmp, cop, cpx, cpy, dec, dex, dey, eor, inc, inx, iny,
    jmp, jml, jsr, jsl, lda, ldx, ldy, lsr, mvn, mvp, nop, ora, pea, pei, per,
    pha, phb, phd, phk, php, phx, phy, pla, plb, pld, plp, plx, ply, rep, rol,
    ror, rti, rtl, rts, sbc, sec, sed, sei, sep, sta, stp, stx, sty, stz, tax,
    tay, tcd, tcs, tdc, trb, tsb, tsc, tsx, txa, txs, txy, tya, tyx, wai, wdm,
    xba, xce
};

struct Instruction {
    const char* mnemonic;
    Operation operationId;
    AddressingMode addressingId;
    OperationFn operation;
    AddressingFn addressing;
    uint8_t baseCycles;
    uint8_t baseBytes;
};

enum StatusFlag : uint8_t {
    Carry = 0x01,
    Zero = 0x02,
    InterruptDisable = 0x04,
    Decimal = 0x08,
    Index8 = 0x10,
    Memory8 = 0x20,
    Overflow = 0x40,
    Negative = 0x80,
};

enum class Interrupt : uint8_t {
    COP,
    BRK,
    IRQ,
    NMI,
    Abort,
};

struct Registers {
    uint16_t a = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t s = 0x01ff;
    uint16_t d = 0;
    uint16_t pc = 0;
    uint8_t db = 0;
    uint8_t pb = 0;
    uint8_t p = Memory8 | Index8 | InterruptDisable;
    bool emulation = true;
};

class CPU {
public:
    struct SaveState {
        Registers registers;
        uint64_t cycles = 0;
        bool irqLine = false;
        bool nmiPending = false;
        bool isStopped = false;
        bool isWaiting = false;
    };

    explicit CPU(Bus& bus);

    void reset();
    uint32_t step();
    SaveState saveState() const;
    void loadState(const SaveState& state);
    void requestIRQ();
    void requestNMI();
    void clearIRQ();
    void setIrqLine(bool level);
    void stop();
    void waitForInterrupt();

    const Registers& registers() const { return r; }
    Registers& mutableRegisters() { return r; }

    bool stopped() const { return isStopped; }
    bool waiting() const { return isWaiting; }
    uint64_t totalCycles() const { return cycles; }
    uint8_t lastOpcode() const { return opcode; }
    const Instruction& currentInstruction() const { return *instruction; }

    uint8_t read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint16_t read16BankWrap(uint8_t bank, uint16_t address);
    uint32_t read24(uint32_t address);
    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);

    uint8_t fetch8();
    uint16_t fetch16();
    uint32_t fetch24();

    void push8(uint8_t value);
    void push16(uint16_t value);
    uint8_t pull8();
    uint16_t pull16();
    // "New" 65816 instructions (JSL/RTL/PEA/PEI/PER/PHB/PHD/PLB/PLD and
    // JSR (a,x)) access the stack without page-1 wrapping even in emulation
    // mode; S is renormalized to page 1 after the instruction.
    void push8NoWrap(uint8_t value);
    void push16NoWrap(uint16_t value);
    uint8_t pull8NoWrap();
    uint16_t pull16NoWrap();

    bool flag(StatusFlag flag) const;
    void setFlag(StatusFlag flag, bool value);
    void setZN(uint32_t value, uint8_t width);
    void addCycles(uint32_t count);
    void branch(bool condition, int16_t offset, bool longBranch);
    void interrupt(Interrupt type);
    void setNativeMode(bool nativeMode);
    void normalizeEmulationRegisters();

    uint8_t accumulatorWidth() const;
    uint8_t indexWidth() const;
    uint16_t accumulatorMask() const;
    uint16_t indexMask() const;
    uint16_t regA() const;
    void setRegA(uint16_t value);
    void setRegX(uint16_t value);
    void setRegY(uint16_t value);

    uint16_t loadOperand(const Operand& operand, uint8_t width);
    void storeOperand(const Operand& operand, uint16_t value, uint8_t width);

private:
    Bus& bus;
    Registers r;
    uint64_t cycles = 0;
    bool irqLine = false;
    bool nmiPending = false;
    bool isStopped = false;
    bool isWaiting = false;
    uint8_t opcode = 0;
    const Instruction* instruction = nullptr;
};

const std::array<Instruction, 256>& opcodeTable();

namespace addressing {
Operand implied(CPU& cpu);
Operand accumulator(CPU& cpu);
Operand immediateM(CPU& cpu);
Operand immediateX(CPU& cpu);
Operand immediate8(CPU& cpu);
Operand direct(CPU& cpu);
Operand directX(CPU& cpu);
Operand directY(CPU& cpu);
Operand directIndirect(CPU& cpu);
Operand directIndirectX(CPU& cpu);
Operand directIndirectY(CPU& cpu);
Operand directIndirectLong(CPU& cpu);
Operand directIndirectLongY(CPU& cpu);
Operand absolute(CPU& cpu);
Operand absoluteX(CPU& cpu);
Operand absoluteY(CPU& cpu);
Operand absoluteIndirect(CPU& cpu);
Operand absoluteIndirectLong(CPU& cpu);
Operand absoluteIndirectX(CPU& cpu);
Operand absoluteLong(CPU& cpu);
Operand absoluteLongX(CPU& cpu);
Operand relative8(CPU& cpu);
Operand relative16(CPU& cpu);
Operand stackRelative(CPU& cpu);
Operand stackRelativeIndirectY(CPU& cpu);
Operand blockMove(CPU& cpu);
}

namespace operations {
void adc(CPU& cpu, const Operand& operand);
void and_(CPU& cpu, const Operand& operand);
void asl(CPU& cpu, const Operand& operand);
void bcc(CPU& cpu, const Operand& operand);
void bcs(CPU& cpu, const Operand& operand);
void beq(CPU& cpu, const Operand& operand);
void bit(CPU& cpu, const Operand& operand);
void bmi(CPU& cpu, const Operand& operand);
void bne(CPU& cpu, const Operand& operand);
void bpl(CPU& cpu, const Operand& operand);
void bra(CPU& cpu, const Operand& operand);
void brk(CPU& cpu, const Operand& operand);
void brl(CPU& cpu, const Operand& operand);
void bvc(CPU& cpu, const Operand& operand);
void bvs(CPU& cpu, const Operand& operand);
void clc(CPU& cpu, const Operand& operand);
void cld(CPU& cpu, const Operand& operand);
void cli(CPU& cpu, const Operand& operand);
void clv(CPU& cpu, const Operand& operand);
void cmp(CPU& cpu, const Operand& operand);
void cop(CPU& cpu, const Operand& operand);
void cpx(CPU& cpu, const Operand& operand);
void cpy(CPU& cpu, const Operand& operand);
void dec(CPU& cpu, const Operand& operand);
void dex(CPU& cpu, const Operand& operand);
void dey(CPU& cpu, const Operand& operand);
void eor(CPU& cpu, const Operand& operand);
void inc(CPU& cpu, const Operand& operand);
void inx(CPU& cpu, const Operand& operand);
void iny(CPU& cpu, const Operand& operand);
void jmp(CPU& cpu, const Operand& operand);
void jml(CPU& cpu, const Operand& operand);
void jsr(CPU& cpu, const Operand& operand);
void jsl(CPU& cpu, const Operand& operand);
void lda(CPU& cpu, const Operand& operand);
void ldx(CPU& cpu, const Operand& operand);
void ldy(CPU& cpu, const Operand& operand);
void lsr(CPU& cpu, const Operand& operand);
void mvn(CPU& cpu, const Operand& operand);
void mvp(CPU& cpu, const Operand& operand);
void nop(CPU& cpu, const Operand& operand);
void ora(CPU& cpu, const Operand& operand);
void pea(CPU& cpu, const Operand& operand);
void pei(CPU& cpu, const Operand& operand);
void per(CPU& cpu, const Operand& operand);
void pha(CPU& cpu, const Operand& operand);
void phb(CPU& cpu, const Operand& operand);
void phd(CPU& cpu, const Operand& operand);
void phk(CPU& cpu, const Operand& operand);
void php(CPU& cpu, const Operand& operand);
void phx(CPU& cpu, const Operand& operand);
void phy(CPU& cpu, const Operand& operand);
void pla(CPU& cpu, const Operand& operand);
void plb(CPU& cpu, const Operand& operand);
void pld(CPU& cpu, const Operand& operand);
void plp(CPU& cpu, const Operand& operand);
void plx(CPU& cpu, const Operand& operand);
void ply(CPU& cpu, const Operand& operand);
void rep(CPU& cpu, const Operand& operand);
void rol(CPU& cpu, const Operand& operand);
void ror(CPU& cpu, const Operand& operand);
void rti(CPU& cpu, const Operand& operand);
void rtl(CPU& cpu, const Operand& operand);
void rts(CPU& cpu, const Operand& operand);
void sbc(CPU& cpu, const Operand& operand);
void sec(CPU& cpu, const Operand& operand);
void sed(CPU& cpu, const Operand& operand);
void sei(CPU& cpu, const Operand& operand);
void sep(CPU& cpu, const Operand& operand);
void sta(CPU& cpu, const Operand& operand);
void stp(CPU& cpu, const Operand& operand);
void stx(CPU& cpu, const Operand& operand);
void sty(CPU& cpu, const Operand& operand);
void stz(CPU& cpu, const Operand& operand);
void tax(CPU& cpu, const Operand& operand);
void tay(CPU& cpu, const Operand& operand);
void tcd(CPU& cpu, const Operand& operand);
void tcs(CPU& cpu, const Operand& operand);
void tdc(CPU& cpu, const Operand& operand);
void trb(CPU& cpu, const Operand& operand);
void tsb(CPU& cpu, const Operand& operand);
void tsc(CPU& cpu, const Operand& operand);
void tsx(CPU& cpu, const Operand& operand);
void txa(CPU& cpu, const Operand& operand);
void txs(CPU& cpu, const Operand& operand);
void txy(CPU& cpu, const Operand& operand);
void tya(CPU& cpu, const Operand& operand);
void tyx(CPU& cpu, const Operand& operand);
void wai(CPU& cpu, const Operand& operand);
void wdm(CPU& cpu, const Operand& operand);
void xba(CPU& cpu, const Operand& operand);
void xce(CPU& cpu, const Operand& operand);
}

} // namespace snesquik::cpu_r5a22
