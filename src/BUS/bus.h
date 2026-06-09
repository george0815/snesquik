#pragma once

#include "CPU_R5A22/core.h"
#include "S-PPU/ppu.h"

#include <array>
#include <cstddef>
#include <cstdint>
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

class CartridgeRom {
public:
    CartridgeRom() = default;
    CartridgeRom(std::vector<uint8_t> bytes, CartridgeMap map);

    void load(std::vector<uint8_t> bytes, CartridgeMap map, size_t ramSize = 0);
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
};

class SnesBus final : public cpu_r5a22::Bus {
public:
    uint8_t read8(uint32_t address) override;
    void write8(uint32_t address, uint8_t value) override;

    void attachCartridge(std::vector<uint8_t> rom, CartridgeMap map, size_t ramSize = 0);
    void attachPpu(ppu::Ppu* ppuCore);
    void setTraceListener(TraceListener* listener);
    const CartridgeRom& cartridge() const { return cart; }

    void beginFrame();
    void runDma(uint8_t channelMask);
    void runHdmaScanline();
    void setVblank(bool active);
    bool nmiEnabled() const { return nmiEnable; }

    void setJoypadState(uint16_t state);
    uint16_t joypadState() const { return joypadCurrentState; }
    void beginJoypadAutoRead();
    void finishJoypadAutoRead();
    bool joypadAutoReadEnabled() const { return joypadAutoReadEnable; }

    uint8_t readWram(uint32_t offset) const;
    void writeWram(uint32_t offset, uint8_t value);

    uint8_t readMmio(uint16_t address) const;
    void writeMmio(uint16_t address, uint8_t value);

    uint8_t openBus() const { return openBusValue; }

private:
    std::optional<size_t> mapWram(uint32_t address) const;
    std::optional<size_t> mapMmio(uint32_t address) const;
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
    uint8_t readApuPort(uint16_t address) const;
    void writeApuPort(uint16_t address, uint8_t value);

    std::array<uint8_t, 128 * 1024> wram{};
    std::array<uint8_t, 0x4000> mmio{};
    std::array<uint8_t, 4> apuCpuToSmp{};
    std::array<uint8_t, 4> apuSmpToCpu{0xaa, 0xbb, 0x00, 0x00};
    CartridgeRom cart;
    ppu::Ppu* ppuCore = nullptr;
    TraceListener* traceListener = nullptr;
    uint8_t openBusValue = 0xff;
    bool nmiEnable = false;
    bool nmiFlag = false;
    bool vblankActive = false;
    bool joypadStrobe = false;
    bool joypadAutoReadEnable = false;
    bool joypadAutoReadBusy = false;
    uint16_t joypadCurrentState = 0;
    uint16_t joypadLatchedState = 0;
    uint8_t joypadReadIndex = 0;
    bool apuIplTransferStarted = false;

    struct HdmaChannel {
        bool active = false;
        bool repeat = false;
        bool doTransfer = false;
        uint8_t lineCounter = 0;
    };
    std::array<HdmaChannel, 8> hdma{};
};

} // namespace snesquik::bus
