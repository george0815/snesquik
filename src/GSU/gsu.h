#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace snesquik::gsu {

class Gsu;

using OperationFn = void (*)(Gsu&, uint8_t opcode);

struct Instruction {
    const char* mnemonic;
    OperationFn operation;
};

// Super FX (GSU) coprocessor. The S-CPU sees it through MMIO at
// $3000-$34FF and shares the cartridge ROM and the GSU work RAM with it.
class Gsu {
public:
    void power();
    void attachRom(std::span<const uint8_t> romBytes);
    void setRamSize(size_t bytes);

    // Run the GSU for the given number of master clocks (21.477 MHz domain).
    void run(uint32_t masterClocks);

    bool running() const { return goFlag(); }
    bool irqPending() const { return (sfr & flagIrq) != 0; }

    // S-CPU side MMIO ($3000-$34FF in banks $00-$3F / $80-$BF).
    uint8_t readIo(uint16_t address);
    void writeIo(uint16_t address, uint8_t value);

    // S-CPU side views of the shared memories.
    bool cpuCanSeeRom() const { return !(goFlag() && scmrRon); }
    bool cpuCanSeeRam() const { return !(goFlag() && scmrRan); }
    uint8_t cpuRomConflictValue(uint32_t address) const;
    uint8_t readRam(uint32_t offset) const;
    void writeRam(uint32_t offset, uint8_t value);
    size_t ramSize() const { return ram.size(); }

    void saveState(std::vector<uint8_t>& out) const;
    bool loadState(const uint8_t* data, size_t size);

    // Status flag bits in SFR.
    static constexpr uint16_t flagZ = 0x0002;
    static constexpr uint16_t flagCy = 0x0004;
    static constexpr uint16_t flagS = 0x0008;
    static constexpr uint16_t flagOv = 0x0010;
    static constexpr uint16_t flagG = 0x0020;
    static constexpr uint16_t flagR = 0x0040;
    static constexpr uint16_t flagAlt1 = 0x0100;
    static constexpr uint16_t flagAlt2 = 0x0200;
    static constexpr uint16_t flagIl = 0x0400;
    static constexpr uint16_t flagIh = 0x0800;
    static constexpr uint16_t flagB = 0x1000;
    static constexpr uint16_t flagIrq = 0x8000;

    // Register/state access for the operation implementations and tests.
    uint16_t reg(int n) const { return r[n & 15]; }
    void setReg(int n, uint16_t value);
    uint16_t srcValue() const { return r[sreg]; }
    void setDest(uint16_t value);
    uint16_t destValue() const { return r[dreg]; }

    bool alt1() const { return (sfr & flagAlt1) != 0; }
    bool alt2() const { return (sfr & flagAlt2) != 0; }
    bool flagB_() const { return (sfr & flagB) != 0; }
    bool goFlag() const { return (sfr & flagG) != 0; }
    void setFlag(uint16_t flag, bool value)
    {
        if (value) {
            sfr |= flag;
        } else {
            sfr = static_cast<uint16_t>(sfr & ~flag);
        }
    }
    void setZS(uint16_t value)
    {
        setFlag(flagZ, value == 0);
        setFlag(flagS, (value & 0x8000) != 0);
    }
    // End-of-instruction register file reset (clears prefixes).
    void regReset()
    {
        setFlag(flagB, false);
        setFlag(flagAlt1, false);
        setFlag(flagAlt2, false);
        sreg = 0;
        dreg = 0;
    }

    // Pipeline access (consumes the prefetched byte and refills).
    uint8_t pipe();

    // Memory subsystem used by operations.
    uint8_t readRomBuffer();
    void syncRomBuffer();
    void syncRamBuffer();
    uint8_t readRamBuffer(uint16_t address);
    void writeRamBuffer(uint16_t address, uint8_t value);
    void updateRomBuffer();

    void plot(uint8_t x, uint8_t y);
    uint8_t rpix(uint8_t x, uint8_t y);
    uint8_t applyColorSource(uint8_t source) const;
    void flushCache();
    void stopGsu();

    void step(uint32_t cycles);

    // Internal architectural state (kept public for the operation table and
    // unit tests, mirroring the CPU core's accessibility style).
    std::array<uint16_t, 16> r{};
    uint16_t sfr = 0;
    uint8_t pbr = 0;
    uint8_t rombr = 0;
    bool rambr = false;
    uint16_t cbr = 0;
    uint8_t scbr = 0;
    uint8_t scmrHt = 0;
    bool scmrRon = false;
    bool scmrRan = false;
    uint8_t scmrMd = 0;
    uint8_t colr = 0;
    uint8_t por = 0;
    bool bramr = false;
    uint8_t vcr = 0x04;
    bool cfgrIrqMask = false;
    bool cfgrMs0 = false;
    bool clsr = false;

