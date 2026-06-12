#include "BUS/bus.h"

namespace snesquik::bus {

namespace {

constexpr uint32_t mask24(uint32_t address)
{
    return address & 0x00ffffff;
}

bool hasLowerHalfCartridgeMirror(uint8_t bank)
{
    return (bank >= 0x40 && bank <= 0x7d) || bank >= 0xc0;
}

std::optional<size_t> wrapRomOffset(size_t offset, size_t size)
{
    if (size == 0) {
        return std::nullopt;
    }
    return offset % size;
}

} // namespace

CartridgeRom::CartridgeRom(std::vector<uint8_t> bytes, CartridgeMap map)
{
    load(std::move(bytes), map);
}

void CartridgeRom::load(std::vector<uint8_t> bytes, CartridgeMap map, size_t ramSize)
{
    rom = std::move(bytes);
    sram.assign(ramSize, 0xff);
    mapping = map;
}

uint8_t CartridgeRom::read(size_t offset) const
{
    if (rom.empty()) {
        return 0xff;
    }
    return rom[offset % rom.size()];
}

uint8_t CartridgeRom::readSram(size_t offset) const
{
    if (sram.empty()) {
        return 0xff;
    }
    return sram[offset % sram.size()];
}

void CartridgeRom::writeSram(size_t offset, uint8_t value)
{
    if (sram.empty()) {
        return;
    }
    sram[offset % sram.size()] = value;
}

std::optional<size_t> CartridgeRom::mapCpuAddress(uint32_t address) const
{
    if (rom.empty()) {
        return std::nullopt;
    }

    address = mask24(address);
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);

    switch (mapping) {
    case CartridgeMap::LoROM:
        if (offset >= 0x8000 || hasLowerHalfCartridgeMirror(bank)) {
            return wrapRomOffset(((bank & 0x7f) * 0x8000) + (offset & 0x7fff), rom.size());
        }
        return std::nullopt;

    case CartridgeMap::HiROM:
        if (bank >= 0xc0 || (bank >= 0x40 && bank <= 0x7d)) {
            return wrapRomOffset(((bank & 0x3f) * 0x10000) + offset, rom.size());
        }
        if (offset >= 0x8000) {
            return wrapRomOffset(((bank & 0x3f) * 0x10000) + offset, rom.size());
        }
        return std::nullopt;

    case CartridgeMap::ExHiROM:
        if (bank >= 0xc0) {
            return wrapRomOffset(((bank & 0x3f) * 0x10000) + offset, rom.size());
        }
        if (bank >= 0x40 && bank <= 0x7d) {
            return wrapRomOffset(0x400000 + ((bank & 0x3f) * 0x10000) + offset, rom.size());
        }
        if (offset >= 0x8000) {
            return wrapRomOffset(0x400000 + ((bank & 0x3f) * 0x10000) + offset, rom.size());
        }
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<size_t> CartridgeRom::mapSramAddress(uint32_t address) const
{
    if (sram.empty()) {
        return std::nullopt;
    }

    address = mask24(address);
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);

    switch (mapping) {
    case CartridgeMap::LoROM:
        if ((bank >= 0x70 && bank <= 0x7d) || bank >= 0xf0) {
            if (offset <= 0x7fff) {
                return (((bank & 0x0f) * 0x8000) + offset) % sram.size();
            }
        }
        if ((bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf)) && offset >= 0x6000 && offset <= 0x7fff) {
            return (((bank & 0x3f) * 0x2000) + (offset - 0x6000)) % sram.size();
        }
        return std::nullopt;

    case CartridgeMap::HiROM:
    case CartridgeMap::ExHiROM:
        if ((bank >= 0x20 && bank <= 0x3f) || (bank >= 0xa0 && bank <= 0xbf)) {
            if (offset >= 0x6000 && offset <= 0x7fff) {
                return (((bank & 0x1f) * 0x2000) + (offset - 0x6000)) % sram.size();
            }
        }
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<size_t> CartridgeRom::headerOffset(CartridgeMap map, size_t romSize)
{
    size_t offset = 0;
    switch (map) {
    case CartridgeMap::LoROM:
        offset = 0x007fc0;
        break;
    case CartridgeMap::HiROM:
        offset = 0x00ffc0;
        break;
    case CartridgeMap::ExHiROM:
        offset = 0x40ffc0;
        break;
    }

    if (offset + 0x20 > romSize) {
        return std::nullopt;
    }
    return offset;
}


uint8_t SnesBus::read8(uint32_t address)
{
    address = mask24(address);
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);

    if (bank == 0x7e || bank == 0x7f) {
        openBusValue = wram[((bank - 0x7e) << 16) + offset];
        return openBusValue;
    }

    if (bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf)) {
        if (offset <= 0x1fff) {
            openBusValue = wram[offset];
            return openBusValue;
        }
        if (offset >= 0x8000) {
            if (auto mapped = cart.mapCpuAddress(address)) {
                openBusValue = cart.read(*mapped);
                return openBusValue;
            }
        }
    }

