#include <cstdio>
#include <cstdlib>
#include "BUS/bus.h"

#include <cstring>

namespace snesquik::bus {

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

    if (gsuPresent) {
        const uint8_t lowBank = bank & 0x7f;
        if (lowBank <= 0x3f) {
            if (offset >= 0x3000 && offset <= 0x34ff) {
                openBusValue = gsuCore.readIo(offset);
                return openBusValue;
            }
            if (offset >= 0x6000 && offset <= 0x7fff) {
                if (gsuCore.cpuCanSeeRam()) {
                    openBusValue = gsuCore.readRam(offset & 0x1fff);
                }
                return openBusValue;
            }
            if (offset >= 0x8000 && !gsuCore.cpuCanSeeRom()) {
                openBusValue = gsuCore.cpuRomConflictValue(address);
                return openBusValue;
            }
        } else if (lowBank <= 0x5f) {
            // Linear (HiROM-style) view of the game pack ROM.
            if (!gsuCore.cpuCanSeeRom()) {
                openBusValue = gsuCore.cpuRomConflictValue(address);
            } else {
                openBusValue = cart.read(address & 0x1fffff);
            }
            return openBusValue;
        } else if (lowBank <= 0x6f) {
            // $60-$6F: linear ROM continuation (mask to 2MB wraps $60->$00).
            // DOOM streams the shotgun sound off the top of $40-$5F into $60.
            if (!gsuCore.cpuCanSeeRom()) {
                openBusValue = gsuCore.cpuRomConflictValue(address);
            } else {
                openBusValue = cart.read(address & 0x1fffff);
            }
            return openBusValue;
        } else if (lowBank >= 0x70 && lowBank <= 0x7d) {
            if (gsuCore.cpuCanSeeRam()) {
                openBusValue = gsuCore.readRam(((lowBank & 1) << 16) | offset);
            }
            return openBusValue;
        }
    }

    if (sa1Present) {
        uint8_t value = 0xff;
        if (sa1MapRead(address, value)) {
            openBusValue = value;
            return openBusValue;
        }
    }

    if (dspPresent) {
        uint8_t value = 0xff;
        if (dspMapRead(address, value)) {
            openBusValue = value;
            return openBusValue;
        }
    }

    if (sdd1Present) {
        uint8_t value = 0xff;
        if (sdd1MapRead(address, value)) {
            openBusValue = value;
            return openBusValue;
        }
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
            openBusValue = static_cast<uint8_t>((irqPending ? 0x80 : 0x00) | (openBusValue & 0x7f));
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

    if (gsuPresent) {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t offset = static_cast<uint16_t>(address);
        const uint8_t lowBank = bank & 0x7f;
        if (bank != 0x7e && bank != 0x7f) {
            if (lowBank <= 0x3f) {
                if (offset >= 0x3000 && offset <= 0x34ff) {
                    gsuCore.writeIo(offset, value);
                    return;
                }
                if (offset >= 0x6000 && offset <= 0x7fff) {
                    if (gsuCore.cpuCanSeeRam()) {
                        gsuCore.writeRam(offset & 0x1fff, value);
                    }
                    return;
                }
            } else if (lowBank >= 0x70 && lowBank <= 0x7d) {
                if (gsuCore.cpuCanSeeRam()) {
                    gsuCore.writeRam(((lowBank & 1) << 16) | offset, value);
                }
                return;
            }
        }
    }

    if (sa1Present && sa1MapWrite(address, value)) {
        return;
    }

    if (dspPresent && dspMapWrite(address, value)) {
        return;
    }

    if (sdd1Present && sdd1MapWrite(address, value)) {
        return;
    }

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
            hvIrqMode = (value >> 4) & 0x03;
            if (hvIrqMode == 0) {
                // Disabling H/V-IRQ acknowledges any pending IRQ.
                irqPending = false;
            }
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
        state.lineCounter = 0;
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

        // S-DD1: a DMA channel armed via $4800 reads its (A-bus) source through
        // the decompressor. Only applies to A->B reads (CPU->PPU direction).
        const uint8_t dmap = mmio[0x4300 + channel * 0x10 - 0x2000];
        sdd1DmaActive = sdd1Present && sdd1Core.channelArmed(channel) && (dmap & 0x80) == 0;
        if (sdd1DmaActive) {
            sdd1Core.decompressBegin(dmaAAddress(channel));
        }

        for (uint32_t index = 0; index < size; ++index) {
            transferDmaByte(channel, static_cast<uint16_t>(index));
        }
        if (sdd1DmaActive) {
            sdd1Core.clearChannelArm(channel); // $4801 arms a single transfer
        }
        sdd1DmaActive = false;
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

        if (state.doTransfer) {
            transferHdma(channel);
        }

        // The full count byte (repeat bit included) is kept in lineCounter.
        // After decrementing, bit7 decides whether the next line transfers,
        // and a new entry is loaded when the low 7 bits reach zero.
        --state.lineCounter;
        state.doTransfer = (state.lineCounter & 0x80) != 0;
        reloadHdma(channel);
    }

