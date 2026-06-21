#pragma once

#include "APU_SPC700/apu.h"
#include "CPU_R5A22/core.h"
#include "DSP/necdsp.h"
#include "GSU/gsu.h"
#include "SA1/sa1.h"
#include "SDD1/sdd1.h"
#include "S-PPU/ppu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace snesquik::bus {

namespace ppu = snesquik::ppu;

enum class CartridgeMap : uint8_t {
    LoROM,
    HiROM,
    ExHiROM,
};

enum JoypadButton : uint16_t {
    JoypadB = 0x8000,
    JoypadY = 0x4000,
    JoypadSelect = 0x2000,
    JoypadStart = 0x1000,
    JoypadUp = 0x0800,
    JoypadDown = 0x0400,
    JoypadLeft = 0x0200,
    JoypadRight = 0x0100,
    JoypadA = 0x0080,
    JoypadX = 0x0040,
    JoypadL = 0x0020,
    JoypadR = 0x0010,
};

// Logical SNES controller buttons, independent of any input source (keyboard,
// gamepad, scripted input). Frontends translate their raw events into these
// and feed them to SnesBus::setButton; the bus owns the joypad bit state.
enum class ControllerButton : uint8_t {
    B,
    Y,
    Select,
    Start,
    Up,
    Down,
    Left,
    Right,
    A,
    X,
    L,
    R,
};

class CartridgeRom {
public:
    CartridgeRom() = default;
    CartridgeRom(std::vector<uint8_t> bytes, CartridgeMap map);

    void load(std::vector<uint8_t> bytes, CartridgeMap map, size_t ramSize = 0);
    std::vector<uint8_t>& sramData() { return sram; }
    const std::vector<uint8_t>& sramData() const { return sram; }
    bool empty() const { return rom.empty(); }
    size_t size() const { return rom.size(); }
    CartridgeMap map() const { return mapping; }
    std::span<const uint8_t> bytes() const { return rom; }

    uint8_t read(size_t offset) const;
    uint8_t readSram(size_t offset) const;
    void writeSram(size_t offset, uint8_t value);
    std::optional<size_t> mapCpuAddress(uint32_t address) const;
    std::optional<size_t> mapSramAddress(uint32_t address) const;

    static constexpr uint32_t headerCpuAddress = 0x00ffc0;
    static std::optional<size_t> headerOffset(CartridgeMap map, size_t romSize);

private:
    std::vector<uint8_t> rom;
    std::vector<uint8_t> sram;
    CartridgeMap mapping = CartridgeMap::LoROM;
};

class TraceListener {
public:
    virtual ~TraceListener() = default;
    virtual void mmioRead(uint16_t address, uint8_t value) = 0;
    virtual void mmioWrite(uint16_t address, uint8_t value) = 0;
    virtual void dmaStart(uint8_t channel, uint16_t size) = 0;
    virtual void vblank(bool active) = 0;

    virtual void dmaTransfer(
    uint8_t channel,
    uint32_t sourceAddress,
    uint8_t bbad,
    uint8_t value,
    uint16_t vramAddress) = 0;

    virtual void hdmaWrite(uint8_t channel, uint16_t address, uint8_t value) = 0;
};

class SnesBus final : public cpu_r5a22::Bus {
public:
    uint8_t read8(uint32_t address) override;
    void write8(uint32_t address, uint8_t value) override;

    void attachCartridge(std::vector<uint8_t> rom, CartridgeMap map, size_t ramSize = 0);
    void attachPpu(ppu::Ppu* ppuCore);
    void setTraceListener(TraceListener* listener);
    const CartridgeRom& cartridge() const { return cart; }
    CartridgeRom& cartridge() { return cart; }



    void beginFrame();
    void runDma(uint8_t channelMask);
    void runHdmaScanline();
    void setVblank(bool active);
    bool nmiEnabled() const { return nmiEnable; }

    void initApu();
    // Wire in the CPU so APU stepping and port accesses can be aligned to the
    // exact CPU cycle. When set, stepApu()'s argument is treated as extra
    // (non-CPU-counter) cycles such as DMA halt time.
    void setCpu(const cpu_r5a22::CPU& c) { cpuTiming = &c; }
    void stepApu(uint32_t extraCycles);
    void endApuFrame();
    apu::Apu& getApu() { return apuCore; }