    if (auto mapped = mapWram(address)) {
        openBusValue = wram[*mapped];
        return openBusValue;
    }

    if (auto mapped = mapMmio(address)) {
        const uint16_t mmioAddress = static_cast<uint16_t>(0x2000 + *mapped);
        if (ppuCore && mmioAddress >= 0x2100 && mmioAddress <= 0x213f) {
            openBusValue = ppuCore->readRegister(mmioAddress);
        } else if (mmioAddress >= 0x2140 && mmioAddress <= 0x2143) {
            openBusValue = readApuPort(mmioAddress);
        } else if (mmioAddress == 0x2180) {
            openBusValue = wram[wramPortAddr & 0x1ffff];
            wramPortAddr = (wramPortAddr + 1) & 0x1ffff;
        } else if (mmioAddress == 0x4016) {
            openBusValue = readJoypadSerial();
        } else if (mmioAddress == 0x4017) {
            openBusValue = 0x00;
        } else if (mmioAddress == 0x4210) {
            openBusValue = static_cast<uint8_t>((nmiFlag ? 0x80 : 0x00) | 0x02);
            nmiFlag = false;
        } else if (mmioAddress == 0x4211) {
            openBusValue = static_cast<uint8_t>((irqPending ? 0x80 : 0x00) | 0x01);
            irqPending = false;
        } else if (mmioAddress == 0x4212) {
            const bool hblank = ppuCore && ppuCore->hblank();
            openBusValue = static_cast<uint8_t>((vblankActive ? 0x80 : 0x00) | (hblank ? 0x40 : 0x00) | (joypadAutoReadBusy ? 0x01 : 0x00));
        } else if (mmioAddress == 0x4214) {
            openBusValue = static_cast<uint8_t>(rddiv);
        } else if (mmioAddress == 0x4215) {
            openBusValue = static_cast<uint8_t>(rddiv >> 8);
        } else if (mmioAddress == 0x4216) {
            openBusValue = static_cast<uint8_t>(rdmpy);
        } else if (mmioAddress == 0x4217) {
            openBusValue = static_cast<uint8_t>(rdmpy >> 8);
        } else if (mmioAddress == 0x4218) {
            openBusValue = static_cast<uint8_t>(joypadLatchedState);
        } else if (mmioAddress == 0x4219) {
            openBusValue = static_cast<uint8_t>(joypadLatchedState >> 8);
        } else {
            openBusValue = mmio[*mapped];
        }
        if (traceListener) {
            traceListener->mmioRead(mmioAddress, openBusValue);
        }
        return openBusValue;
    }

    if (auto mapped = cart.mapSramAddress(address)) {
        openBusValue = cart.readSram(*mapped);
        return openBusValue;
    }

    if (auto mapped = cart.mapCpuAddress(address)) {
        openBusValue = cart.read(*mapped);
        return openBusValue;
    }

    return openBusValue;
}

