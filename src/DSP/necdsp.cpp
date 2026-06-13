#include "DSP/necdsp.h"

#include <cstring>
#include <fstream>

namespace snesquik::dsp {

namespace {

template <typename T>
void appendPod(std::vector<uint8_t>& out, const T& value)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
bool readPod(const uint8_t*& pos, const uint8_t* end, T& value)
{
    if (static_cast<size_t>(end - pos) < sizeof(T)) {
        return false;
    }
    std::memcpy(&value, pos, sizeof(T));
    pos += sizeof(T);
    return true;
}

} // namespace

void NecDsp::power()
{
    regs = Registers{};
    sr = 0;
    dr = 0;
    dataRam.fill(0);
    // Program/data ROM and romLoaded are preserved across power so a loaded
    // dump survives a reset; callers that want a fresh chip call loadRom.
}

bool NecDsp::loadRom(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() != dumpBytes) {
        return false;
    }
    // The combined dump stores each 24-bit program word and each 16-bit data
    // word least-significant byte first.
    for (size_t i = 0; i < programWords; ++i) {
        const size_t o = i * 3;
        programRom[i] = static_cast<uint32_t>(bytes[o])
                      | (static_cast<uint32_t>(bytes[o + 1]) << 8)
                      | (static_cast<uint32_t>(bytes[o + 2]) << 16);
    }
    const size_t dataBase = programWords * 3;
    for (size_t i = 0; i < dataRomWords; ++i) {
        const size_t o = dataBase + i * 2;
        dataRom[i] = static_cast<uint16_t>(bytes[o]) | (static_cast<uint16_t>(bytes[o + 1]) << 8);
    }
    romLoaded = true;
    power();
    return true;
}

// ---------------------------------------------------------------------------
// Instruction execution
// ---------------------------------------------------------------------------
uint16_t NecDsp::readSrc(uint8_t src)
{
    switch (src & 0x0f) {
    case 0: return regs.trb;
    case 1: return regs.a;
    case 2: return regs.b;
    case 3: return regs.tr;
    case 4: return regs.dp;
    case 5: return regs.rp;
    case 6: return dataRom[regs.rp & 0x3ff];
    case 7: return static_cast<uint16_t>(0x8000 - (regs.flagA.s1 ? 1 : 0)); // SGN
    case 8: sr |= srRqm; return dr; // DR (reading sets RQM)
    case 9: return dr;              // DR (no flag)
    case 10: return sr;
    case 11: return regs.si;  // SIM (serial, unused)
    case 12: return regs.si;  // SIL
    case 13: return regs.k;
    case 14: return regs.l;
    case 15: return dataRam[regs.dp & 0x7ff];
    }
    return 0;
}

void NecDsp::writeDst(uint8_t dst, uint16_t idb)
{
    switch (dst & 0x0f) {
    case 0: break; // NON
    case 1: regs.a = idb; break;
    case 2: regs.b = idb; break;
    case 3: regs.tr = idb; break;
    case 4: regs.dp = idb; break;
    case 5: regs.rp = idb & 0x3ff; break;
    case 6: // DR: output, request transfer
        dr = idb;
        sr |= srRqm;
        break;
    case 7: // SR: the DSP controls only the user/control bits; the transfer
            // logic (RQM, DRS) and low status bits stay hardware-owned.
        sr = (sr & 0x907c) | (idb & ~0x907c);
        break;
    case 8: regs.so = idb; break; // SOL
    case 9: regs.so = idb; break; // SOM
    case 10: regs.k = idb; break;
    case 11: regs.k = idb; regs.l = dataRom[regs.rp & 0x3ff]; break;       // KLR
    case 12: regs.l = idb; regs.k = dataRam[((regs.dp & 0x7ff) | 0x40)]; break; // KLM
    case 13: regs.l = idb; break;
    case 14: regs.trb = idb; break;
    case 15: dataRam[regs.dp & 0x7ff] = idb; break; // MEM
    }
}