    pendingDmaDots += 16;
}

void SnesBus::checkIrq(uint16_t h, uint16_t v)
{
    switch (hvIrqMode) {
    case 1:
        if (h == irqHTimeVal) {
            irqPending = true;
        }
        break;
    case 2:
        if (v == irqVTimeVal && h == 0) {
            irqPending = true;
        }
        break;
    case 3:
        if (h == irqHTimeVal && v == irqVTimeVal) {
            irqPending = true;
        }
        break;
    default:
        break;
    }
}

void SnesBus::checkIrqCrossing(uint16_t prevH, uint16_t prevV, uint16_t currH, uint16_t currV)
{
    if (hvIrqMode == 0) {
        return;
    }

    constexpr uint32_t dotsPerScanline = 341;
    constexpr uint32_t scanlines = 262;
    constexpr uint32_t frameDots = dotsPerScanline * scanlines;
    const uint32_t prevPos = static_cast<uint32_t>(prevV) * dotsPerScanline + prevH;
    const uint32_t currPos = static_cast<uint32_t>(currV) * dotsPerScanline + currH;
    // Dots advanced this step; the trigger window is (prevPos, currPos].
    const uint32_t span = (currPos + frameDots - prevPos) % frameDots;

    if (hvIrqMode == 1) {
        // H-IRQ fires at H=HTIME on every scanline.
        if (irqHTimeVal >= dotsPerScanline) {
            return;
        }
        uint32_t delta = (irqHTimeVal + dotsPerScanline - (prevPos % dotsPerScanline)) % dotsPerScanline;
        if (delta == 0) {
            delta = dotsPerScanline;
        }
        if (delta <= span) {
            irqPending = true;
        }
        return;
    }

    // V-IRQ fires at the start of line VTIME; H+V at (VTIME, HTIME).
    const uint32_t targetH = (hvIrqMode == 2) ? 0 : irqHTimeVal;
    const uint32_t targetPos = static_cast<uint32_t>(irqVTimeVal) * dotsPerScanline + targetH;
    if (targetPos >= frameDots) {
        return;
    }
    uint32_t delta = (targetPos + frameDots - prevPos) % frameDots;
    if (delta == 0) {
        delta = frameDots;
    }
    if (delta <= span) {
        irqPending = true;
    }
}

