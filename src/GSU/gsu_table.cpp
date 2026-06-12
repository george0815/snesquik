#include "GSU/gsu.h"

namespace snesquik::gsu {

namespace {

std::array<Instruction, 256> buildTable()
{
    using namespace operations;
    std::array<Instruction, 256> table{};

    auto set = [&](uint8_t opcode, const char* mnemonic, OperationFn fn) {
        table[opcode] = {mnemonic, fn};
    };
    auto setRange = [&](uint8_t first, uint8_t last, const char* mnemonic, OperationFn fn) {
        for (int opcode = first; opcode <= last; ++opcode) {
            table[opcode] = {mnemonic, fn};
        }
    };

    set(0x00, "STOP", stop);
    set(0x01, "NOP", nop);
    set(0x02, "CACHE", cacheOp);
    set(0x03, "LSR", lsr);
    set(0x04, "ROL", rol);
    set(0x05, "BRA", branch);
    set(0x06, "BLT", branch);
    set(0x07, "BGE", branch);
    set(0x08, "BNE", branch);
    set(0x09, "BEQ", branch);
    set(0x0a, "BPL", branch);
    set(0x0b, "BMI", branch);
    set(0x0c, "BCC", branch);
    set(0x0d, "BCS", branch);
    set(0x0e, "BVC", branch);
    set(0x0f, "BVS", branch);
    setRange(0x10, 0x1f, "TO/MOVE", toMove);
    setRange(0x20, 0x2f, "WITH", with);
    setRange(0x30, 0x3b, "STW/STB", store);
    set(0x3c, "LOOP", loop);
    set(0x3d, "ALT1", alt1);
    set(0x3e, "ALT2", alt2);
    set(0x3f, "ALT3", alt3);
    setRange(0x40, 0x4b, "LDW/LDB", load);
    set(0x4c, "PLOT/RPIX", plotRpix);
    set(0x4d, "SWAP", swap);
    set(0x4e, "COLOR/CMODE", colorCmode);
    set(0x4f, "NOT", notOp);
    setRange(0x50, 0x5f, "ADD/ADC", addAdc);
    setRange(0x60, 0x6f, "SUB/SBC/CMP", subSbcCmp);
    set(0x70, "MERGE", merge);
    setRange(0x71, 0x7f, "AND/BIC", andBic);
    setRange(0x80, 0x8f, "MULT/UMULT", multUmult);
    set(0x90, "SBK", sbk);
    setRange(0x91, 0x94, "LINK", link);
    set(0x95, "SEX", sex);
    set(0x96, "ASR/DIV2", asrDiv2);
    set(0x97, "ROR", ror);
    setRange(0x98, 0x9d, "JMP/LJMP", jmpLjmp);
    set(0x9e, "LOB", lob);
    set(0x9f, "FMULT/LMULT", fmultLmult);
    setRange(0xa0, 0xaf, "IBT/LMS/SMS", ibtLmsSms);
    setRange(0xb0, 0xbf, "FROM/MOVES", fromMoves);
    set(0xc0, "HIB", hib);
    setRange(0xc1, 0xcf, "OR/XOR", orXor);
    setRange(0xd0, 0xde, "INC", inc);
    set(0xdf, "GETC/RAMB/ROMB", getcRambRomb);
    setRange(0xe0, 0xee, "DEC", dec);
    set(0xef, "GETB", getb);
    setRange(0xf0, 0xff, "IWT/LM/SM", iwtLmSm);

    return table;
}

} // namespace

const std::array<Instruction, 256>& opcodeTable()
{
    static const std::array<Instruction, 256> table = buildTable();
    return table;
}

} // namespace snesquik::gsu
