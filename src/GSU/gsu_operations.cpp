#include "GSU/gsu.h"

namespace snesquik::gsu::operations {

// $00 STOP
void stop(Gsu& gsu, uint8_t)
{
    if (!gsu.cfgrIrqMask) {
        gsu.setFlag(Gsu::flagIrq, true);
    }
    gsu.stopGsu();
    gsu.pipeline = 0x01; // nop
    gsu.regReset();
}

// $01 NOP
void nop(Gsu& gsu, uint8_t)
{
    gsu.regReset();
}

// $02 CACHE
void cacheOp(Gsu& gsu, uint8_t)
{
    if (gsu.cbr != (gsu.r[15] & 0xfff0)) {
        gsu.cbr = gsu.r[15] & 0xfff0;
        gsu.flushCache();
    }
    gsu.regReset();
}

// $03 LSR
void lsr(Gsu& gsu, uint8_t)
{
    gsu.setFlag(Gsu::flagCy, (gsu.srcValue() & 1) != 0);
    gsu.setDest(gsu.srcValue() >> 1);
    gsu.setZS(gsu.destValue());
    gsu.regReset();
}

// $04 ROL
void rol(Gsu& gsu, uint8_t)
{
    const bool carry = (gsu.srcValue() & 0x8000) != 0;
    gsu.setDest(static_cast<uint16_t>((gsu.srcValue() << 1) | ((gsu.sfr & Gsu::flagCy) ? 1 : 0)));
    gsu.setZS(gsu.destValue());
    gsu.setFlag(Gsu::flagCy, carry);
    gsu.regReset();
}

// $05-$0F branches (with delay slot via the pipeline)
void branch(Gsu& gsu, uint8_t opcode)
{
    const bool s = (gsu.sfr & Gsu::flagS) != 0;
    const bool ov = (gsu.sfr & Gsu::flagOv) != 0;
    const bool z = (gsu.sfr & Gsu::flagZ) != 0;
    const bool cy = (gsu.sfr & Gsu::flagCy) != 0;

    bool take = false;
    switch (opcode) {
    case 0x05: take = true; break;          // BRA
    case 0x06: take = (s ^ ov) == 0; break; // BLT (per hardware encoding)
    case 0x07: take = (s ^ ov) != 0; break; // BGE
    case 0x08: take = !z; break;            // BNE
    case 0x09: take = z; break;             // BEQ
    case 0x0a: take = !s; break;            // BPL
    case 0x0b: take = s; break;             // BMI
    case 0x0c: take = !cy; break;           // BCC
    case 0x0d: take = cy; break;            // BCS
    case 0x0e: take = !ov; break;           // BVC
    case 0x0f: take = ov; break;            // BVS
    default: break;
    }

    const auto displacement = static_cast<int8_t>(gsu.pipe());
    if (take) {
        gsu.setReg(15, static_cast<uint16_t>(gsu.r[15] + displacement));
    }
}

// $10-$1F TO Rn / MOVE Rn,Rs
void toMove(Gsu& gsu, uint8_t opcode)
{
    const int n = opcode & 15;
    if (!gsu.flagB_()) {
        gsu.dreg = n;
    } else {
        gsu.setReg(n, gsu.srcValue());
        gsu.regReset();
    }
}

// $20-$2F WITH Rn
void with(Gsu& gsu, uint8_t opcode)
{
    const int n = opcode & 15;
    gsu.sreg = n;
    gsu.dreg = n;
    gsu.setFlag(Gsu::flagB, true);
}

// $30-$3B STW (Rn) / alt1: STB (Rn)
void store(Gsu& gsu, uint8_t opcode)
{
    const int n = opcode & 15;
    gsu.ramaddr = gsu.r[n];
    gsu.writeRamBuffer(gsu.ramaddr, static_cast<uint8_t>(gsu.srcValue()));
    if (!gsu.alt1()) {
        gsu.writeRamBuffer(gsu.ramaddr ^ 1, static_cast<uint8_t>(gsu.srcValue() >> 8));
    }
    gsu.regReset();
}

// $3C LOOP
void loop(Gsu& gsu, uint8_t)
{
    gsu.setReg(12, static_cast<uint16_t>(gsu.r[12] - 1));
    gsu.setZS(gsu.r[12]);
    if ((gsu.sfr & Gsu::flagZ) == 0) {
        gsu.setReg(15, gsu.r[13]);
    }
    gsu.regReset();
}

// $3D ALT1
void alt1(Gsu& gsu, uint8_t)
{
    gsu.setFlag(Gsu::flagB, false);
    gsu.setFlag(Gsu::flagAlt1, true);
}

// $3E ALT2
void alt2(Gsu& gsu, uint8_t)
{
    gsu.setFlag(Gsu::flagB, false);
    gsu.setFlag(Gsu::flagAlt2, true);
}

// $3F ALT3
void alt3(Gsu& gsu, uint8_t)
{
    gsu.setFlag(Gsu::flagB, false);
    gsu.setFlag(Gsu::flagAlt1, true);
    gsu.setFlag(Gsu::flagAlt2, true);
}

// $40-$4B LDW (Rn) / alt1: LDB (Rn)
void load(Gsu& gsu, uint8_t opcode)
{
    const int n = opcode & 15;
    gsu.ramaddr = gsu.r[n];
    uint16_t value = gsu.readRamBuffer(gsu.ramaddr);
    if (!gsu.alt1()) {
        value |= static_cast<uint16_t>(gsu.readRamBuffer(gsu.ramaddr ^ 1) << 8);
    }
    gsu.setDest(value);
    gsu.regReset();
}

// $4C PLOT / alt1: RPIX
void plotRpix(Gsu& gsu, uint8_t)
{
    if (!gsu.alt1()) {
        gsu.plot(static_cast<uint8_t>(gsu.r[1]), static_cast<uint8_t>(gsu.r[2]));
        gsu.setReg(1, static_cast<uint16_t>(gsu.r[1] + 1));
    } else {
        gsu.setDest(gsu.rpix(static_cast<uint8_t>(gsu.r[1]), static_cast<uint8_t>(gsu.r[2])));
        gsu.setZS(gsu.destValue());
    }
    gsu.regReset();
}

// $4D SWAP
void swap(Gsu& gsu, uint8_t)
{
    gsu.setDest(static_cast<uint16_t>((gsu.srcValue() >> 8) | (gsu.srcValue() << 8)));
    gsu.setZS(gsu.destValue());
    gsu.regReset();
}

// $4E COLOR / alt1: CMODE
void colorCmode(Gsu& gsu, uint8_t)
{
    if (!gsu.alt1()) {
        gsu.colr = gsu.applyColorSource(static_cast<uint8_t>(gsu.srcValue()));
    } else {
        gsu.por = static_cast<uint8_t>(gsu.srcValue() & 0x1f);
    }
    gsu.regReset();
}

// $4F NOT
void notOp(Gsu& gsu, uint8_t)
{
    gsu.setDest(static_cast<uint16_t>(~gsu.srcValue()));
    gsu.setZS(gsu.destValue());
    gsu.regReset();
}

// $50-$5F ADD Rn / alt1: ADC Rn / alt2: ADD #n / alt3: ADC #n
void addAdc(Gsu& gsu, uint8_t opcode)
{
    uint32_t n = opcode & 15;
    if (!gsu.alt2()) {
        n = gsu.r[n];
    }
    const uint32_t carryIn = gsu.alt1() && (gsu.sfr & Gsu::flagCy) ? 1 : 0;
    const uint32_t result = gsu.srcValue() + n + carryIn;
    gsu.setFlag(Gsu::flagOv, (~(gsu.srcValue() ^ n) & (n ^ result) & 0x8000) != 0);
    gsu.setFlag(Gsu::flagCy, result >= 0x10000);
    gsu.setDest(static_cast<uint16_t>(result));
    gsu.setZS(gsu.destValue());
    gsu.regReset();
}

// $60-$6F SUB Rn / alt1: SBC Rn / alt2: SUB #n / alt3: CMP Rn
void subSbcCmp(Gsu& gsu, uint8_t opcode)
{
    uint32_t n = opcode & 15;
    if (!gsu.alt2() || gsu.alt1()) {
        n = gsu.r[n];
    }
    const int borrow = (!gsu.alt2() && gsu.alt1() && !(gsu.sfr & Gsu::flagCy)) ? 1 : 0;
    const int result = static_cast<int>(gsu.srcValue()) - static_cast<int>(n) - borrow;
    gsu.setFlag(Gsu::flagOv, ((gsu.srcValue() ^ n) & (gsu.srcValue() ^ result) & 0x8000) != 0);
    gsu.setFlag(Gsu::flagCy, result >= 0);
    gsu.setFlag(Gsu::flagZ, static_cast<uint16_t>(result) == 0);
    gsu.setFlag(Gsu::flagS, (result & 0x8000) != 0);
    if (!gsu.alt2() || !gsu.alt1()) {
        gsu.setDest(static_cast<uint16_t>(result));
    }
    gsu.regReset();
}

// $70 MERGE
void merge(Gsu& gsu, uint8_t)
{
    gsu.setDest(static_cast<uint16_t>((gsu.r[7] & 0xff00) | (gsu.r[8] >> 8)));
    gsu.setFlag(Gsu::flagOv, (gsu.destValue() & 0xc0c0) != 0);
    gsu.setFlag(Gsu::flagS, (gsu.destValue() & 0x8080) != 0);
    gsu.setFlag(Gsu::flagCy, (gsu.destValue() & 0xe0e0) != 0);
    gsu.setFlag(Gsu::flagZ, (gsu.destValue() & 0xf0f0) != 0);
    gsu.regReset();
}

// $71-$7F AND Rn / alt1: BIC Rn / alt2: AND #n / alt3: BIC #n
void andBic(Gsu& gsu, uint8_t opcode)
{
    uint16_t n = opcode & 15;
    if (!gsu.alt2()) {
        n = gsu.r[n];
    }
    gsu.setDest(gsu.srcValue() & (gsu.alt1() ? static_cast<uint16_t>(~n) : n));
    gsu.setZS(gsu.destValue());
    gsu.regReset();
}

// $80-$8F MULT Rn / alt1: UMULT Rn / alt2: MULT #n / alt3: UMULT #n
void multUmult(Gsu& gsu, uint8_t opcode)
{
    uint16_t n = opcode & 15;
    if (!gsu.alt2()) {
        n = gsu.r[n];
    }
    uint16_t result;
    if (!gsu.alt1()) {
        result = static_cast<uint16_t>(static_cast<int8_t>(gsu.srcValue()) * static_cast<int8_t>(n));
    } else {
        result = static_cast<uint16_t>(static_cast<uint8_t>(gsu.srcValue()) * static_cast<uint8_t>(n));
    }
    gsu.setDest(result);
    gsu.setZS(result);
    gsu.regReset();
    if (!gsu.cfgrMs0) {
        gsu.step(gsu.clsr ? 1 : 2);
    }
}

// $90 SBK
void sbk(Gsu& gsu, uint8_t)
{
    gsu.writeRamBuffer(gsu.ramaddr ^ 0, static_cast<uint8_t>(gsu.srcValue()));
    gsu.writeRamBuffer(gsu.ramaddr ^ 1, static_cast<uint8_t>(gsu.srcValue() >> 8));
    gsu.regReset();
}

// $91-$94 LINK #n
void link(Gsu& gsu, uint8_t opcode)
{
    gsu.setReg(11, static_cast<uint16_t>(gsu.r[15] + (opcode & 15)));
    gsu.regReset();
}

// $95 SEX
void sex(Gsu& gsu, uint8_t)
{
    gsu.setDest(static_cast<uint16_t>(static_cast<int8_t>(gsu.srcValue())));
    gsu.setZS(gsu.destValue());
    gsu.regReset();
}

// $96 ASR / alt1: DIV2
void asrDiv2(Gsu& gsu, uint8_t)
{
    gsu.setFlag(Gsu::flagCy, (gsu.srcValue() & 1) != 0);
    uint16_t result = static_cast<uint16_t>(static_cast<int16_t>(gsu.srcValue()) >> 1);
    if (gsu.alt1()) {
        // DIV2 rounds -1 to 0.
        result = static_cast<uint16_t>(result
            + ((static_cast<uint32_t>(gsu.srcValue()) + 1) >> 16));
    }
    gsu.setDest(result);
    gsu.setZS(result);
    gsu.regReset();
}

// $97 ROR
void ror(Gsu& gsu, uint8_t)
{
    const bool carry = (gsu.srcValue() & 1) != 0;
    gsu.setDest(static_cast<uint16_t>((((gsu.sfr & Gsu::flagCy) ? 1 : 0) << 15) | (gsu.srcValue() >> 1)));
    gsu.setZS(gsu.destValue());
    gsu.setFlag(Gsu::flagCy, carry);
    gsu.regReset();
}

// $98-$9D JMP Rn / alt1: LJMP Rn
void jmpLjmp(Gsu& gsu, uint8_t opcode)
{
    const int n = opcode & 15;
    if (!gsu.alt1()) {
        gsu.setReg(15, gsu.r[n]);
    } else {
        gsu.pbr = static_cast<uint8_t>(gsu.r[n] & 0x7f);
        gsu.setReg(15, gsu.srcValue());
        gsu.cbr = gsu.r[15] & 0xfff0;
        gsu.flushCache();
    }
    gsu.regReset();
}

// $9E LOB
void lob(Gsu& gsu, uint8_t)
{
    gsu.setDest(gsu.srcValue() & 0xff);
    gsu.setFlag(Gsu::flagS, (gsu.destValue() & 0x80) != 0);
    gsu.setFlag(Gsu::flagZ, gsu.destValue() == 0);
    gsu.regReset();
}

// $9F FMULT / alt1: LMULT
void fmultLmult(Gsu& gsu, uint8_t)
{
    const uint32_t result = static_cast<uint32_t>(
        static_cast<int16_t>(gsu.srcValue()) * static_cast<int16_t>(gsu.r[6]));
    if (gsu.alt1()) {
        gsu.setReg(4, static_cast<uint16_t>(result));
    }
    gsu.setDest(static_cast<uint16_t>(result >> 16));
    gsu.setZS(gsu.destValue());
    gsu.setFlag(Gsu::flagCy, (result & 0x8000) != 0);
    gsu.regReset();
    gsu.step(static_cast<uint32_t>((gsu.cfgrMs0 ? 3 : 7) * (gsu.clsr ? 1 : 2)));
}

// $A0-$AF IBT Rn,#pp / alt1: LMS Rn,(yy) / alt2: SMS (yy),Rn
void ibtLmsSms(Gsu& gsu, uint8_t opcode)
{
    const int n = opcode & 15;
    if (gsu.alt1()) {
        gsu.ramaddr = static_cast<uint16_t>(gsu.pipe() << 1);
        const uint8_t lo = gsu.readRamBuffer(gsu.ramaddr ^ 0);
        gsu.setReg(n, static_cast<uint16_t>((gsu.readRamBuffer(gsu.ramaddr ^ 1) << 8) | lo));
    } else if (gsu.alt2()) {
        gsu.ramaddr = static_cast<uint16_t>(gsu.pipe() << 1);
        gsu.writeRamBuffer(gsu.ramaddr ^ 0, static_cast<uint8_t>(gsu.r[n]));
        gsu.writeRamBuffer(gsu.ramaddr ^ 1, static_cast<uint8_t>(gsu.r[n] >> 8));
    } else {
        gsu.setReg(n, static_cast<uint16_t>(static_cast<int8_t>(gsu.pipe())));
    }
    gsu.regReset();
}

// $B0-$BF FROM Rn / MOVES Rn,Rs
void fromMoves(Gsu& gsu, uint8_t opcode)
{
    const int n = opcode & 15;
    if (!gsu.flagB_()) {
        gsu.sreg = n;
    } else {
        gsu.setDest(gsu.r[n]);
        gsu.setFlag(Gsu::flagOv, (gsu.destValue() & 0x80) != 0);
        gsu.setZS(gsu.destValue());
        gsu.regReset();
    }
}

// $C0 HIB
void hib(Gsu& gsu, uint8_t)
{
    gsu.setDest(gsu.srcValue() >> 8);
    gsu.setFlag(Gsu::flagS, (gsu.destValue() & 0x80) != 0);
    gsu.setFlag(Gsu::flagZ, gsu.destValue() == 0);
    gsu.regReset();
}

// $C1-$CF OR Rn / alt1: XOR Rn / alt2: OR #n / alt3: XOR #n
void orXor(Gsu& gsu, uint8_t opcode)
{
    uint16_t n = opcode & 15;
    if (!gsu.alt2()) {
        n = gsu.r[n];
    }
    gsu.setDest(gsu.alt1() ? static_cast<uint16_t>(gsu.srcValue() ^ n)
                           : static_cast<uint16_t>(gsu.srcValue() | n));
    gsu.setZS(gsu.destValue());
    gsu.regReset();
}

// $D0-$DE INC Rn
void inc(Gsu& gsu, uint8_t opcode)
{
    const int n = opcode & 15;
    gsu.setReg(n, static_cast<uint16_t>(gsu.r[n] + 1));
    gsu.setZS(gsu.r[n]);
    gsu.regReset();
}

// $DF GETC / alt2: RAMB / alt3: ROMB
void getcRambRomb(Gsu& gsu, uint8_t)
{
    if (!gsu.alt2()) {
        gsu.colr = gsu.applyColorSource(gsu.readRomBuffer());
    } else if (!gsu.alt1()) {
        gsu.syncRamBuffer();
        gsu.rambr = (gsu.srcValue() & 0x01) != 0;
    } else {
        gsu.syncRomBuffer();
        gsu.rombr = static_cast<uint8_t>(gsu.srcValue() & 0x7f);
    }
    gsu.regReset();
}

// $E0-$EE DEC Rn
void dec(Gsu& gsu, uint8_t opcode)
{
    const int n = opcode & 15;
    gsu.setReg(n, static_cast<uint16_t>(gsu.r[n] - 1));
    gsu.setZS(gsu.r[n]);
    gsu.regReset();
}

// $EF GETB / alt1: GETBH / alt2: GETBL / alt3: GETBS
void getb(Gsu& gsu, uint8_t)
{
    const int mode = (gsu.alt2() ? 2 : 0) | (gsu.alt1() ? 1 : 0);
    switch (mode) {
    case 0:
        gsu.setDest(gsu.readRomBuffer());
        break;
    case 1:
        gsu.setDest(static_cast<uint16_t>((gsu.readRomBuffer() << 8) | (gsu.srcValue() & 0xff)));
        break;
    case 2:
        gsu.setDest(static_cast<uint16_t>((gsu.srcValue() & 0xff00) | gsu.readRomBuffer()));
        break;
    case 3:
        gsu.setDest(static_cast<uint16_t>(static_cast<int8_t>(gsu.readRomBuffer())));
        break;
    }
    gsu.regReset();
}

// $F0-$FF IWT Rn,#xxxx / alt1: LM Rn,(xxxx) / alt2: SM (xxxx),Rn
void iwtLmSm(Gsu& gsu, uint8_t opcode)
{
    const int n = opcode & 15;
    if (gsu.alt1()) {
        uint16_t address = gsu.pipe();
        address |= static_cast<uint16_t>(gsu.pipe() << 8);
        gsu.ramaddr = address;
        const uint8_t lo = gsu.readRamBuffer(gsu.ramaddr ^ 0);
        gsu.setReg(n, static_cast<uint16_t>((gsu.readRamBuffer(gsu.ramaddr ^ 1) << 8) | lo));
    } else if (gsu.alt2()) {
        uint16_t address = gsu.pipe();
        address |= static_cast<uint16_t>(gsu.pipe() << 8);
        gsu.ramaddr = address;
        gsu.writeRamBuffer(gsu.ramaddr ^ 0, static_cast<uint8_t>(gsu.r[n]));
        gsu.writeRamBuffer(gsu.ramaddr ^ 1, static_cast<uint8_t>(gsu.r[n] >> 8));
    } else {
        const uint8_t lo = gsu.pipe();
        gsu.setReg(n, static_cast<uint16_t>((gsu.pipe() << 8) | lo));
    }
    gsu.regReset();
}

} // namespace snesquik::gsu::operations