void NecDsp::execOp(uint32_t opcode)
{
    const uint8_t pselect = (opcode >> 20) & 0x03;
    const uint8_t alu = (opcode >> 16) & 0x0f;
    const uint8_t asl = (opcode >> 15) & 0x01;
    const uint8_t dpl = (opcode >> 13) & 0x03;
    const uint8_t dphm = (opcode >> 9) & 0x0f;
    const uint8_t rpdcr = (opcode >> 8) & 0x01;
    const uint8_t src = (opcode >> 4) & 0x0f;
    const uint8_t dst = (opcode >> 0) & 0x0f;

    const uint16_t idb = readSrc(src);

    // The multiplier output is recomputed every operation from K and L.
    const int32_t mul = static_cast<int32_t>(static_cast<int16_t>(regs.k))
                      * static_cast<int16_t>(regs.l);
    regs.m = static_cast<uint16_t>(mul >> 15);
    regs.n = static_cast<uint16_t>(mul << 1);

    if (alu != 0) {
        uint16_t p = 0;
        switch (pselect) {
        case 0: p = dataRam[regs.dp & 0x7ff]; break;
        case 1: p = idb; break;
        case 2: p = regs.m; break;
        case 3: p = regs.n; break;
        }
        uint16_t& acc = asl ? regs.b : regs.a;
        Flag& flag = asl ? regs.flagB : regs.flagA;
        const uint16_t q = acc;
        // The ALU carry-in comes from the OTHER accumulator's carry flag.
        const bool carryIn = asl ? regs.flagA.c : regs.flagB.c;
        uint32_t r = 0;
        switch (alu) {
        case 1: r = q | p; break;
        case 2: r = q & p; break;
        case 3: r = q ^ p; break;
        case 4: r = static_cast<uint32_t>(q) - p; break;
        case 5: r = static_cast<uint32_t>(q) + p; break;
        case 6: r = static_cast<uint32_t>(q) - p - carryIn; break;
        case 7: r = static_cast<uint32_t>(q) + p + carryIn; break;
        case 8: r = static_cast<uint32_t>(q) - 1; break;
        case 9: r = static_cast<uint32_t>(q) + 1; break;
        case 10: r = static_cast<uint16_t>(~q); break;
        case 11: r = static_cast<uint16_t>(static_cast<int16_t>(q) >> 1); break;
        case 12: r = (static_cast<uint32_t>(q) << 1) | (carryIn ? 1 : 0); break;
        case 13: r = (static_cast<uint32_t>(q) << 2) | 3; break;
        case 14: r = (static_cast<uint32_t>(q) << 4) | 15; break;
        case 15: r = (static_cast<uint32_t>(q) << 8) | (q >> 8); break;
        }

        const uint16_t result = static_cast<uint16_t>(r);
        // Basic flags first (matching MAME uPD7725): S1 latches the sign while
        // OV1 is set, otherwise tracks S0.
        flag.s0 = (result & 0x8000) != 0;
        flag.z = (result == 0);
        if (!flag.ov1) {
            flag.s1 = flag.s0;
        }
        switch (alu) {
        case 4: case 6: case 8: { // subtraction (SUB/SBB/DEC)
            const uint16_t pp = (alu == 8) ? 1 : p;
            flag.ov0 = (((q ^ result) & (q ^ pp)) & 0x8000) != 0;
            flag.c = result > q;
            flag.ov1 = (flag.ov0 && flag.ov1) ? (flag.s1 == flag.s0) : (flag.ov0 || flag.ov1);
            break;
        }
        case 5: case 7: case 9: { // addition (ADD/ADC/INC)
            const uint16_t pp = (alu == 9) ? 1 : p;
            flag.ov0 = (((q ^ result) & static_cast<uint16_t>(~(q ^ pp))) & 0x8000) != 0;
            flag.c = result < q;
            flag.ov1 = (flag.ov0 && flag.ov1) ? (flag.s1 == flag.s0) : (flag.ov0 || flag.ov1);
            break;
        }
        case 11: flag.c = (q & 0x0001) != 0; flag.ov0 = false; flag.ov1 = false; break; // SHR1
        case 12: flag.c = (q & 0x8000) != 0; flag.ov0 = false; flag.ov1 = false; break; // SHL1
        default: flag.c = false; flag.ov0 = false; flag.ov1 = false; break; // logical/SHL2/4/XCHG/CMP
        }
        acc = result;
    }

    writeDst(dst, idb);

    // Data-pointer update is skipped when the instruction wrote DP directly.
    if (dst != 4) {
        switch (dpl) {
        case 0: break;
        case 1: regs.dp = (regs.dp & 0xf0) | ((regs.dp + 1) & 0x0f); break;
        case 2: regs.dp = (regs.dp & 0xf0) | ((regs.dp - 1) & 0x0f); break;
        case 3: regs.dp = regs.dp & 0xf0; break;
        }
        regs.dp ^= static_cast<uint16_t>(dphm << 4);
    }
    // ROM-pointer decrement, skipped when the instruction wrote RP directly.
    if (rpdcr && dst != 5) {
        regs.rp = (regs.rp - 1) & 0x3ff;
    }
}