    void attachGsu(size_t ramSize);
    bool hasGsu() const { return gsuPresent; }
    void stepGsu(uint32_t masterClocks);
    bool gsuIrqPending() const { return gsuPresent && gsuCore.irqPending(); }
    gsu::Gsu& getGsu() { return gsuCore; }

    void attachSa1(size_t bwRamSize);
    bool hasSa1() const { return sa1Present; }
    void stepSa1(uint32_t masterClocks);
    bool sa1IrqPending() const { return sa1Present && sa1Core.irqToScpuPending(); }
    sa1::Sa1& getSa1() { return sa1Core; }

    void attachDsp(const std::string& romPath, CartridgeMap map);
    bool hasDsp() const { return dspPresent; }
    void stepDsp(uint32_t cycles);
    dsp::NecDsp& getDsp() { return dspCore; }

    void attachSdd1();
    bool hasSdd1() const { return sdd1Present; }
    sdd1::Sdd1& getSdd1() { return sdd1Core; }

    void saveState(std::vector<uint8_t>& out);
    bool loadState(const uint8_t* data, size_t size);

    bool irqEnabled() const { return hvIrqMode != 0; }
    uint8_t irqMode() const { return hvIrqMode; }
    uint16_t irqHTime() const { return irqHTimeVal; }
    uint16_t irqVTime() const { return irqVTimeVal; }
    bool irqFlag() const { return irqPending; }
    void clearIrqFlag() { irqPending = false; }
    void setIrq() { irqPending = true; }
    void checkIrq(uint16_t h, uint16_t v);
    void checkIrqCrossing(uint16_t prevH, uint16_t prevV, uint16_t currH, uint16_t currV);

    uint32_t consumeDmaDots();

    void setJoypadState(uint16_t state);
    void setButton(ControllerButton button, bool pressed);
    uint16_t joypadState() const { return joypadCurrentState; }
    void beginJoypadAutoRead();
    void finishJoypadAutoRead();
    void tickJoypadAutoRead(uint32_t cpuCycles);
    bool joypadAutoReadEnabled() const { return joypadAutoReadEnable; }
    bool joypadAutoReadIsBusy() const { return joypadAutoReadBusy; }

    bool nmiEdgePending() const { return nmiEdge; }
    void clearNmiEdge() { nmiEdge = false; }

    uint8_t readWram(uint32_t offset) const;
    void writeWram(uint32_t offset, uint8_t value);

    uint8_t readMmio(uint16_t address) const;
    void writeMmio(uint16_t address, uint8_t value);

    uint8_t openBus() const { return openBusValue; }

    struct ApuPortLog {
        uint8_t lastWrite[4] = {};
        uint8_t lastRead[4] = {};
        uint32_t writeCount[4] = {};
        uint32_t readCount[4] = {};
        uint32_t totalWrites = 0;
        uint32_t totalReads = 0;
        void reset() { writeCount[0]=writeCount[1]=writeCount[2]=writeCount[3]=0; readCount[0]=readCount[1]=readCount[2]=readCount[3]=0; totalWrites=0; totalReads=0; }
    };
    void resetApuPortLog() { apuPortLog.reset(); }
    const ApuPortLog& getApuPortLog() const { return apuPortLog; }