void SnesBus::write8(uint32_t address, uint8_t value)
{
    address = mask24(address);
    openBusValue = value;

    if (auto mapped = mapWram(address)) {
        wram[*mapped] = value;
        if (*mapped == 0x05B4) {
            gameFlagLog.writeCount++;
            gameFlagLog.lastValueWritten = value;
        }
        return;
    }

    if (auto mapped = mapMmio(address)) {
        const uint16_t mmioAddress = static_cast<uint16_t>(0x2000 + *mapped);
        if (traceListener) {
            traceListener->mmioWrite(mmioAddress, value);
        }
        if (mmioAddress == 0x4016) {
            const bool newStrobe = (value & 0x01) != 0;
            if (newStrobe || joypadStrobe != newStrobe) {
                latchJoypad();
            }
            joypadStrobe = newStrobe;
            mmio[*mapped] = value;
            return;
        }
        if (mmioAddress == 0x4200) {
            const bool wasNmiEnabled = nmiEnable;
            mmio[*mapped] = value;
            nmiEnable = (value & 0x80) != 0;
            hvIrqEnabled = (value & 0x10) != 0;
            joypadAutoReadEnable = (value & 0x01) != 0;
            if (!wasNmiEnabled && nmiEnable && nmiFlag) {
                nmiEdge = true;
            }
            return;
        }
        if (mmioAddress == 0x4207) {
            mmio[*mapped] = value;
            irqHTimeVal = static_cast<uint16_t>((irqHTimeVal & 0x100) | value);
            return;
        }
        if (mmioAddress == 0x4208) {
            mmio[*mapped] = value;
            irqHTimeVal = static_cast<uint16_t>((irqHTimeVal & 0x0ff) | ((value & 0x01) << 8));
            return;
        }
        if (mmioAddress == 0x4209) {
            mmio[*mapped] = value;
            irqVTimeVal = static_cast<uint16_t>((irqVTimeVal & 0x100) | value);
            return;
        }
        if (mmioAddress == 0x420a) {
            mmio[*mapped] = value;
            irqVTimeVal = static_cast<uint16_t>((irqVTimeVal & 0x0ff) | ((value & 0x01) << 8));
            return;
        }
        if (mmioAddress == 0x420b) {
            mmio[*mapped] = value;
            runDma(value);
            return;
        }
        if (mmioAddress == 0x420c) {
            mmio[*mapped] = value;
            return;
        }
        if (mmioAddress == 0x4202) {
            mmio[*mapped] = value;
            wrmpya = value;
            return;
        }
        if (mmioAddress == 0x4203) {
            mmio[*mapped] = value;
            wrmpyb = value;
            rdmpy = static_cast<uint16_t>(wrmpya * wrmpyb);
            return;
        }
        if (mmioAddress == 0x4204) {
            mmio[*mapped] = value;
            wrdiva = static_cast<uint16_t>((wrdiva & 0xff00) | value);
            return;
        }
        if (mmioAddress == 0x4205) {
            mmio[*mapped] = value;
            wrdiva = static_cast<uint16_t>((wrdiva & 0x00ff) | (static_cast<uint16_t>(value) << 8));
            return;
        }
        if (mmioAddress == 0x4206) {
            mmio[*mapped] = value;
            wrdivb = value;
            if (wrdivb == 0) {
                rddiv = 0xffff;
                rdmpy = wrdiva;
            } else {
                rddiv = static_cast<uint16_t>(wrdiva / wrdivb);
                rdmpy = static_cast<uint16_t>(wrdiva % wrdivb);
            }
            return;
        }
        if (mmioAddress == 0x2180) {
            wram[wramPortAddr & 0x1ffff] = value;
            wramPortAddr = (wramPortAddr + 1) & 0x1ffff;
            return;
        }
        if (mmioAddress == 0x2181) {
            wramPortAddr = (wramPortAddr & 0x1ff00) | value;
            return;
        }
        if (mmioAddress == 0x2182) {
            wramPortAddr = (wramPortAddr & 0x100ff) | (static_cast<uint32_t>(value) << 8);
            return;
        }
        if (mmioAddress == 0x2183) {
            wramPortAddr = (wramPortAddr & 0x0ffff) | (static_cast<uint32_t>(value & 0x01) << 16);
            return;
        }
        if (mmioAddress >= 0x2140 && mmioAddress <= 0x2143) {
            writeApuPort(mmioAddress, value);
            return;
        }
        if (ppuCore && mmioAddress >= 0x2100 && mmioAddress <= 0x213f) {
            ppuCore->writeRegister(mmioAddress, value);
            return;
        }
        mmio[*mapped] = value;
        return;
    }

    if (auto mapped = cart.mapSramAddress(address)) {
        cart.writeSram(*mapped, value);
    }
}