    uint8_t pipeline = 0x01;
    uint16_t ramaddr = 0;
    int sreg = 0;
    int dreg = 0;
    bool r15Modified = false;

    // ROM/RAM buffering (latency-modelled like hardware).
    uint32_t romcl = 0;
    uint8_t romdr = 0;
    uint32_t ramcl = 0;
    uint16_t ramar = 0;
    uint8_t ramdr = 0;

    struct Cache {
        std::array<uint8_t, 512> buffer{};
        std::array<bool, 32> valid{};
    } cache;

    struct PixelCache {
        uint16_t offset = 0xffff;
        uint8_t bitpend = 0;
        std::array<uint8_t, 8> data{};
    };
    std::array<PixelCache, 2> pixelCache;

    // POR bits.
    bool porTransparent() const { return (por & 0x01) != 0; }
    bool porDither() const { return (por & 0x02) != 0; }
    bool porHighNibble() const { return (por & 0x04) != 0; }
    bool porFreezeHigh() const { return (por & 0x08) != 0; }
    bool porObj() const { return (por & 0x10) != 0; }

private:
    uint8_t readOpcode(uint16_t address);
    uint8_t readMemory(uint32_t address);
    void writeMemory(uint32_t address, uint8_t value);
    uint8_t readCacheByte(uint16_t offset) const;
    void writeCacheByte(uint16_t offset, uint8_t value);
    void flushPixelCache(PixelCache& pc);
    uint32_t screenPixelAddress(uint8_t x, uint8_t y, uint32_t& bpp) const;
    void executeOne();

    std::span<const uint8_t> rom;
    std::vector<uint8_t> ram;
    uint32_t romMask = 0;
    uint32_t ramMask = 0;

    // Cycle budget in GSU clocks for the current run() slice.
    int64_t budget = 0;

public:
    // Debug tracing: when traceRemaining > 0, executed instructions are
    // appended to traceSink (set by the probe).
    uint64_t traceRemaining = 0;
    void* traceSink = nullptr; // std::FILE*
    uint64_t stopCount = 0;
    uint64_t instructionCount = 0;
};

const std::array<Instruction, 256>& opcodeTable();

namespace operations {
void stop(Gsu& gsu, uint8_t opcode);
void nop(Gsu& gsu, uint8_t opcode);
void cacheOp(Gsu& gsu, uint8_t opcode);
void lsr(Gsu& gsu, uint8_t opcode);
void rol(Gsu& gsu, uint8_t opcode);
void branch(Gsu& gsu, uint8_t opcode);
void toMove(Gsu& gsu, uint8_t opcode);
void with(Gsu& gsu, uint8_t opcode);
void store(Gsu& gsu, uint8_t opcode);
void loop(Gsu& gsu, uint8_t opcode);
void alt1(Gsu& gsu, uint8_t opcode);
void alt2(Gsu& gsu, uint8_t opcode);
void alt3(Gsu& gsu, uint8_t opcode);
void load(Gsu& gsu, uint8_t opcode);
void plotRpix(Gsu& gsu, uint8_t opcode);
void swap(Gsu& gsu, uint8_t opcode);
void colorCmode(Gsu& gsu, uint8_t opcode);
void notOp(Gsu& gsu, uint8_t opcode);
void addAdc(Gsu& gsu, uint8_t opcode);
void subSbcCmp(Gsu& gsu, uint8_t opcode);
void merge(Gsu& gsu, uint8_t opcode);
void andBic(Gsu& gsu, uint8_t opcode);
void multUmult(Gsu& gsu, uint8_t opcode);
void sbk(Gsu& gsu, uint8_t opcode);
void link(Gsu& gsu, uint8_t opcode);
void sex(Gsu& gsu, uint8_t opcode);
void asrDiv2(Gsu& gsu, uint8_t opcode);
void ror(Gsu& gsu, uint8_t opcode);
void jmpLjmp(Gsu& gsu, uint8_t opcode);
void lob(Gsu& gsu, uint8_t opcode);
void fmultLmult(Gsu& gsu, uint8_t opcode);
void ibtLmsSms(Gsu& gsu, uint8_t opcode);
void fromMoves(Gsu& gsu, uint8_t opcode);
void hib(Gsu& gsu, uint8_t opcode);
void orXor(Gsu& gsu, uint8_t opcode);
void inc(Gsu& gsu, uint8_t opcode);
void getcRambRomb(Gsu& gsu, uint8_t opcode);
void dec(Gsu& gsu, uint8_t opcode);
void getb(Gsu& gsu, uint8_t opcode);
void iwtLmSm(Gsu& gsu, uint8_t opcode);
}

} // namespace snesquik::gsu
