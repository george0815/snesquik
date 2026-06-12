#include "GSU/gsu.h"

#include <cstdio>
#include <cstring>

namespace snesquik::gsu {

void Gsu::power()
{
    r.fill(0);
    sfr = 0;
    pbr = 0;
    rombr = 0;
    rambr = false;
    cbr = 0;
    scbr = 0;
    scmrHt = 0;
    scmrRon = false;
    scmrRan = false;
    scmrMd = 0;
    colr = 0;
    por = 0;
    bramr = false;
    vcr = 0x04;
    cfgrIrqMask = false;
    cfgrMs0 = false;
    clsr = false;
    pipeline = 0x01; // nop
    ramaddr = 0;
    sreg = 0;
    dreg = 0;
    r15Modified = false;
    romcl = 0;
    romdr = 0;
    ramcl = 0;
    ramar = 0;
    ramdr = 0;
    cache.buffer.fill(0);
    cache.valid.fill(false);
    for (auto& pc : pixelCache) {
        pc.offset = 0xffff;
        pc.bitpend = 0;
        pc.data.fill(0);
    }
    budget = 0;
}

void Gsu::attachRom(std::span<const uint8_t> romBytes)
{
    rom = romBytes;
    romMask = rom.empty() ? 0 : static_cast<uint32_t>(rom.size() - 1);
}

void Gsu::setRamSize(size_t bytes)
{
    ram.assign(bytes, 0);
    ramMask = ram.empty() ? 0 : static_cast<uint32_t>(ram.size() - 1);
}

void Gsu::setReg(int n, uint16_t value)
{
    n &= 15;
    r[n] = value;
    if (n == 14) {
        updateRomBuffer();
    }
    if (n == 15) {
        r15Modified = true;
    }
}

void Gsu::setDest(uint16_t value)
{
    setReg(dreg, value);
}

void Gsu::step(uint32_t cycles)
{
    if (romcl) {
        const uint32_t consumed = cycles < romcl ? cycles : romcl;
        romcl -= consumed;
        if (romcl == 0) {
            setFlag(flagR, false);
            romdr = readMemory((static_cast<uint32_t>(rombr) << 16) + r[14]);
        }
    }
    if (ramcl) {
        const uint32_t consumed = cycles < ramcl ? cycles : ramcl;
        ramcl -= consumed;
        if (ramcl == 0) {
            writeMemory(0x700000 + (static_cast<uint32_t>(rambr) << 16) + ramar, ramdr);
        }
    }
    // The budget is in master clocks; one GSU cycle is 2 master clocks at
    // 10.7 MHz (CLSR=0) or 1 at 21.4 MHz (CLSR=1).
    budget -= static_cast<int64_t>(cycles) * (clsr ? 1 : 2);
}

uint8_t Gsu::readMemory(uint32_t address)
{
    address &= 0xffffff;
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    if (bank <= 0x3f) {
        // LoROM view of the game pack ROM.
        if (rom.empty()) {
            return 0;
        }
        return rom[((((address & 0x3f0000) >> 1) | (address & 0x7fff))) & romMask];
    }
    if (bank <= 0x5f) {
        if (rom.empty()) {
            return 0;
        }
        return rom[address & romMask];
    }
    if (bank <= 0x7f) {
        if (ram.empty()) {
            return 0;
        }
        return ram[address & ramMask];
    }
    return 0;
}

void Gsu::writeMemory(uint32_t address, uint8_t value)
{
    address &= 0xffffff;
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    if (bank >= 0x60 && bank <= 0x7f && !ram.empty()) {
        ram[address & ramMask] = value;
    }
}

void Gsu::syncRomBuffer()
{
    if (romcl) {
        step(romcl);
    }
}

uint8_t Gsu::readRomBuffer()
{
    syncRomBuffer();
    return romdr;
}

void Gsu::updateRomBuffer()
{
    setFlag(flagR, true);
    romcl = clsr ? 5 : 6;
}

void Gsu::syncRamBuffer()
{
    if (ramcl) {
        step(ramcl);
    }
}

uint8_t Gsu::readRamBuffer(uint16_t address)
{
    syncRamBuffer();
    return readMemory(0x700000 + (static_cast<uint32_t>(rambr) << 16) + address);
}

void Gsu::writeRamBuffer(uint16_t address, uint8_t value)
{
    syncRamBuffer();
    ramcl = clsr ? 5 : 6;
    ramar = address;
    ramdr = value;
}

uint8_t Gsu::readOpcode(uint16_t address)
{
    const uint16_t offset = static_cast<uint16_t>(address - cbr);
    if (offset < 512) {
        if (!cache.valid[offset >> 4]) {
            uint32_t dp = offset & 0xfff0;
            uint32_t sp = (static_cast<uint32_t>(pbr) << 16) + ((cbr + dp) & 0xfff0);
            for (int n = 0; n < 16; ++n) {
                step(clsr ? 5 : 6);
                cache.buffer[dp++ & 511] = readMemory(sp++);
            }
            cache.valid[offset >> 4] = true;
        } else {
            step(clsr ? 1 : 2);
        }
        return cache.buffer[offset];
    }

    if (pbr <= 0x5f) {
        syncRomBuffer();
    } else {
        syncRamBuffer();
    }
    step(clsr ? 5 : 6);
    return readMemory((static_cast<uint32_t>(pbr) << 16) | address);
}

uint8_t Gsu::pipe()
{
    const uint8_t result = pipeline;
    pipeline = readOpcode(static_cast<uint16_t>(++r[15]));
    r15Modified = false;
    return result;
}

void Gsu::flushCache()
{
    cache.valid.fill(false);
}

uint8_t Gsu::readCacheByte(uint16_t offset) const
{
    return cache.buffer[(offset + cbr) & 511];
}

void Gsu::writeCacheByte(uint16_t offset, uint8_t value)
{
    const uint16_t address = static_cast<uint16_t>((offset + cbr) & 511);
    cache.buffer[address] = value;
    if ((address & 15) == 15) {
        cache.valid[address >> 4] = true;
    }
}

void Gsu::stopGsu()
{
    setFlag(flagG, false);
    ++stopCount;
}

uint8_t Gsu::applyColorSource(uint8_t source) const
{
    if (porHighNibble()) {
        return static_cast<uint8_t>((colr & 0xf0) | (source >> 4));
    }
    if (porFreezeHigh()) {
        return static_cast<uint8_t>((colr & 0xf0) | (source & 0x0f));
    }
    return source;
}

uint32_t Gsu::screenPixelAddress(uint8_t x, uint8_t y, uint32_t& bpp) const
{
    uint32_t cn = 0;
    switch (porObj() ? 3 : scmrHt) {
    case 0:
        cn = ((x & 0xf8) << 1) + ((y & 0xf8) >> 3);
        break;
    case 1:
        cn = ((x & 0xf8) << 1) + ((x & 0xf8) >> 1) + ((y & 0xf8) >> 3);
        break;
    case 2:
        cn = ((x & 0xf8) << 1) + ((x & 0xf8) << 0) + ((y & 0xf8) >> 3);
        break;
    case 3:
        cn = ((y & 0x80) << 2) + ((x & 0x80) << 1) + ((y & 0x78) << 1) + ((x & 0x78) >> 3);
        break;
    }
    bpp = 2u << (scmrMd - (scmrMd >> 1)); // 2, 4, 4, 8
    return 0x700000 + (cn * (bpp << 3)) + (static_cast<uint32_t>(scbr) << 10) + ((y & 0x07) * 2);
}

void Gsu::plot(uint8_t x, uint8_t y)
{
    if (!porTransparent()) {
        if (scmrMd == 3) {
            if (porFreezeHigh()) {
                if ((colr & 0x0f) == 0) {
                    return;
                }
            } else if (colr == 0) {
                return;
            }
        } else if ((colr & 0x0f) == 0) {
            return;
        }
    }

    uint8_t color = colr;
    if (porDither() && scmrMd != 3) {
        if ((x ^ y) & 1) {
            color >>= 4;
        }
        color &= 0x0f;
    }

    const uint16_t offset = static_cast<uint16_t>((y << 5) + (x >> 3));
    if (offset != pixelCache[0].offset) {
        flushPixelCache(pixelCache[1]);
        pixelCache[1] = pixelCache[0];
        pixelCache[0].bitpend = 0;
        pixelCache[0].offset = offset;
    }

    x = (x & 7) ^ 7;
    pixelCache[0].data[x] = color;
    pixelCache[0].bitpend |= static_cast<uint8_t>(1 << x);
    if (pixelCache[0].bitpend == 0xff) {
        flushPixelCache(pixelCache[1]);
        pixelCache[1] = pixelCache[0];
        pixelCache[0].bitpend = 0;
    }
}

uint8_t Gsu::rpix(uint8_t x, uint8_t y)
{
    flushPixelCache(pixelCache[1]);
    flushPixelCache(pixelCache[0]);

    uint32_t bpp = 0;
    const uint32_t addr = screenPixelAddress(x, y, bpp);
    uint8_t data = 0;
    x = (x & 7) ^ 7;

    for (uint32_t n = 0; n < bpp; ++n) {
        const uint32_t byte = ((n >> 1) << 4) + (n & 1);
        step(clsr ? 5 : 6);
        data |= static_cast<uint8_t>(((readMemory(addr + byte) >> x) & 1) << n);
    }
    return data;
}

void Gsu::flushPixelCache(PixelCache& pc)
{
    if (pc.bitpend == 0) {
        return;
    }

    const uint8_t x = static_cast<uint8_t>(pc.offset << 3);
    const uint8_t y = static_cast<uint8_t>(pc.offset >> 5);

    uint32_t bpp = 0;
    const uint32_t addr = screenPixelAddress(x, y, bpp);

    for (uint32_t n = 0; n < bpp; ++n) {
        const uint32_t byte = ((n >> 1) << 4) + (n & 1);
        uint8_t data = 0;
        for (int px = 0; px < 8; ++px) {
            data |= static_cast<uint8_t>(((pc.data[px] >> n) & 1) << px);
        }
        if (pc.bitpend != 0xff) {
            step(clsr ? 5 : 6);
            data &= pc.bitpend;
            data |= readMemory(addr + byte) & ~pc.bitpend;
        }
        step(clsr ? 5 : 6);
        writeMemory(addr + byte, data);
    }

    pc.bitpend = 0;
}

void Gsu::executeOne()
{
    const uint8_t opcode = pipeline;
    pipeline = readOpcode(r[15]);
    r15Modified = false;

    const auto& instruction = opcodeTable()[opcode];
    ++instructionCount;

    if (traceRemaining > 0 && traceSink) {
        --traceRemaining;
        std::fprintf(static_cast<std::FILE*>(traceSink),
            "%02x:%04x %02x %-12s sfr=%04x s=%d d=%d "
            "r0=%04x r1=%04x r2=%04x r3=%04x r4=%04x r5=%04x r6=%04x r11=%04x r12=%04x r13=%04x r14=%04x\n",
            pbr, static_cast<uint16_t>(r[15] - 1), opcode, instruction.mnemonic, sfr, sreg, dreg,
            r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[11], r[12], r[13], r[14]);
    }

    instruction.operation(*this, opcode);

    if (r15Modified) {
        r15Modified = false;
    } else {
        ++r[15];
    }
}

void Gsu::run(uint32_t masterClocks)
{
    // GSU clock: master/2 when CLSR=0 (10.7 MHz), master/1 when CLSR=1.
    budget += masterClocks;
    if (!goFlag()) {
        budget = 0;
        return;
    }
    while (goFlag() && budget > 0) {
        const int64_t before = budget;
        executeOne();
        if (budget == before) {
            // Instruction consumed no modelled time (pure cache-resident
            // register op already costed in readOpcode); charge minimum.
            step(clsr ? 1 : 2);
        }
    }
    if (!goFlag()) {
        budget = 0;
    }
}

uint8_t Gsu::readIo(uint16_t address)
{
    address = static_cast<uint16_t>(0x3000 | (address & 0x3ff));

    if (address >= 0x3100 && address <= 0x32ff) {
        return readCacheByte(static_cast<uint16_t>(address - 0x3100));
    }
    if (address <= 0x301f) {
        const int n = (address >> 1) & 15;
        return static_cast<uint8_t>(r[n] >> ((address & 1) << 3));
    }

    switch (address) {
    case 0x3030:
        return static_cast<uint8_t>(sfr & 0x7e);
    case 0x3031: {
        const uint8_t value = static_cast<uint8_t>((sfr >> 8) & 0x9f);
        setFlag(flagIrq, false);
        return value;
    }
    case 0x3034:
        return pbr;
    case 0x3036:
        return rombr;
    case 0x303b:
        return vcr;
    case 0x303c:
        return rambr ? 1 : 0;
    case 0x303e:
        return static_cast<uint8_t>(cbr);
    case 0x303f:
        return static_cast<uint8_t>(cbr >> 8);
    default:
        return 0;
    }
}

void Gsu::writeIo(uint16_t address, uint8_t value)
{
    address = static_cast<uint16_t>(0x3000 | (address & 0x3ff));

    if (address >= 0x3100 && address <= 0x32ff) {
        writeCacheByte(static_cast<uint16_t>(address - 0x3100), value);
        return;
    }
    if (address <= 0x301f) {
        const int n = (address >> 1) & 15;
        if ((address & 1) == 0) {
            r[n] = static_cast<uint16_t>((r[n] & 0xff00) | value);
        } else {
            r[n] = static_cast<uint16_t>((value << 8) | (r[n] & 0x00ff));
        }
        if (n == 14) {
            updateRomBuffer();
        }
        if (address == 0x301f) {
            setFlag(flagG, true);
        }
        return;
    }

    switch (address) {
    case 0x3030: {
        const bool wasRunning = goFlag();
        sfr = static_cast<uint16_t>((sfr & 0xff00) | value);
        if (wasRunning && !goFlag()) {
            cbr = 0;
            flushCache();
        }
        break;
    }
    case 0x3031:
        sfr = static_cast<uint16_t>((value << 8) | (sfr & 0x00ff));
        break;
    case 0x3033:
        bramr = (value & 0x01) != 0;
        break;
    case 0x3034:
        pbr = value & 0x7f;
        flushCache();
        break;
    case 0x3037:
        cfgrIrqMask = (value & 0x80) != 0;
        cfgrMs0 = (value & 0x20) != 0;
        break;
    case 0x3038:
        scbr = value;
        break;
    case 0x3039:
        clsr = (value & 0x01) != 0;
        break;
    case 0x303a:
        scmrHt = static_cast<uint8_t>((((value & 0x20) != 0) << 1) | ((value & 0x04) != 0));
        scmrRon = (value & 0x10) != 0;
        scmrRan = (value & 0x08) != 0;
        scmrMd = value & 0x03;
        break;
    default:
        break;
    }
}

uint8_t Gsu::cpuRomConflictValue(uint32_t address) const
{
    // While the GSU owns the ROM bus, the S-CPU reads this fixed pattern;
    // its placement provides valid interrupt vectors pointing at $0104.
    static constexpr uint8_t vector[16] = {
        0x00, 0x01, 0x00, 0x01, 0x04, 0x01, 0x00, 0x01,
        0x00, 0x01, 0x08, 0x01, 0x00, 0x01, 0x0c, 0x01,
    };
    return vector[address & 15];
}

uint8_t Gsu::readRam(uint32_t offset) const
{
    if (ram.empty()) {
        return 0xff;
    }
    return ram[offset & ramMask];
}

void Gsu::writeRam(uint32_t offset, uint8_t value)
{
    if (!ram.empty()) {
        ram[offset & ramMask] = value;
    }
}

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

void Gsu::saveState(std::vector<uint8_t>& out) const
{
    appendPod(out, r);
    appendPod(out, sfr);
    appendPod(out, pbr);
    appendPod(out, rombr);
    appendPod(out, rambr);
    appendPod(out, cbr);
    appendPod(out, scbr);
    appendPod(out, scmrHt);
    appendPod(out, scmrRon);
    appendPod(out, scmrRan);
    appendPod(out, scmrMd);
    appendPod(out, colr);
    appendPod(out, por);
    appendPod(out, bramr);
    appendPod(out, cfgrIrqMask);
    appendPod(out, cfgrMs0);
    appendPod(out, clsr);
    appendPod(out, pipeline);
    appendPod(out, ramaddr);
    appendPod(out, sreg);
    appendPod(out, dreg);
    appendPod(out, r15Modified);
    appendPod(out, romcl);
    appendPod(out, romdr);
    appendPod(out, ramcl);
    appendPod(out, ramar);
    appendPod(out, ramdr);
    appendPod(out, cache);
    appendPod(out, pixelCache);
    appendPod(out, static_cast<uint32_t>(ram.size()));
    out.insert(out.end(), ram.begin(), ram.end());
}

bool Gsu::loadState(const uint8_t* data, size_t size)
{
    const uint8_t* pos = data;
    const uint8_t* end = data + size;
    if (!readPod(pos, end, r) || !readPod(pos, end, sfr) || !readPod(pos, end, pbr)
        || !readPod(pos, end, rombr) || !readPod(pos, end, rambr) || !readPod(pos, end, cbr)
        || !readPod(pos, end, scbr) || !readPod(pos, end, scmrHt) || !readPod(pos, end, scmrRon)
        || !readPod(pos, end, scmrRan) || !readPod(pos, end, scmrMd) || !readPod(pos, end, colr)
        || !readPod(pos, end, por) || !readPod(pos, end, bramr) || !readPod(pos, end, cfgrIrqMask)
        || !readPod(pos, end, cfgrMs0) || !readPod(pos, end, clsr) || !readPod(pos, end, pipeline)
        || !readPod(pos, end, ramaddr) || !readPod(pos, end, sreg) || !readPod(pos, end, dreg)
        || !readPod(pos, end, r15Modified) || !readPod(pos, end, romcl) || !readPod(pos, end, romdr)
        || !readPod(pos, end, ramcl) || !readPod(pos, end, ramar) || !readPod(pos, end, ramdr)
        || !readPod(pos, end, cache) || !readPod(pos, end, pixelCache)) {
        return false;
    }
    uint32_t ramBytes = 0;
    if (!readPod(pos, end, ramBytes) || static_cast<size_t>(end - pos) != ramBytes) {
        return false;
    }
    ram.assign(pos, pos + ramBytes);
    ramMask = ram.empty() ? 0 : static_cast<uint32_t>(ram.size() - 1);
    budget = 0;
    return true;
}

} // namespace snesquik::gsu