void SnesBus::attachCartridge(std::vector<uint8_t> rom, CartridgeMap map, size_t ramSize)
{
    cart.load(std::move(rom), map, ramSize);
}

void SnesBus::attachPpu(ppu::Ppu* ppuCore)
{
    this->ppuCore = ppuCore;
}

void SnesBus::setTraceListener(TraceListener* listener)
{
    traceListener = listener;
}

void SnesBus::beginFrame()
{
    gameFlagLog.reset();
    const uint8_t channelMask = mmio[0x420c - 0x2000];
    for (uint8_t channel = 0; channel < 8; ++channel) {
        HdmaChannel& state = hdma[channel];
        state = {};
        if ((channelMask & (1u << channel)) == 0) {
            continue;
        }

        state.active = true;
        setHdmaTableAddress(channel, static_cast<uint16_t>(mmio[0x4302 + channel * 0x10 - 0x2000]
            | (mmio[0x4303 + channel * 0x10 - 0x2000] << 8)));
        reloadHdma(channel);
    }
}

void SnesBus::runDma(uint8_t channelMask)
{
    for (uint8_t channel = 0; channel < 8; ++channel) {
        if ((channelMask & (1u << channel)) == 0) {
            continue;
        }

        uint32_t size = dmaSize(channel);
        if (size == 0) {
            size = 0x10000;
        }
        if (traceListener) {
            traceListener->dmaStart(channel, static_cast<uint16_t>(size));
        }
        for (uint32_t index = 0; index < size; ++index) {
            transferDmaByte(channel, static_cast<uint16_t>(index));
        }
        setDmaSize(channel, 0);

        pendingDmaDots += size * 2;
    }
}

uint32_t SnesBus::consumeDmaDots()
{
    const uint32_t dots = pendingDmaDots;
    pendingDmaDots = 0;
    return dots;
}

void SnesBus::runHdmaScanline()
{
    const uint8_t channelMask = mmio[0x420c - 0x2000];
    for (uint8_t channel = 0; channel < 8; ++channel) {
        HdmaChannel& state = hdma[channel];
        if ((channelMask & (1u << channel)) == 0 || !state.active) {
            continue;
        }

        if (state.lineCounter == 0) {
            reloadHdma(channel);
            if (!state.active) {
                continue;
            }
        }

        if (state.doTransfer) {
            transferHdma(channel);
            if (state.repeat) {
                state.doTransfer = false;
            }
        }

        --state.lineCounter;
    }

    pendingDmaDots += 16;
}

void SnesBus::checkIrq(uint16_t h, uint16_t v)
{
    if (hvIrqEnabled && h == irqHTimeVal && v == irqVTimeVal) {
        irqPending = true;
    }
}

void SnesBus::checkIrqCrossing(uint16_t prevH, uint16_t prevV, uint16_t currH, uint16_t currV)
{
    if (!hvIrqEnabled) {
        return;
    }

    constexpr uint32_t dotsPerScanline = 341;
    const uint32_t prevPos = static_cast<uint32_t>(prevV) * dotsPerScanline + prevH;
    const uint32_t currPos = static_cast<uint32_t>(currV) * dotsPerScanline + currH;
    const uint32_t targetPos = static_cast<uint32_t>(irqVTimeVal) * dotsPerScanline + irqHTimeVal;

    if (currPos >= prevPos) {
        if (targetPos >= prevPos && targetPos <= currPos) {
            irqPending = true;
        }
    } else {
        if (targetPos >= prevPos || targetPos <= currPos) {
            irqPending = true;
        }
    }
}