void NecDsp::execRt(uint32_t opcode)
{
    execOp(opcode);
    regs.sp = (regs.sp - 1) & 0x0f;
    regs.pc = regs.stack[regs.sp];
}

void NecDsp::execJp(uint32_t opcode)
{
    const uint16_t brch = (opcode >> 13) & 0x1ff;
    const uint16_t na = (opcode >> 2) & 0x7ff;

    // Unconditional jump / call.
    if (brch == 0x100) { regs.pc = na; return; }
    if (brch == 0x140) {
        regs.stack[regs.sp] = regs.pc;
        regs.sp = (regs.sp + 1) & 0x0f;
        regs.pc = na;
        return;
    }

    bool take = false;
    switch (brch) {
    case 0x080: take = !regs.flagA.c; break;   // JNCA
    case 0x082: take = regs.flagA.c; break;    // JCA
    case 0x084: take = !regs.flagB.c; break;   // JNCB
    case 0x086: take = regs.flagB.c; break;    // JCB
    case 0x088: take = !regs.flagA.z; break;   // JNZA
    case 0x08a: take = regs.flagA.z; break;    // JZA
    case 0x08c: take = !regs.flagB.z; break;   // JNZB
    case 0x08e: take = regs.flagB.z; break;    // JZB
    case 0x090: take = !regs.flagA.ov0; break; // JNOVA0
    case 0x092: take = regs.flagA.ov0; break;  // JOVA0
    case 0x094: take = !regs.flagB.ov0; break; // JNOVB0
    case 0x096: take = regs.flagB.ov0; break;  // JOVB0
    case 0x098: take = !regs.flagA.ov1; break; // JNOVA1
    case 0x09a: take = regs.flagA.ov1; break;  // JOVA1
    case 0x09c: take = !regs.flagB.ov1; break; // JNOVB1
    case 0x09e: take = regs.flagB.ov1; break;  // JOVB1
    case 0x0a0: take = !regs.flagA.s0; break;  // JNSA0
    case 0x0a2: take = regs.flagA.s0; break;   // JSA0
    case 0x0a4: take = !regs.flagB.s0; break;  // JNSB0
    case 0x0a6: take = regs.flagB.s0; break;   // JSB0
    case 0x0a8: take = !regs.flagA.s1; break;  // JNSA1
    case 0x0aa: take = regs.flagA.s1; break;   // JSA1
    case 0x0ac: take = !regs.flagB.s1; break;  // JNSB1
    case 0x0ae: take = regs.flagB.s1; break;   // JSB1
    case 0x0b0: take = (regs.dp & 0x0f) == 0x00; break; // JDPL0
    case 0x0b1: take = (regs.dp & 0x0f) != 0x00; break; // JDPLN0
    case 0x0b2: take = (regs.dp & 0x0f) == 0x0f; break; // JDPLF
    case 0x0b3: take = (regs.dp & 0x0f) != 0x0f; break; // JDPLNF
    case 0x0b4: take = true; break;  // JNSIAK (serial in ack=0 on SNES)
    case 0x0b6: take = false; break; // JSIAK
    case 0x0b8: take = true; break;  // JNSOAK (serial out ack=0 on SNES)
    case 0x0ba: take = false; break; // JSOAK
    case 0x0bc: take = (sr & srRqm) == 0; break; // JNRQM
    case 0x0be: // JRQM: the DSP blocks here waiting for the S-CPU transfer.
        take = (sr & srRqm) != 0;
        if (take) {
            dspWaiting = true;
        }
        break;
    default: take = false; break;
    }
    if (take) {
        regs.pc = na;
    }
}