void SnesBus::setVblank(bool active)
{
    if (active && !vblankActive) {
        nmiFlag = true;
    }
    if (!active && vblankActive) {
        // RDNMI bit 7 is cleared automatically at the end of vblank.
        nmiFlag = false;
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

namespace {

uint16_t maskForButton(ControllerButton button)
{
    switch (button) {
    case ControllerButton::B:      return JoypadB;
    case ControllerButton::Y:      return JoypadY;
    case ControllerButton::Select: return JoypadSelect;
    case ControllerButton::Start:  return JoypadStart;
    case ControllerButton::Up:     return JoypadUp;
    case ControllerButton::Down:   return JoypadDown;
    case ControllerButton::Left:   return JoypadLeft;
    case ControllerButton::Right:  return JoypadRight;
    case ControllerButton::A:      return JoypadA;
    case ControllerButton::X:      return JoypadX;
    case ControllerButton::L:      return JoypadL;
    case ControllerButton::R:      return JoypadR;
    }
    return 0;
}

} // namespace

void SnesBus::setButton(ControllerButton button, bool pressed)
{
    const uint16_t mask = maskForButton(button);
    if (pressed) {
        joypadCurrentState |= mask;
    } else {
        joypadCurrentState &= static_cast<uint16_t>(~mask);
    }
    if (joypadStrobe) {
        latchJoypad();
    }
}

void SnesBus::beginJoypadAutoRead()
{
    if (joypadAutoReadEnable) {
        joypadAutoReadBusy = true;
        // Auto-read takes 4224 master clocks (~3 scanlines); cycles here are
        // ~6 master clocks each.
        joypadAutoReadCyclesRemaining = 704;
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
    if (gsuPresent) {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t offset = static_cast<uint16_t>(address);
        const uint8_t lowBank = bank & 0x7f;
        if (bank != 0x7e && bank != 0x7f) {
            if (lowBank <= 0x3f) {
                if (offset >= 0x3000 && offset <= 0x34ff) {
                    return gsuCore.readIo(offset);
                }
                if (offset >= 0x6000 && offset <= 0x7fff) {
                    return gsuCore.cpuCanSeeRam() ? gsuCore.readRam(offset & 0x1fff) : openBusValue;
                }
                if (offset >= 0x8000 && !gsuCore.cpuCanSeeRom()) {
                    return gsuCore.cpuRomConflictValue(address);
                }
            } else if (lowBank <= 0x5f) {
                // Linear (HiROM-style) ROM view.
                if (!gsuCore.cpuCanSeeRom()) {
                    return gsuCore.cpuRomConflictValue(address);
                }
                return cart.read(address & 0x1fffff);
            } else if (lowBank <= 0x6f) {
                // $60-$6F: linear ROM continuation (see read8).
                if (!gsuCore.cpuCanSeeRom()) {
                    return gsuCore.cpuRomConflictValue(address);
                }
                return cart.read(address & 0x1fffff);
            } else if (lowBank >= 0x70 && lowBank <= 0x7d) {
                return gsuCore.cpuCanSeeRam() ? gsuCore.readRam(((lowBank & 1) << 16) | offset)
                                              : openBusValue;
            }
        }
    }
    if (sa1Present) {
        uint8_t value = 0xff;
        if (sa1MapRead(address, value)) {
            return value;
        }
    }
    if (dspPresent) {
        uint8_t value = 0xff;
        if (dspMapRead(address, value)) {
            return value;
        }
    }
    if (sdd1Present) {
        uint8_t value = 0xff;
        if (sdd1MapRead(address, value)) {
            return value;
        }
    }
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
    if (gsuPresent) {
        const uint8_t bank = static_cast<uint8_t>(address >> 16);
        const uint16_t offset = static_cast<uint16_t>(address);
        const uint8_t lowBank = bank & 0x7f;
        if (bank != 0x7e && bank != 0x7f) {
            if (lowBank <= 0x3f) {
                if (offset >= 0x3000 && offset <= 0x34ff) {
                    gsuCore.writeIo(offset, value);
                    return;
                }
                if (offset >= 0x6000 && offset <= 0x7fff) {
                    if (gsuCore.cpuCanSeeRam()) {
                        gsuCore.writeRam(offset & 0x1fff, value);
                    }
                    return;
                }
            } else if (lowBank >= 0x70 && lowBank <= 0x7d) {
                if (gsuCore.cpuCanSeeRam()) {
                    gsuCore.writeRam(((lowBank & 1) << 16) | offset, value);
                }
                return;
            }
        }
    }
    if (sa1Present && sa1MapWrite(address, value)) {
        return;
    }
    if (dspPresent && dspMapWrite(address, value)) {
        return;
    }
    if (sdd1Present && sdd1MapWrite(address, value)) {
        return;
    }
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
        const uint8_t value = sdd1DmaActive ? sdd1Core.decompressReadByte() : readRaw(aaddr);

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
        // The DMA A-bus address increments/decrements only its 16-bit offset and
        // wraps WITHIN the fixed bank ($xx:FFFF -> $xx:0000); the bank byte
        // ($43x4) is never modified by a transfer. DOOM's sound-table loader
        // relies on this: it sets the source bank once and only the offset per
        // DMA, so carrying into the bank would make a following DMA read the
        // wrong bank (the shotgun-fire crash: the $5686 table loaded from the
        // wrong bank after the prior DMA ended on a bank boundary).
        const uint32_t bank = aaddr & 0xff0000;
        uint16_t offset = static_cast<uint16_t>(aaddr);
        if ((dmap & 0x10) != 0) {
            --offset;
        } else {
            ++offset;
        }
        aaddr = bank | offset;
        setDmaAAddress(channel, aaddr);
    }
}

void SnesBus::reloadHdma(uint8_t channel)
{
    HdmaChannel& state = hdma[channel];
    // A new table entry is only loaded once the current one is exhausted, i.e.
    // the low 7 bits of the line counter have reached zero.
    if ((state.lineCounter & 0x7f) != 0) {
        return;
    }
    const uint16_t base = static_cast<uint16_t>(0x4300 + channel * 0x10);
    uint16_t table = hdmaTableAddress(channel);
    const uint8_t bank = mmio[base + 4 - 0x2000];
    const uint8_t line = readRaw((static_cast<uint32_t>(bank) << 16) | table);
    setHdmaTableAddress(channel, static_cast<uint16_t>(table + 1));

    state.lineCounter = line; // keep the full byte (repeat bit in bit 7)
    if (line == 0) {
        state.active = false;
        state.doTransfer = false;
        return;
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
    // A connected standard controller returns its 16 button/ID bits, then the
    // data line idles high (1) on every further clock. Games (e.g. DOOM) read
    // past bit 16 and rely on the 1 to detect a present controller.
    uint8_t bit = 1;
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
    // Run the SPC to the exact CPU cycle of this access so the tight port
    // handshake (e.g. DOOM's sound-upload loop) sees coherent, in-order timing.
    if (cpuTiming) {
        apuCore.syncToCpuCycle(cpuTiming->totalCycles());
    }
    const uint8_t val = apuCore.readPort(port);
    apuPortLog.lastRead[port] = val;
    apuPortLog.readCount[port]++;
    apuPortLog.totalReads++;
    return val;
}

void SnesBus::writeApuPort(uint16_t address, uint8_t value)
{
    const uint8_t port = static_cast<uint8_t>((address - 0x2140) & 0x03);
    if (cpuTiming) {
        apuCore.syncToCpuCycle(cpuTiming->totalCycles());
    }
    apuCore.writePort(port, value);
    apuPortLog.lastWrite[port] = value;
    apuPortLog.writeCount[port]++;
    apuPortLog.totalWrites++;
}

void SnesBus::saveState(std::vector<uint8_t>& out)
{
    appendPod(out, wram);
    appendPod(out, mmio);
    appendPod(out, openBusValue);
    appendPod(out, wramPortAddr);
    appendPod(out, wrmpya);
    appendPod(out, wrmpyb);
    appendPod(out, rdmpy);
    appendPod(out, wrdiva);
    appendPod(out, wrdivb);
    appendPod(out, rddiv);
    appendPod(out, nmiEnable);
    appendPod(out, nmiFlag);
    appendPod(out, vblankActive);
    appendPod(out, joypadStrobe);
    appendPod(out, joypadAutoReadEnable);
    appendPod(out, joypadAutoReadBusy);
    appendPod(out, joypadAutoReadCyclesRemaining);
    appendPod(out, nmiEdge);
    appendPod(out, joypadCurrentState);
    appendPod(out, joypadLatchedState);
    appendPod(out, joypadReadIndex);
    appendPod(out, hvIrqMode);
    appendPod(out, irqHTimeVal);
    appendPod(out, irqVTimeVal);
    appendPod(out, irqPending);
    appendPod(out, hdma);
    appendPod(out, pendingDmaDots);

    const auto& sram = cart.sramData();
    appendPod(out, static_cast<uint32_t>(sram.size()));
    out.insert(out.end(), sram.begin(), sram.end());

    std::vector<uint8_t> apuBlob;
    apuCore.saveState(apuBlob);
    appendPod(out, static_cast<uint32_t>(apuBlob.size()));
    out.insert(out.end(), apuBlob.begin(), apuBlob.end());

    std::vector<uint8_t> gsuBlob;
    if (gsuPresent) {
        gsuCore.saveState(gsuBlob);
    }
    appendPod(out, static_cast<uint32_t>(gsuBlob.size()));
    out.insert(out.end(), gsuBlob.begin(), gsuBlob.end());

    std::vector<uint8_t> sa1Blob;
    if (sa1Present) {
        sa1Core.saveState(sa1Blob);
    }
    appendPod(out, static_cast<uint32_t>(sa1Blob.size()));
    out.insert(out.end(), sa1Blob.begin(), sa1Blob.end());

    std::vector<uint8_t> dspBlob;
    if (dspPresent) {
        dspCore.saveState(dspBlob);
    }
    appendPod(out, static_cast<uint32_t>(dspBlob.size()));
    out.insert(out.end(), dspBlob.begin(), dspBlob.end());

    std::vector<uint8_t> sdd1Blob;
    if (sdd1Present) {
        sdd1Core.saveState(sdd1Blob);
    }
    appendPod(out, static_cast<uint32_t>(sdd1Blob.size()));
    out.insert(out.end(), sdd1Blob.begin(), sdd1Blob.end());
}

bool SnesBus::loadState(const uint8_t* data, size_t size)
{
    const uint8_t* pos = data;
    const uint8_t* end = data + size;

    if (!readPod(pos, end, wram) || !readPod(pos, end, mmio) || !readPod(pos, end, openBusValue)
        || !readPod(pos, end, wramPortAddr) || !readPod(pos, end, wrmpya) || !readPod(pos, end, wrmpyb)
        || !readPod(pos, end, rdmpy) || !readPod(pos, end, wrdiva) || !readPod(pos, end, wrdivb)
        || !readPod(pos, end, rddiv) || !readPod(pos, end, nmiEnable) || !readPod(pos, end, nmiFlag)
        || !readPod(pos, end, vblankActive) || !readPod(pos, end, joypadStrobe)
        || !readPod(pos, end, joypadAutoReadEnable) || !readPod(pos, end, joypadAutoReadBusy)
        || !readPod(pos, end, joypadAutoReadCyclesRemaining) || !readPod(pos, end, nmiEdge)
        || !readPod(pos, end, joypadCurrentState) || !readPod(pos, end, joypadLatchedState)
        || !readPod(pos, end, joypadReadIndex) || !readPod(pos, end, hvIrqMode)
        || !readPod(pos, end, irqHTimeVal) || !readPod(pos, end, irqVTimeVal)
        || !readPod(pos, end, irqPending) || !readPod(pos, end, hdma)
        || !readPod(pos, end, pendingDmaDots)) {
        return false;
    }

    uint32_t sramSize = 0;
    if (!readPod(pos, end, sramSize) || static_cast<size_t>(end - pos) < sramSize) {
        return false;
    }
    auto& sram = cart.sramData();
    if (sram.size() == sramSize) {
        std::memcpy(sram.data(), pos, sramSize);
    }
    pos += sramSize;

    uint32_t apuSize = 0;
    if (!readPod(pos, end, apuSize) || static_cast<size_t>(end - pos) < apuSize) {
        return false;
    }
    if (!apuCore.loadState(pos, apuSize)) {
        return false;
    }
    pos += apuSize;

    if (pos == end) {
        // Older state without a GSU section.
        return true;
    }
    uint32_t gsuSize = 0;
    if (!readPod(pos, end, gsuSize) || static_cast<size_t>(end - pos) < gsuSize) {
        return false;
    }
    if (gsuSize > 0) {
        if (!gsuPresent || !gsuCore.loadState(pos, gsuSize)) {
            return false;
        }
    }
    pos += gsuSize;

    if (pos == end) {
        // Older state without an SA-1 section.
        return true;
    }
    uint32_t sa1Size = 0;
    if (!readPod(pos, end, sa1Size) || static_cast<size_t>(end - pos) < sa1Size) {
        return false;
    }
    if (sa1Size > 0) {
        if (!sa1Present || !sa1Core.loadState(pos, sa1Size)) {
            return false;
        }
    }
    pos += sa1Size;

    if (pos == end) {
        // Older state without a DSP section.
        return true;
    }
    uint32_t dspSize = 0;
    if (!readPod(pos, end, dspSize) || static_cast<size_t>(end - pos) < dspSize) {
        return false;
    }
    if (dspSize > 0) {
        if (!dspPresent || !dspCore.loadState(pos, dspSize)) {
            return false;
        }
    }
    pos += dspSize;

    if (pos == end) {
        // Older state without an S-DD1 section.
        return true;
    }
    uint32_t sdd1Size = 0;
    if (!readPod(pos, end, sdd1Size) || static_cast<size_t>(end - pos) < sdd1Size) {
        return false;
    }
    if (sdd1Size > 0) {
        if (!sdd1Present || !sdd1Core.loadState(pos, sdd1Size)) {
            return false;
        }
    }
    pos += sdd1Size;

    return pos == end;
}

void SnesBus::attachGsu(size_t ramSize)
{
    gsuPresent = true;
    gsuCore.power();
    gsuCore.attachRom(cart.bytes());
    gsuCore.setRamSize(ramSize);
}

void SnesBus::stepGsu(uint32_t masterClocks)
{
    if (gsuPresent) {
        gsuCore.run(masterClocks);
    }
}

void SnesBus::attachSa1(size_t bwRamSize)
{
    sa1Present = true;
    sa1Core.power();
    sa1Core.attachRom(cart.bytes());
    sa1Core.setBwRamSize(bwRamSize);
}

void SnesBus::stepSa1(uint32_t masterClocks)
{
    if (sa1Present) {
        sa1Core.stepSa1(masterClocks);
    }
}

void SnesBus::attachDsp(const std::string& romPath, CartridgeMap map)
{
    dspPresent = true;
    dspHiRomMap = (map == CartridgeMap::HiROM || map == CartridgeMap::ExHiROM);
    dspCore.power();
    if (!romPath.empty()) {
        dspCore.loadRom(romPath); // inert if the dump is missing/wrong size
    }
}

void SnesBus::stepDsp(uint32_t cycles)
{
    if (dspPresent) {
        dspCore.step(cycles);
    }
}

void SnesBus::attachSdd1()
{
    sdd1Present = true;
    sdd1Core.power();
    sdd1Core.attachRom(cart.bytes());
}

bool SnesBus::sdd1MapRead(uint32_t address, uint8_t& value)
{
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);
    const uint8_t lowBank = bank & 0x7f; // $00-$3F mirrors $80-$BF
    // Registers $4800-$4807 in banks $00-$3F / $80-$BF.
    if (lowBank <= 0x3f && offset >= 0x4800 && offset <= 0x4807) {
        value = sdd1Core.readRegister(offset);
        return true;
    }
    // MMC ROM window $C0-$FF.
    if (bank >= 0xc0) {
        value = sdd1Core.readRom(address);
        return true;
    }
    return false;
}

bool SnesBus::sdd1MapWrite(uint32_t address, uint8_t value)
{
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);
    const uint8_t lowBank = bank & 0x7f;
    if (lowBank <= 0x3f && offset >= 0x4800 && offset <= 0x4807) {
        sdd1Core.writeRegister(offset, value);
        return true;
    }
    return false;
}

bool SnesBus::dspMapRead(uint32_t address, uint8_t& value)
{
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);
    const uint8_t lowBank = bank & 0x7f; // $00-$3F mirrors $80-$BF
    if (dspHiRomMap) {
        // HiROM DSP-1: banks $00-$1F & $80-$9F, $6000-$7FFF; $1000 selects SR.
        if (lowBank <= 0x1f && offset >= 0x6000 && offset <= 0x7fff) {
            value = (offset & 0x1000) ? dspCore.readSR() : dspCore.readDR();
            return true;
        }
    } else {
        // LoROM DSP-1: banks $30-$3F & $B0-$BF, $8000-$FFFF; $4000 selects SR.
        if (lowBank >= 0x30 && lowBank <= 0x3f && offset >= 0x8000) {
            value = (offset & 0x4000) ? dspCore.readSR() : dspCore.readDR();
            return true;
        }
    }
    return false;
}