void SnesBus::setVblank(bool active)
{
    if (active && !vblankActive) {
        nmiFlag = true;
    }
    vblankActive = active;
    if (traceListener) {
        traceListener->vblank(active);
    }
}

void SnesBus::setJoypadState(uint16_t state)
{
    joypadCurrentState = state;
    if (joypadStrobe) {
        latchJoypad();
    }
}

void SnesBus::beginJoypadAutoRead()
{
    if (joypadAutoReadEnable) {
        joypadAutoReadBusy = true;
        joypadAutoReadCyclesRemaining = 152;
    }
}

void SnesBus::finishJoypadAutoRead()
{
    if (joypadAutoReadEnable) {
        joypadLatchedState = joypadCurrentState;
        storeJoypadAutoReadResult();
    }
    joypadAutoReadBusy = false;
    joypadAutoReadCyclesRemaining = 0;
}

void SnesBus::tickJoypadAutoRead(uint32_t cpuCycles)
{
    if (joypadAutoReadBusy && joypadAutoReadCyclesRemaining > 0) {
        if (cpuCycles >= joypadAutoReadCyclesRemaining) {
            finishJoypadAutoRead();
        } else {
            joypadAutoReadCyclesRemaining -= cpuCycles;
        }
    }
}

uint8_t SnesBus::readWram(uint32_t offset) const
{
    return wram[offset % wram.size()];
}

void SnesBus::writeWram(uint32_t offset, uint8_t value)
{
    wram[offset % wram.size()] = value;
}

uint8_t SnesBus::readMmio(uint16_t address) const
{
    if (address < 0x2000 || address > 0x5fff) {
        return 0xff;
    }
    return mmio[address - 0x2000];
}

void SnesBus::writeMmio(uint16_t address, uint8_t value)
{
    if (address >= 0x2000 && address <= 0x5fff) {
        mmio[address - 0x2000] = value;
    }
}

std::optional<size_t> SnesBus::mapWram(uint32_t address) const
{
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);

    if (bank == 0x7e || bank == 0x7f) {
        return ((bank - 0x7e) * 0x10000) + offset;
    }

    if ((bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf)) && offset <= 0x1fff) {
        return offset;
    }

    return std::nullopt;
}

std::optional<size_t> SnesBus::mapMmio(uint32_t address) const
{
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);

    if ((bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf)) && offset >= 0x2000 && offset <= 0x5fff) {
        return offset - 0x2000;
    }

    return std::nullopt;
}

uint8_t SnesBus::readRaw(uint32_t address)
{
    address = mask24(address);
    if (auto mapped = mapWram(address)) {
        return wram[*mapped];
    }
    if (auto mapped = mapMmio(address)) {
        const uint16_t mmioAddress = static_cast<uint16_t>(0x2000 + *mapped);
        if (ppuCore && mmioAddress >= 0x2100 && mmioAddress <= 0x213f) {
            return ppuCore->readRegister(mmioAddress);
        }
        if (mmioAddress >= 0x2140 && mmioAddress <= 0x2143) {
            return readApuPort(mmioAddress);
        }
        if (mmioAddress == 0x2180) {
            uint8_t v = wram[wramPortAddr & 0x1ffff];
            wramPortAddr = (wramPortAddr + 1) & 0x1ffff;
            return v;
        }
        if (mmioAddress == 0x4214) {
            return static_cast<uint8_t>(rddiv);
        }
        if (mmioAddress == 0x4215) {
            return static_cast<uint8_t>(rddiv >> 8);
        }
        if (mmioAddress == 0x4216) {
            return static_cast<uint8_t>(rdmpy);
        }
        if (mmioAddress == 0x4217) {
            return static_cast<uint8_t>(rdmpy >> 8);
        }
        if (mmioAddress == 0x4218) {
            return static_cast<uint8_t>(joypadLatchedState);
        }
        if (mmioAddress == 0x4219) {
            return static_cast<uint8_t>(joypadLatchedState >> 8);
        }
        return mmio[*mapped];
    }
    if (auto mapped = cart.mapSramAddress(address)) {
        return cart.readSram(*mapped);
    }
    if (auto mapped = cart.mapCpuAddress(address)) {
        return cart.read(*mapped);
    }
    return openBusValue;
}

