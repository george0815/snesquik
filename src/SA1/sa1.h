#pragma once

#include "CPU_R5A22/core.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace snesquik::sa1 {

// SA-1 coprocessor: a second 65816 CPU (10.74 MHz) with its own memory
// mapping, 2 KB internal RAM (I-RAM), shared battery work RAM (BW-RAM), an
// arithmetic unit, and bidirectional IRQ/NMI messaging with the S-CPU.
//
// This first implementation is the "foundation": CPU + memory map + MMIO +
// IRQ messaging + arithmetic unit. DMA, character conversion, variable-length
// bit processing, bitmap BW-RAM, and timers are deferred.
//
// `Sa1` itself is the cpu_r5a22::Bus seen by the *internal* SA-1 CPU. The
// S-CPU reaches the SA-1 through the cpuXxx() accessors, which SnesBus calls
// when an SA-1 cartridge is attached (mirroring the GSU integration).
class Sa1 final : public cpu_r5a22::Bus {
public:
    Sa1();

    void power();
    void attachRom(std::span<const uint8_t> romBytes);
    void setBwRamSize(size_t bytes);

    // Advance the SA-1 by the given number of S-CPU master clocks (21.477 MHz).
    void stepSa1(uint32_t masterClocks);

    // SA-1 -> S-CPU interrupt line (level), OR'd into the S-CPU IRQ by main.
    bool irqToScpuPending() const { return cpuIrqFlag && cpuIrqEnable; }

    // ---- cpu_r5a22::Bus: the SA-1 CPU's own memory map ----
    uint8_t read8(uint32_t address) override;
    void write8(uint32_t address, uint8_t value) override;

    // ---- S-CPU side accessors (called by SnesBus) ----
    bool ioAddress(uint16_t offset) const { return offset >= 0x2200 && offset <= 0x23ff; }
    uint8_t cpuReadIo(uint16_t offset);
    void cpuWriteIo(uint16_t offset, uint8_t value);
    uint8_t cpuReadIram(uint16_t offset)
    {
        // While a type-1 character-conversion DMA is active, the S-CPU reads
        // the converted tile data through I-RAM (built on the fly from the
        // BW-RAM bitmap).
        if (ccActive) {
            return ccDmaRead(offset & 0x7ff);
        }
        return iram[offset & 0x7ff];
    }
    void cpuWriteIram(uint16_t offset, uint8_t value) { iram[offset & 0x7ff] = value; }
    uint8_t cpuReadBwWindow(uint16_t offset) const;
    void cpuWriteBwWindow(uint16_t offset, uint8_t value);
    uint8_t cpuReadBwLinear(uint32_t address) const;
    void cpuWriteBwLinear(uint32_t address, uint8_t value);
    // ROM as the S-CPU sees it (MMC-banked). cartOffset masked to ROM size.
    uint8_t cpuReadRom(uint32_t address) const { return readRom(mmcRom(address)); }

    size_t bwRamSize() const { return bwram.size(); }
    const std::vector<uint8_t>& bwRam() const { return bwram; }
    std::vector<uint8_t>& bwRam() { return bwram; }

    void saveState(std::vector<uint8_t>& out) const;
    bool loadState(const uint8_t* data, size_t size);

    const cpu_r5a22::CPU& cpu() const { return sa1Cpu; }
    cpu_r5a22::CPU& cpu() { return sa1Cpu; }
    bool running() const { return !sa1Reset && !sa1Wait; }

private:
    // Shared MMC ROM mapping (same for both CPUs); returns a cart byte offset.
    uint32_t mmcRom(uint32_t address) const;
    uint8_t readRom(uint32_t cartOffset) const
    {
        return rom.empty() ? 0xff : rom[cartOffset & romMask];
    }
    uint8_t readBwBlock(uint32_t block, uint16_t offset) const;
    void writeBwBlock(uint32_t block, uint16_t offset, uint8_t value);

    // Register file shared by both sides; `fromSa1` distinguishes the reader.
    uint8_t regRead(uint16_t offset, bool fromSa1);
    void regWrite(uint16_t offset, uint8_t value, bool fromSa1);
    void runArithmetic();
    void updateSa1IrqLine();
    void deliverSa1Reset();
    // DMA / character-conversion DMA.
    void dmaNormal();
    void dmaCharConv2();        // type-2: convert a BRF line into I-RAM
    uint8_t ccDmaRead(uint16_t iramOffset); // type-1: on-read conversion
    uint8_t dmaSourceByte(uint32_t address) const;
    void dmaDestByte(uint32_t address, uint8_t value);

    cpu_r5a22::CPU sa1Cpu;