    struct GameFlagLog {
        uint32_t writeCount = 0;
        uint8_t lastValueWritten = 0;
        void reset() { writeCount = 0; lastValueWritten = 0; }
    };
    void resetGameFlagLog() { gameFlagLog.reset(); }
    const GameFlagLog& getGameFlagLog() const { return gameFlagLog; }

private:
    std::optional<size_t> mapWram(uint32_t address) const;
    std::optional<size_t> mapMmio(uint32_t address) const;
    // SA-1 address decode shared by read8/write8/readRaw/writeRaw. Returns
    // true (and sets `value` on read) when the SA-1 owns the address; false
    // means the access falls through to the normal S-CPU map (WRAM / PPU /
    // CPU MMIO).
    bool sa1MapRead(uint32_t address, uint8_t& value);
    bool sa1MapWrite(uint32_t address, uint8_t value);
    // DSP-1 register decode (DR/SR) shared by all four access paths.
    bool dspMapRead(uint32_t address, uint8_t& value);
    bool dspMapWrite(uint32_t address, uint8_t value);
    // S-DD1 decode: $4800-$4807 registers and the $C0-$FF MMC ROM window.
    bool sdd1MapRead(uint32_t address, uint8_t& value);
    bool sdd1MapWrite(uint32_t address, uint8_t value);
    uint8_t readRaw(uint32_t address);
    void writeRaw(uint32_t address, uint8_t value);
    void transferDmaByte(uint8_t channel, uint16_t index);
    void reloadHdma(uint8_t channel);
    void transferHdma(uint8_t channel);
    uint8_t dmaBAddress(uint8_t mode, uint8_t bbad, uint16_t index) const;
    uint8_t dmaTransferByteCount(uint8_t mode) const;
    uint32_t dmaAAddress(uint8_t channel) const;
    void setDmaAAddress(uint8_t channel, uint32_t address);
    uint16_t dmaSize(uint8_t channel) const;
    void setDmaSize(uint8_t channel, uint16_t size);
    uint16_t hdmaTableAddress(uint8_t channel) const;
    void setHdmaTableAddress(uint8_t channel, uint16_t address);
    uint16_t hdmaIndirectAddress(uint8_t channel) const;
    void setHdmaIndirectAddress(uint8_t channel, uint16_t address);
    void latchJoypad();
    uint8_t readJoypadSerial();
    void storeJoypadAutoReadResult();
    uint8_t readApuPort(uint16_t address);
    void writeApuPort(uint16_t address, uint8_t value);

    const cpu_r5a22::CPU* cpuTiming = nullptr;
    std::array<uint8_t, 128 * 1024> wram{};
    std::array<uint8_t, 0x4000> mmio{};
    apu::Apu apuCore;
    gsu::Gsu gsuCore;
    bool gsuPresent = false;
    sa1::Sa1 sa1Core;
    bool sa1Present = false;
    dsp::NecDsp dspCore;
    bool dspPresent = false;
    bool dspHiRomMap = false; // register window: HiROM $6000-$7FFF vs LoROM $8000-$FFFF
    sdd1::Sdd1 sdd1Core;
    bool sdd1Present = false;
    bool sdd1DmaActive = false; // current DMA channel reads from the decompressor
    CartridgeRom cart;
    ppu::Ppu* ppuCore = nullptr;
    TraceListener* traceListener = nullptr;
    uint8_t openBusValue = 0xff;
    uint32_t wramPortAddr = 0;
    uint8_t wrmpya = 0xff;
    uint8_t wrmpyb = 0xff;
    uint16_t rdmpy = 0;
    uint16_t wrdiva = 0;
    uint8_t wrdivb = 0xff;
    uint16_t rddiv = 0;
    bool nmiEnable = false;
    bool nmiFlag = false;
    bool vblankActive = false;
    bool joypadStrobe = false;
    bool joypadAutoReadEnable = false;
    bool joypadAutoReadBusy = false;
    uint32_t joypadAutoReadCyclesRemaining = 0;
    bool nmiEdge = false;
    uint16_t joypadCurrentState = 0;
    uint16_t joypadLatchedState = 0;
    uint8_t joypadReadIndex = 0;
    uint8_t hvIrqMode = 0; // NMITIMEN bits 5-4: 0=off, 1=H, 2=V, 3=H+V
    uint16_t irqHTimeVal = 0;
    uint16_t irqVTimeVal = 0;
    bool irqPending = false;

    struct HdmaChannel {
        bool active = false;
        bool repeat = false;
        bool doTransfer = false;
        uint8_t lineCounter = 0;
    };
    std::array<HdmaChannel, 8> hdma{};

    uint32_t pendingDmaDots = 0;

    ApuPortLog apuPortLog;
    GameFlagLog gameFlagLog;
};

} // namespace snesquik::bus