void SnesBus::writeRaw(uint32_t address, uint8_t value)
{
    address = mask24(address);
    if (auto mapped = mapWram(address)) {
        wram[*mapped] = value;
        return;
    }
    if (auto mapped = mapMmio(address)) {
        const uint16_t mmioAddress = static_cast<uint16_t>(0x2000 + *mapped);
        if (ppuCore && mmioAddress >= 0x2100 && mmioAddress <= 0x213f) {
            ppuCore->writeRegister(mmioAddress, value);
            return;
        }
        if (mmioAddress >= 0x2140 && mmioAddress <= 0x2143) {
            writeApuPort(mmioAddress, value);
            return;
        }
        if (mmioAddress == 0x2180) {
            wram[wramPortAddr & 0x1ffff] = value;
            wramPortAddr = (wramPortAddr + 1) & 0x1ffff;
            return;
        }
        if (mmioAddress == 0x2181) {
            wramPortAddr = (wramPortAddr & 0x1ff00) | value;
            return;
        }
        if (mmioAddress == 0x2182) {
            wramPortAddr = (wramPortAddr & 0x100ff) | (static_cast<uint32_t>(value) << 8);
            return;
        }
        if (mmioAddress == 0x2183) {
            wramPortAddr = (wramPortAddr & 0x0ffff) | (static_cast<uint32_t>(value & 0x01) << 16);
            return;
        }
        if (mmioAddress == 0x4202) {
            mmio[*mapped] = value;
            wrmpya = value;
            return;
        }
        if (mmioAddress == 0x4203) {
            mmio[*mapped] = value;
            wrmpyb = value;
            rdmpy = static_cast<uint16_t>(wrmpya * wrmpyb);
            return;
        }
        if (mmioAddress == 0x4204) {
            mmio[*mapped] = value;
            wrdiva = static_cast<uint16_t>((wrdiva & 0xff00) | value);
            return;
        }
        if (mmioAddress == 0x4205) {
            mmio[*mapped] = value;
            wrdiva = static_cast<uint16_t>((wrdiva & 0x00ff) | (static_cast<uint16_t>(value) << 8));
            return;
        }
        if (mmioAddress == 0x4206) {
            mmio[*mapped] = value;
            wrdivb = value;
            if (wrdivb == 0) {
                rddiv = 0xffff;
                rdmpy = wrdiva;
            } else {
                rddiv = static_cast<uint16_t>(wrdiva / wrdivb);
                rdmpy = static_cast<uint16_t>(wrdiva % wrdivb);
            }
            return;
        }
        mmio[*mapped] = value;
        return;
    }

    if (auto mapped = cart.mapSramAddress(address)) {
        cart.writeSram(*mapped, value);
    }
}

void SnesBus::transferDmaByte(uint8_t channel, uint16_t index)
{
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    const uint8_t dmap = mmio[base - 0x2000];
    const uint8_t bbad = mmio[base + 1 - 0x2000];
    const uint8_t baddr = dmaBAddress(dmap & 0x07, bbad, index);
    uint32_t aaddr = dmaAAddress(channel);

    if ((dmap & 0x80) != 0) {
        const uint8_t value = readRaw(0x002100 + baddr);
        writeRaw(aaddr, value);
    } else {
        const uint8_t value = readRaw(aaddr);

        if (traceListener && ppuCore &&
            (bbad == 0x18 || bbad == 0x19 || bbad == 0x04 || bbad == 0x22)) {
            traceListener->dmaTransfer(
                channel,
                aaddr,
                bbad,
                value,
                ppuCore->vramAddress
            );
        }

        writeRaw(0x002100 + baddr, value);
    }

    if ((dmap & 0x08) == 0) {
        if ((dmap & 0x10) != 0) {
            --aaddr;
        } else {
            ++aaddr;
        }
        setDmaAAddress(channel, aaddr);
    }
}