void NecDsp::execLd(uint32_t opcode)
{
    const uint16_t imm = static_cast<uint16_t>((opcode >> 6) & 0xffff);
    const uint8_t dst = opcode & 0x0f;
    writeDst(dst, imm);
}

void NecDsp::exec()
{
    if (!romLoaded) {
        return;
    }
    dspWaiting = false;
    const uint32_t opcode = programRom[regs.pc & 0x7ff];
    regs.pc = (regs.pc + 1) & 0x7ff;
    switch (opcode >> 22) {
    case 0: execOp(opcode); break;
    case 1: execRt(opcode); break;
    case 2: execJp(opcode); break;
    case 3: execLd(opcode); break;
    }
}

void NecDsp::runToRequest()
{
    if (!romLoaded) {
        return;
    }
    // Advance until the DSP blocks at a JRQM wait (it has produced output /
    // requested input and is waiting for the S-CPU). Stopping merely because
    // RQM is set would be wrong: reading an input word (src 8) also sets RQM,
    // but the DSP keeps running until it explicitly waits. Bounded for safety.
    for (uint32_t i = 0; i < 200000; ++i) {
        exec();
        if (dspWaiting) {
            break;
        }
    }
}

void NecDsp::step(uint32_t cycles)
{
    if (!romLoaded) {
        return;
    }
    // Run up to `cycles` instructions, stopping once the DSP blocks at a JRQM
    // wait (further execution would just spin that wait loop).
    for (uint32_t i = 0; i < cycles; ++i) {
        exec();
        if (dspWaiting) {
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// S-CPU interface (DR/SR with the RQM / 16-bit two-step handshake)
// ---------------------------------------------------------------------------
uint8_t NecDsp::readDR()
{
    uint8_t value;
    if ((sr & srDrc) == 0) {
        // 16-bit transfer: low byte then high byte.
        if ((sr & srDrs) == 0) {
            sr |= srDrs;
            value = static_cast<uint8_t>(dr);
        } else {
            sr &= ~(srDrs | srRqm);
            value = static_cast<uint8_t>(dr >> 8);
            runToRequest();
        }
    } else {
        // 8-bit transfer.
        sr &= ~srRqm;
        value = static_cast<uint8_t>(dr);
        runToRequest();
    }
    return value;
}

void NecDsp::writeDR(uint8_t value)
{
    if ((sr & srDrc) == 0) {
        if ((sr & srDrs) == 0) {
            sr |= srDrs;
            dr = (dr & 0xff00) | value;
        } else {
            sr &= ~(srDrs | srRqm);
            dr = (dr & 0x00ff) | (static_cast<uint16_t>(value) << 8);
            runToRequest();
        }
    } else {
        sr &= ~srRqm;
        dr = (dr & 0xff00) | value;
        runToRequest();
    }
}

// ---------------------------------------------------------------------------
// Save state
// ---------------------------------------------------------------------------
void NecDsp::saveState(std::vector<uint8_t>& out) const
{
    appendPod(out, regs);
    appendPod(out, sr);
    appendPod(out, dr);
    appendPod(out, dataRam);
}

bool NecDsp::loadState(const uint8_t* data, size_t size)
{
    const uint8_t* pos = data;
    const uint8_t* end = data + size;
    if (!readPod(pos, end, regs) || !readPod(pos, end, sr) || !readPod(pos, end, dr)
        || !readPod(pos, end, dataRam)) {
        return false;
    }
    return true;
}

} // namespace snesquik::dsp