    std::span<const uint8_t> rom;
    uint32_t romMask = 0;
    std::vector<uint8_t> bwram;
    uint32_t bwramMask = 0;
    std::array<uint8_t, 0x800> iram{};

    int64_t budget = 0;       // SA-1 cycles available to run
    uint32_t clockRemainder = 0; // leftover master clocks (master = 2 SA-1 cyc)

    // $2200 CCNT (S-CPU -> SA-1 control)
    bool sa1IrqReq = false;   // bit7: IRQ message to SA-1
    bool sa1Wait = false;     // bit6: ready barrier (halt SA-1)
    bool sa1Reset = true;     // bit5: reset barrier (1 = held in reset)
    bool sa1NmiReq = false;   // bit4: NMI message to SA-1
    uint8_t messageToSa1 = 0; // bits3-0
    // $2201 SIE / $2202 SIC (S-CPU interrupt enable/clear for SA-1 sources)
    bool cpuIrqEnable = false;   // SA-1 -> S-CPU IRQ enable
    bool chdmaIrqEnable = false;
    // $2203-$2208 SA-1 CPU vectors
    uint16_t crv = 0, cnv = 0, civ = 0;
    // $2209 SCNT (SA-1 -> S-CPU control)
    bool cpuIrqReq = false;  // bit7: IRQ message to S-CPU
    bool cpuIvSel = false;   // bit6: S-CPU IRQ vector uses SIV
    bool cpuNvSel = false;   // bit4: S-CPU NMI vector uses SNV
    uint8_t messageToCpu = 0;
    // $220A CIE / $220B CIC (SA-1 interrupt enable/clear)
    bool sa1IrqEnable = false;
    bool timerIrqEnable = false;
    bool dmaIrqEnable = false;
    bool sa1NmiEnable = false;
    // $220C-$220F S-CPU vectors (redirected by the SA-1)
    uint16_t snv = 0, siv = 0;
    // $2220-$2223 MMC ROM super-bank registers (3-bit bank, mode bit)
    uint8_t romBank[4] = {0, 1, 2, 3};
    bool romBankMode[4] = {false, false, false, false};
    // $2224 BMAPS (S-CPU BW-RAM 8 KB block) / $2225 BMAP (SA-1 block)
    uint8_t cpuBwBlock = 0;
    uint8_t sa1BwBlock = 0;
    bool sa1BwBitmap = false; // $2225 bit7 (bitmap window; deferred)
    // Pending-interrupt flags
    bool cpuIrqFlag = false;   // SA-1 -> S-CPU IRQ pending
    bool chdmaIrqFlag = false;
    bool sa1IrqFlag = false;   // S-CPU -> SA-1 IRQ pending
    bool timerIrqFlag = false;
    bool dmaIrqFlag = false;
    bool sa1NmiFlag = false;   // S-CPU -> SA-1 NMI pending
    bool sa1NmiLine = false;   // edge tracker for NMI delivery
    // $2250-$2254 arithmetic unit
    bool arithSum = false;     // 0 = mul/div, 1 = cumulative sum
    bool arithDivide = false;  // 0 = multiply, 1 = divide
    uint16_t mathA = 0, mathB = 0;
    uint64_t mathResult = 0;   // 40-bit
    bool mathOverflow = false;
    // $2230-$224F DMA / character-conversion DMA
    bool dmaEnable = false;    // DCNT bit7 (DMAEN)
    bool dmaPriority = false;  // DCNT bit6 (DPRIO)
    bool ccEnable = false;     // DCNT bit5 (CDEN)
    bool ccType1 = false;      // DCNT bit4 (CDSEL: 1=type1, 0=type2)
    uint8_t dmaDest = 0;       // DCNT bit2 (DD: 0=I-RAM, 1=BW-RAM)
    uint8_t dmaSource = 0;     // DCNT bits1-0 (SD: 0=ROM, 1=BW-RAM, 2=I-RAM)
    uint8_t ccColorBits = 0;   // CDMA bits1-0 (bpp = 2 << (2 - ccColorBits))
    uint8_t ccSize = 0;        // CDMA bits4-2 (virtual VRAM width)
    uint32_t dmaSourceAddr = 0; // $2232-34 (DSA)
    uint32_t dmaDestAddr = 0;   // $2235-37 (DDA)
    uint16_t dmaCount = 0;      // $2238-39 (DTC)
    bool bitmapFormat = false;  // $223F bit7 (BBF)
    std::array<uint8_t, 16> ccBrf{}; // $2240-$224F bitmap register file
    uint8_t ccLine = 0;         // type-2 line counter (0-15)
    bool ccActive = false;      // type-1 conversion intercepts I-RAM reads
};

} // namespace snesquik::sa1