bool SnesBus::dspMapWrite(uint32_t address, uint8_t value)
{
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);
    const uint8_t lowBank = bank & 0x7f;
    if (dspHiRomMap) {
        if (lowBank <= 0x1f && offset >= 0x6000 && offset <= 0x7fff) {
            if (!(offset & 0x1000)) {
                dspCore.writeDR(value); // SR is read-only to the S-CPU
            }
            return true;
        }
    } else {
        if (lowBank >= 0x30 && lowBank <= 0x3f && offset >= 0x8000) {
            if (!(offset & 0x4000)) {
                dspCore.writeDR(value);
            }
            return true;
        }
    }
    return false;
}

bool SnesBus::sa1MapRead(uint32_t address, uint8_t& value)
{
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);
    if (bank == 0x7e || bank == 0x7f) {
        return false; // system WRAM
    }
    if (bank >= 0x40 && bank <= 0x4f) {
        value = sa1Core.cpuReadBwLinear((static_cast<uint32_t>(bank - 0x40) << 16) | offset);
        return true;
    }
    if (bank >= 0xc0) {
        value = sa1Core.cpuReadRom(address);
        return true;
    }
    const uint8_t lowBank = bank & 0x7f; // $00-$3F == $80-$BF
    if (lowBank <= 0x3f) {
        if (offset >= 0x2200 && offset <= 0x23ff) {
            value = sa1Core.cpuReadIo(offset);
            return true;
        }
        if (offset >= 0x3000 && offset <= 0x37ff) {
            value = sa1Core.cpuReadIram(offset);
            return true;
        }
        if (offset >= 0x6000 && offset <= 0x7fff) {
            value = sa1Core.cpuReadBwWindow(offset);
            return true;
        }
        if (offset >= 0x8000) {
            value = sa1Core.cpuReadRom(address);
            return true;
        }
    }
    return false; // WRAM mirror / PPU / CPU MMIO handled by the normal map
}