void SnesBus::reloadHdma(uint8_t channel)
{
    HdmaChannel& state = hdma[channel];
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    uint16_t table = hdmaTableAddress(channel);
    const uint8_t bank = mmio[base + 4 - 0x2000];
    const uint8_t line = readRaw((static_cast<uint32_t>(bank) << 16) | table);
    setHdmaTableAddress(channel, static_cast<uint16_t>(table + 1));

    if (line == 0) {
        state.active = false;
        state.lineCounter = 0;
        state.doTransfer = false;
        return;
    }

    state.repeat = (line & 0x80) != 0;
    state.lineCounter = line & 0x7f;
    if (state.lineCounter == 0) {
        state.lineCounter = 128;
    }
    state.doTransfer = true;

    const uint8_t dmap = mmio[base - 0x2000];
    if ((dmap & 0x40) != 0) {
        table = hdmaTableAddress(channel);
        const uint16_t low = readRaw((static_cast<uint32_t>(bank) << 16) | table);
        const uint16_t high = readRaw((static_cast<uint32_t>(bank) << 16) | static_cast<uint16_t>(table + 1));
        setHdmaIndirectAddress(channel, static_cast<uint16_t>(low | (high << 8)));
        setHdmaTableAddress(channel, static_cast<uint16_t>(table + 2));
    }
}

void SnesBus::transferHdma(uint8_t channel)
{
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    const uint8_t dmap = mmio[base - 0x2000];
    const uint8_t bbad = mmio[base + 1 - 0x2000];
    const uint8_t bytes = dmaTransferByteCount(dmap & 0x07);

    for (uint8_t index = 0; index < bytes; ++index) {
        uint8_t value = 0;
        if ((dmap & 0x40) != 0) {
            const uint8_t bank = mmio[base + 7 - 0x2000];
            const uint16_t indirect = hdmaIndirectAddress(channel);
            value = readRaw((static_cast<uint32_t>(bank) << 16) | indirect);
            setHdmaIndirectAddress(channel, static_cast<uint16_t>(indirect + 1));
        } else {
            const uint8_t bank = mmio[base + 4 - 0x2000];
            const uint16_t table = hdmaTableAddress(channel);
            value = readRaw((static_cast<uint32_t>(bank) << 16) | table);
            setHdmaTableAddress(channel, static_cast<uint16_t>(table + 1));
        }
        const uint16_t target = static_cast<uint16_t>(
            0x2100 + dmaBAddress(dmap & 0x07, bbad, index)
        );

        if (traceListener) {
            traceListener->hdmaWrite(channel, target, value);
        }

        writeRaw(target, value);
    }
}

uint8_t SnesBus::dmaBAddress(uint8_t mode, uint8_t bbad, uint16_t index) const
{
    static constexpr uint8_t sequence[8][4] = {
        {0, 0, 0, 0},
        {0, 1, 0, 1},
        {0, 0, 0, 0},
        {0, 0, 1, 1},
        {0, 1, 2, 3},
        {0, 1, 0, 1},
        {0, 0, 0, 0},
        {0, 0, 1, 1},
    };
    return static_cast<uint8_t>(bbad + sequence[mode & 0x07][index & 0x03]);
}

uint8_t SnesBus::dmaTransferByteCount(uint8_t mode) const
{
    static constexpr uint8_t counts[8] = {1, 2, 2, 4, 4, 4, 2, 4};
    return counts[mode & 0x07];
}

uint32_t SnesBus::dmaAAddress(uint8_t channel) const
{
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    const uint32_t low = mmio[base + 2 - 0x2000];
    const uint32_t high = mmio[base + 3 - 0x2000];
    const uint32_t bank = mmio[base + 4 - 0x2000];
    return (bank << 16) | (high << 8) | low;
}

void SnesBus::setDmaAAddress(uint8_t channel, uint32_t address)
{
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    mmio[base + 2 - 0x2000] = static_cast<uint8_t>(address);
    mmio[base + 3 - 0x2000] = static_cast<uint8_t>(address >> 8);
    mmio[base + 4 - 0x2000] = static_cast<uint8_t>(address >> 16);
}

uint16_t SnesBus::dmaSize(uint8_t channel) const
{
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    return static_cast<uint16_t>(mmio[base + 5 - 0x2000] | (mmio[base + 6 - 0x2000] << 8));
}

uint16_t SnesBus::hdmaTableAddress(uint8_t channel) const
{
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    return static_cast<uint16_t>(mmio[base + 8 - 0x2000] | (mmio[base + 9 - 0x2000] << 8));
}

void SnesBus::setHdmaTableAddress(uint8_t channel, uint16_t address)
{
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    mmio[base + 8 - 0x2000] = static_cast<uint8_t>(address);
    mmio[base + 9 - 0x2000] = static_cast<uint8_t>(address >> 8);
}

uint16_t SnesBus::hdmaIndirectAddress(uint8_t channel) const
{
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    return static_cast<uint16_t>(mmio[base + 5 - 0x2000] | (mmio[base + 6 - 0x2000] << 8));
}

void SnesBus::setHdmaIndirectAddress(uint8_t channel, uint16_t address)
{
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    mmio[base + 5 - 0x2000] = static_cast<uint8_t>(address);
    mmio[base + 6 - 0x2000] = static_cast<uint8_t>(address >> 8);
}

void SnesBus::setDmaSize(uint8_t channel, uint16_t size)
{
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    mmio[base + 5 - 0x2000] = static_cast<uint8_t>(size);
    mmio[base + 6 - 0x2000] = static_cast<uint8_t>(size >> 8);
}

void SnesBus::latchJoypad()
{
    joypadLatchedState = joypadCurrentState;
    joypadReadIndex = 0;
    storeJoypadAutoReadResult();
}

uint8_t SnesBus::readJoypadSerial()
{
    uint8_t bit = 0;
    if (joypadReadIndex < 16) {
        bit = static_cast<uint8_t>((joypadLatchedState >> (15 - joypadReadIndex)) & 0x01);
    }
    if (!joypadStrobe && joypadReadIndex < 16) {
        ++joypadReadIndex;
    }
    return bit;
}

void SnesBus::storeJoypadAutoReadResult()
{
    mmio[0x4218 - 0x2000] = static_cast<uint8_t>(joypadLatchedState);
    mmio[0x4219 - 0x2000] = static_cast<uint8_t>(joypadLatchedState >> 8);
}

uint8_t SnesBus::readApuPort(uint16_t address)
{
    const uint8_t port = static_cast<uint8_t>((address - 0x2140) & 0x03);
    const uint8_t val = apuCore.readPort(port);
    apuPortLog.lastRead[port] = val;
    apuPortLog.readCount[port]++;
    apuPortLog.totalReads++;
    return val;
}

void SnesBus::writeApuPort(uint16_t address, uint8_t value)
{
    const uint8_t port = static_cast<uint8_t>((address - 0x2140) & 0x03);
    apuCore.writePort(port, value);
    apuPortLog.lastWrite[port] = value;
    apuPortLog.writeCount[port]++;
    apuPortLog.totalWrites++;
}

void SnesBus::initApu()
{
    apuCore.init();
}

void SnesBus::stepApu(uint32_t cpuCycles)
{
    apuCore.step(cpuCycles);
}

void SnesBus::endApuFrame()
{
    apuCore.endFrame();
}

} // namespace snesquik::bus