bool SnesBus::sa1MapWrite(uint32_t address, uint8_t value)
{
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);
    if (bank == 0x7e || bank == 0x7f) {
        return false;
    }
    if (bank >= 0x40 && bank <= 0x4f) {
        sa1Core.cpuWriteBwLinear((static_cast<uint32_t>(bank - 0x40) << 16) | offset, value);
        return true;
    }
    if (bank >= 0xc0) {
        return true; // ROM region: writes ignored
    }
    const uint8_t lowBank = bank & 0x7f;
    if (lowBank <= 0x3f) {
        if (offset >= 0x2200 && offset <= 0x23ff) {
            sa1Core.cpuWriteIo(offset, value);
            return true;
        }
        if (offset >= 0x3000 && offset <= 0x37ff) {
            sa1Core.cpuWriteIram(offset, value);
            return true;
        }
        if (offset >= 0x6000 && offset <= 0x7fff) {
            sa1Core.cpuWriteBwWindow(offset, value);
            return true;
        }
        if (offset >= 0x8000) {
            return true; // ROM region: writes ignored
        }
    }
    return false;
}

void SnesBus::initApu()
{
    apuCore.init();
}

void SnesBus::stepApu(uint32_t extraCycles)
{
    if (cpuTiming) {
        // Catch the SPC up to the CPU's current cycle (any port accesses during
        // the instruction already advanced it to their exact sub-instruction
        // moment); extraCycles adds DMA halt time the CPU counter omits.
        apuCore.syncToCpuCycle(cpuTiming->totalCycles(), extraCycles);
        return;
    }
    apuCore.step(extraCycles);
}

void SnesBus::endApuFrame()
{
    apuCore.endFrame();
}

} // namespace snesquik::bus
