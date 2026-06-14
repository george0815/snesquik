#include "SA1/sa1.h"

#include <algorithm>
#include <cstring>

namespace snesquik::sa1 {

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

Sa1::Sa1()
    : sa1Cpu(*this)
{
}

void Sa1::power()
{
    iram.fill(0);
    budget = 0;
    clockRemainder = 0;

    sa1IrqReq = false;
    sa1Wait = false;
    sa1Reset = true;
    sa1NmiReq = false;
    messageToSa1 = 0;
    cpuIrqEnable = false;
    chdmaIrqEnable = false;
    crv = cnv = civ = 0;
    cpuIrqReq = false;
    cpuIvSel = false;
    cpuNvSel = false;
    messageToCpu = 0;
    sa1IrqEnable = false;
    timerIrqEnable = false;
    dmaIrqEnable = false;
    sa1NmiEnable = false;
    snv = siv = 0;
    romBank[0] = 0;
    romBank[1] = 1;
    romBank[2] = 2;
    romBank[3] = 3;
    for (bool& mode : romBankMode) {
        mode = false;
    }
    cpuBwBlock = 0;
    sa1BwBlock = 0;
    sa1BwBitmap = false;
    cpuIrqFlag = chdmaIrqFlag = false;
    sa1IrqFlag = timerIrqFlag = dmaIrqFlag = false;
    sa1NmiFlag = false;
    sa1NmiLine = false;
    arithSum = arithDivide = false;
    mathA = mathB = 0;
    mathResult = 0;
    mathOverflow = false;
    dmaEnable = dmaPriority = ccEnable = ccType1 = false;
    dmaDest = dmaSource = ccColorBits = ccSize = 0;
    dmaSourceAddr = dmaDestAddr = 0;
    dmaCount = 0;
    bitmapFormat = false;
    ccBrf.fill(0);
    ccLine = 0;
    ccActive = false;

    // The SA-1 boots held in reset; the S-CPU releases it via $2200.
    sa1Cpu.reset();
}

void Sa1::attachRom(std::span<const uint8_t> romBytes)
{
    rom = romBytes;
    romMask = rom.empty() ? 0 : static_cast<uint32_t>(rom.size() - 1);
}

void Sa1::setBwRamSize(size_t bytes)
{
    bwram.assign(bytes, 0);
    bwramMask = bwram.empty() ? 0 : static_cast<uint32_t>(bwram.size() - 1);
}

// ---------------------------------------------------------------------------
// MMC ROM mapping (shared by both CPUs). Implements the default (mode 0)
// SA-1 layout; the four super-bank registers each select a 1 MB block.
// ---------------------------------------------------------------------------
uint32_t Sa1::mmcRom(uint32_t address) const
{
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);

    auto loRom = [&](uint8_t super, uint8_t bankIndex) -> uint32_t {
        return (static_cast<uint32_t>(romBank[super]) << 20)
             | (static_cast<uint32_t>(bankIndex & 0x1f) << 15)
             | (offset & 0x7fff);
    };
    auto hiRom = [&](uint8_t super, uint8_t bankIndex) -> uint32_t {
        return (static_cast<uint32_t>(romBank[super]) << 20)
             | (static_cast<uint32_t>(bankIndex & 0x0f) << 16)
             | offset;
    };

    if (bank <= 0x1f) {
        return loRom(0, bank); // $00-$1F:8000-FFFF -> CXB
    }
    if (bank <= 0x3f) {
        return loRom(1, bank - 0x20); // $20-$3F -> DXB
    }
    if (bank >= 0x80 && bank <= 0x9f) {
        return loRom(2, bank - 0x80); // $80-$9F -> EXB
    }
    if (bank >= 0xa0 && bank <= 0xbf) {
        return loRom(3, bank - 0xa0); // $A0-$BF -> FXB
    }
    if (bank >= 0xc0 && bank <= 0xcf) {
        return hiRom(0, bank - 0xc0); // $C0-$CF -> CXB
    }
    if (bank >= 0xd0 && bank <= 0xdf) {
        return hiRom(1, bank - 0xd0); // $D0-$DF -> DXB
    }
    if (bank >= 0xe0 && bank <= 0xef) {
        return hiRom(2, bank - 0xe0); // $E0-$EF -> EXB
    }
    return hiRom(3, bank - 0xf0); // $F0-$FF -> FXB
}

// ---------------------------------------------------------------------------
// BW-RAM
// ---------------------------------------------------------------------------
uint8_t Sa1::readBwBlock(uint32_t block, uint16_t offset) const
{
    if (bwram.empty()) {
        return 0xff;
    }
    const uint32_t addr = (block * 0x2000u) + (offset & 0x1fff);
    return bwram[addr & bwramMask];
}

void Sa1::writeBwBlock(uint32_t block, uint16_t offset, uint8_t value)
{
    if (bwram.empty()) {
        return;
    }
    const uint32_t addr = (block * 0x2000u) + (offset & 0x1fff);
    bwram[addr & bwramMask] = value;
}

uint8_t Sa1::cpuReadBwWindow(uint16_t offset) const
{
    return readBwBlock(cpuBwBlock, offset);
}

void Sa1::cpuWriteBwWindow(uint16_t offset, uint8_t value)
{
    writeBwBlock(cpuBwBlock, offset, value);
}

uint8_t Sa1::cpuReadBwLinear(uint32_t address) const
{
    if (bwram.empty()) {
        return 0xff;
    }
    return bwram[address & bwramMask];
}

void Sa1::cpuWriteBwLinear(uint32_t address, uint8_t value)
{
    if (bwram.empty()) {
        return;
    }
    bwram[address & bwramMask] = value;
}

// ---------------------------------------------------------------------------
// SA-1 CPU memory map (cpu_r5a22::Bus interface for the internal CPU)
// ---------------------------------------------------------------------------
uint8_t Sa1::read8(uint32_t address)
{
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);

    if (bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf)) {
        if (offset < 0x0800) {
            return iram[offset & 0x7ff];
        }
        if (offset >= 0x2200 && offset <= 0x23ff) {
            return regRead(offset, true);
        }
        if (offset >= 0x3000 && offset <= 0x37ff) {
            return iram[offset & 0x7ff];
        }
        if (offset >= 0x6000 && offset <= 0x7fff) {
            return readBwBlock(sa1BwBlock, offset);
        }
        if (offset >= 0x8000) {
            // Vector redirection: the SA-1 CPU's reset/NMI/IRQ vectors come
            // from CRV/CNV/CIV rather than the cartridge $FFxx vectors.
            if (bank == 0) {
                switch (offset) {
                case 0xfffc: return static_cast<uint8_t>(crv);
                case 0xfffd: return static_cast<uint8_t>(crv >> 8);
                case 0xffea: return static_cast<uint8_t>(cnv);
                case 0xffeb: return static_cast<uint8_t>(cnv >> 8);
                case 0xffee: return static_cast<uint8_t>(civ);
                case 0xffef: return static_cast<uint8_t>(civ >> 8);
                default: break;
                }
            }
            return readRom(mmcRom(address));
        }
        return 0xff;
    }
    if (bank >= 0x40 && bank <= 0x4f) {
        return cpuReadBwLinear(((bank - 0x40) << 16) | offset);
    }
    if (bank >= 0xc0) {
        return readRom(mmcRom(address));
    }
    return 0xff;
}

void Sa1::write8(uint32_t address, uint8_t value)
{
    const uint8_t bank = static_cast<uint8_t>(address >> 16);
    const uint16_t offset = static_cast<uint16_t>(address);

    if (bank <= 0x3f || (bank >= 0x80 && bank <= 0xbf)) {
        if (offset < 0x0800) {
            iram[offset & 0x7ff] = value;
            return;
        }
        if (offset >= 0x2200 && offset <= 0x23ff) {
            regWrite(offset, value, true);
            return;
        }
        if (offset >= 0x3000 && offset <= 0x37ff) {
            iram[offset & 0x7ff] = value;
            return;
        }
        if (offset >= 0x6000 && offset <= 0x7fff) {
            writeBwBlock(sa1BwBlock, offset, value);
            return;
        }
        return;
    }
    if (bank >= 0x40 && bank <= 0x4f) {
        cpuWriteBwLinear(((bank - 0x40) << 16) | offset, value);
    }
}

// ---------------------------------------------------------------------------
// S-CPU side MMIO ($2200-$23FF)
// ---------------------------------------------------------------------------
uint8_t Sa1::cpuReadIo(uint16_t offset)
{
    return regRead(offset, false);
}

void Sa1::cpuWriteIo(uint16_t offset, uint8_t value)
{
    regWrite(offset, value, false);
}

uint8_t Sa1::regRead(uint16_t offset, bool /*fromSa1*/)
{
    switch (offset) {
    case 0x2300: // SFR - read by S-CPU (SA-1 -> S-CPU status)
        return static_cast<uint8_t>((cpuIrqFlag ? 0x80 : 0)
                                    | (cpuIvSel ? 0x40 : 0)
                                    | (chdmaIrqFlag ? 0x20 : 0)
                                    | (cpuNvSel ? 0x10 : 0)
                                    | (messageToCpu & 0x0f));
    case 0x2301: // CFR - read by SA-1 (S-CPU -> SA-1 status)
        return static_cast<uint8_t>((sa1IrqFlag ? 0x80 : 0)
                                    | (timerIrqFlag ? 0x40 : 0)
                                    | (dmaIrqFlag ? 0x20 : 0)
                                    | (sa1NmiFlag ? 0x10 : 0)
                                    | (messageToSa1 & 0x0f));
    case 0x2306: return static_cast<uint8_t>(mathResult);
    case 0x2307: return static_cast<uint8_t>(mathResult >> 8);
    case 0x2308: return static_cast<uint8_t>(mathResult >> 16);
    case 0x2309: return static_cast<uint8_t>(mathResult >> 24);
    case 0x230a: return static_cast<uint8_t>(mathResult >> 32);
    case 0x230b: return mathOverflow ? 0x80 : 0x00; // OF
    case 0x230e: return 0x01; // version code
    default:
        return 0x00;
    }
}

void Sa1::regWrite(uint16_t offset, uint8_t value, bool /*fromSa1*/)
{
    switch (offset) {
    case 0x2200: { // CCNT - SA-1 CPU control
        const bool prevReset = sa1Reset;
        sa1IrqReq = (value & 0x80) != 0;
        sa1Wait = (value & 0x40) != 0;
        sa1Reset = (value & 0x20) != 0;
        const bool nmiReq = (value & 0x10) != 0;
        messageToSa1 = value & 0x0f;
        if (sa1IrqReq) {
            sa1IrqFlag = true;
        }
        if (nmiReq && !sa1NmiReq) {
            sa1NmiFlag = true;
        }
        sa1NmiReq = nmiReq;
        if (prevReset && !sa1Reset) {
            deliverSa1Reset(); // released from reset -> start at CRV
        }
        updateSa1IrqLine();
        break;
    }
    case 0x2201: // SIE
        cpuIrqEnable = (value & 0x80) != 0;
        chdmaIrqEnable = (value & 0x20) != 0;
        break;
    case 0x2202: // SIC - acknowledge SA-1 -> S-CPU interrupts
        if (value & 0x80) {
            cpuIrqFlag = false;
        }
        if (value & 0x20) {
            chdmaIrqFlag = false;
        }
        break;
    case 0x2203: crv = (crv & 0xff00) | value; break;            // CRV low
    case 0x2204: crv = (crv & 0x00ff) | (value << 8); break;     // CRV high
    case 0x2205: cnv = (cnv & 0xff00) | value; break;            // CNV low
    case 0x2206: cnv = (cnv & 0x00ff) | (value << 8); break;     // CNV high
    case 0x2207: civ = (civ & 0xff00) | value; break;            // CIV low
    case 0x2208: civ = (civ & 0x00ff) | (value << 8); break;     // CIV high
    case 0x2209: { // SCNT - SA-1 -> S-CPU control
        const bool req = (value & 0x80) != 0;
        cpuIvSel = (value & 0x40) != 0;
        cpuNvSel = (value & 0x10) != 0;
        messageToCpu = value & 0x0f;
        if (req && !cpuIrqReq) {
            cpuIrqFlag = true;
        }
        cpuIrqReq = req;
        break;
    }
    case 0x220a: // CIE
        sa1IrqEnable = (value & 0x80) != 0;
        timerIrqEnable = (value & 0x40) != 0;
        dmaIrqEnable = (value & 0x20) != 0;
        sa1NmiEnable = (value & 0x10) != 0;
        updateSa1IrqLine();
        break;
    case 0x220b: // CIC - acknowledge S-CPU -> SA-1 interrupts
        if (value & 0x80) {
            sa1IrqFlag = false;
        }
        if (value & 0x40) {
            timerIrqFlag = false;
        }
        if (value & 0x20) {
            dmaIrqFlag = false;
        }
        if (value & 0x10) {
            sa1NmiFlag = false;
        }
        updateSa1IrqLine();
        break;
    case 0x220c: snv = (snv & 0xff00) | value; break;            // SNV low
    case 0x220d: snv = (snv & 0x00ff) | (value << 8); break;     // SNV high
    case 0x220e: siv = (siv & 0xff00) | value; break;            // SIV low
    case 0x220f: siv = (siv & 0x00ff) | (value << 8); break;     // SIV high
    case 0x2220: romBankMode[0] = (value & 0x80) != 0; romBank[0] = value & 0x07; break;
    case 0x2221: romBankMode[1] = (value & 0x80) != 0; romBank[1] = value & 0x07; break;
    case 0x2222: romBankMode[2] = (value & 0x80) != 0; romBank[2] = value & 0x07; break;
    case 0x2223: romBankMode[3] = (value & 0x80) != 0; romBank[3] = value & 0x07; break;
    case 0x2224: cpuBwBlock = value & 0x1f; break;               // BMAPS
    case 0x2225: // BMAP
        sa1BwBitmap = (value & 0x80) != 0;
        sa1BwBlock = value & 0x7f;
        break;
    case 0x2250: // arithmetic control
        arithDivide = (value & 0x01) != 0;
        arithSum = (value & 0x02) != 0;
        if (arithSum) {
            mathResult = 0;
            mathOverflow = false;
        }
        break;
    case 0x2230: // DCNT - DMA control
        dmaSource = value & 0x03;
        dmaDest = (value >> 2) & 0x01;
        ccType1 = (value >> 4) & 0x01;
        ccEnable = (value >> 5) & 0x01;
        dmaPriority = (value >> 6) & 0x01;
        dmaEnable = (value >> 7) & 0x01;
        if (!ccEnable) {
            ccActive = false;
        }
        break;
    case 0x2231: // CDMA - character-conversion parameters
        ccColorBits = value & 0x03;
        ccSize = (value >> 2) & 0x07;
        if (value & 0x80) { // CHDEND: terminate type-1 conversion
            ccActive = false;
            ccLine = 0;
        }
        break;
    case 0x2232: dmaSourceAddr = (dmaSourceAddr & 0xffff00) | value; break;       // DSA low
    case 0x2233: dmaSourceAddr = (dmaSourceAddr & 0xff00ff) | (value << 8); break; // DSA mid
    case 0x2234: dmaSourceAddr = (dmaSourceAddr & 0x00ffff) | (value << 16); break; // DSA high
    case 0x2235: dmaDestAddr = (dmaDestAddr & 0xffff00) | value; break;           // DDA low
    case 0x2236: // DDA mid - triggers normal DMA (dest I-RAM) or type-1 CC DMA
        dmaDestAddr = (dmaDestAddr & 0xff00ff) | (value << 8);
        if (dmaEnable && !ccEnable && dmaDest == 0) {
            dmaNormal();
        } else if (dmaEnable && ccEnable && ccType1) {
            ccActive = true; // type-1: subsequent I-RAM reads convert on the fly
            ccLine = 0;
            chdmaIrqFlag = true;
        }
        break;
    case 0x2237: // DDA high - triggers normal DMA (dest BW-RAM)
        dmaDestAddr = (dmaDestAddr & 0x00ffff) | (value << 16);
        if (dmaEnable && !ccEnable && dmaDest == 1) {
            dmaNormal();
        }
        break;
    case 0x2238: dmaCount = (dmaCount & 0xff00) | value; break;        // DTC low
    case 0x2239: dmaCount = (dmaCount & 0x00ff) | (value << 8); break; // DTC high
    case 0x223f: bitmapFormat = (value & 0x80) != 0; break;           // BBF
    case 0x2240: case 0x2241: case 0x2242: case 0x2243:
    case 0x2244: case 0x2245: case 0x2246: case 0x2247:
    case 0x2248: case 0x2249: case 0x224a: case 0x224b:
    case 0x224c: case 0x224d: case 0x224e: case 0x224f: // BRF
        ccBrf[offset & 0x0f] = value;
        // Filling a half of the register file (8 bytes) triggers a type-2
        // character conversion of that line.
        if (dmaEnable && ccEnable && !ccType1 && (offset == 0x2247 || offset == 0x224f)) {
            dmaCharConv2();
        }
        break;
    case 0x2251: mathA = (mathA & 0xff00) | value; break;        // MA low
    case 0x2252: mathA = (mathA & 0x00ff) | (value << 8); break; // MA high
    case 0x2253: mathB = (mathB & 0xff00) | value; break;        // MB low
    case 0x2254: // MB high - triggers the operation
        mathB = (mathB & 0x00ff) | (value << 8);
        runArithmetic();
        break;
    default:
        break;
    }
}

void Sa1::runArithmetic()
{
    if (arithSum) {
        // Cumulative sum (multiply-accumulate), 40-bit signed accumulator.
        const int64_t product = static_cast<int64_t>(static_cast<int16_t>(mathA))
                              * static_cast<int16_t>(mathB);
        int64_t acc = static_cast<int64_t>(mathResult);
        acc += product;
        if (acc < 0 || acc > 0xffffffffffLL) {
            mathOverflow = true;
        }
        mathResult = static_cast<uint64_t>(acc) & 0xffffffffffULL;
    } else if (!arithDivide) {
        // Signed 16x16 -> 32-bit multiply.
        const int32_t product = static_cast<int32_t>(static_cast<int16_t>(mathA))
                             * static_cast<int16_t>(mathB);
        mathResult = static_cast<uint32_t>(product);
    } else {
        // Divide: signed 16-bit dividend / unsigned 16-bit divisor.
        // Quotient (signed) in low word, remainder (unsigned) in high word,
        // with floored division so the remainder is always non-negative.
        const int32_t dividend = static_cast<int16_t>(mathA);
        const int32_t divisor = static_cast<uint16_t>(mathB);
        int32_t quotient = 0;
        int32_t remainder = dividend;
        if (divisor != 0) {
            quotient = dividend / divisor;
            remainder = dividend % divisor;
            if (remainder < 0) {
                quotient -= 1;
                remainder += divisor;
            }
        }
        mathResult = (static_cast<uint32_t>(remainder & 0xffff) << 16)
                   | static_cast<uint16_t>(quotient);
    }
    mathB = 0;
}

// ---------------------------------------------------------------------------
// DMA and character-conversion DMA
// ---------------------------------------------------------------------------
uint8_t Sa1::dmaSourceByte(uint32_t address) const
{
    switch (dmaSource) {
    case 0: return readRom(mmcRom(address));                        // ROM
    case 1: return bwram.empty() ? 0 : bwram[address & bwramMask];  // BW-RAM
    case 2: return iram[address & 0x7ff];                           // I-RAM
    default: return 0;
    }
}

void Sa1::dmaDestByte(uint32_t address, uint8_t value)
{
    if (dmaDest == 0) {
        iram[address & 0x7ff] = value; // I-RAM
    } else if (!bwram.empty()) {
        bwram[address & bwramMask] = value; // BW-RAM
    }
}

void Sa1::dmaNormal()
{
    // Copy DTC bytes from the source device to the destination device.
    for (uint32_t i = 0; i < dmaCount; ++i) {
        dmaDestByte(dmaDestAddr, dmaSourceByte(dmaSourceAddr));
        dmaSourceAddr = (dmaSourceAddr + 1) & 0xffffff;
        dmaDestAddr = (dmaDestAddr + 1) & 0xffffff;
    }
    dmaIrqFlag = true;
    updateSa1IrqLine();
}

// Character-conversion type 2: the SA-1 CPU fills an 8-pixel "line" of the BRF
// register file; this converts it to an SNES planar bitplane row in I-RAM.
void Sa1::dmaCharConv2()
{
    const uint32_t bpp = 2u << (2 - ccColorBits); // 8, 4 or 2 bits per pixel
    const uint8_t* brf = &ccBrf[(ccLine & 1) << 3];
    uint32_t address = dmaDestAddr & 0x7ff;
    address &= ~((1u << (7 - ccColorBits)) - 1);
    address += (ccLine & 8) * bpp;
    address += (ccLine & 7) * 2;
    for (uint32_t p = 0; p < bpp; ++p) {
        uint8_t output = 0;
        for (uint32_t x = 0; x < 8; ++x) {
            output |= static_cast<uint8_t>(((brf[x] >> p) & 1) << (7 - x));
        }
        const uint32_t planeAddr = (address + ((p & 6) << 3) + (p & 1)) & 0x7ff;
        iram[planeAddr] = output;
    }
    ccLine = (ccLine + 1) & 15;
}

// Character-conversion type 1: while active, S-CPU I-RAM reads return tile data
// built on the fly from the linear BW-RAM bitmap at the DMA source address.
uint8_t Sa1::ccDmaRead(uint16_t iramOffset)
{
    const uint32_t bpp = 2u << (2 - ccColorBits);
    const uint32_t tileSize = 8u * bpp; // planar bytes per 8x8 tile
    const uint32_t rel = (iramOffset - (dmaDestAddr & 0x7ff)) & 0x7ff;
    const uint32_t tile = rel / tileSize;
    const uint32_t byteInTile = rel % tileSize;
    const uint32_t row = (byteInTile & 0x0f) >> 1;
    const uint32_t plane = ((byteInTile >> 4) << 1) | (byteInTile & 1);
    const uint32_t vramWidth = 1u << ccSize;      // tiles across
    const uint32_t tileX = tile % vramWidth;
    const uint32_t tileY = tile / vramWidth;
    const uint32_t bitmapWidth = vramWidth * 8;   // pixels across
    const uint32_t pixelMask = (1u << bpp) - 1;
    uint8_t output = 0;
    for (uint32_t x = 0; x < 8; ++x) {
        uint8_t pixel = 0;
        if (!bwram.empty()) {
            const uint32_t pixelIndex = (tileY * 8 + row) * bitmapWidth + (tileX * 8 + x);
            const uint32_t bitPos = pixelIndex * bpp;
            const uint32_t byteAddr = (dmaSourceAddr + (bitPos >> 3)) & bwramMask;
            pixel = static_cast<uint8_t>((bwram[byteAddr] >> (bitPos & 7)) & pixelMask);
        }
        output |= static_cast<uint8_t>(((pixel >> plane) & 1) << (7 - x));
    }
    return output;
}

// ---------------------------------------------------------------------------
// Interrupt delivery / reset
// ---------------------------------------------------------------------------
void Sa1::updateSa1IrqLine()
{
    const bool irq = (sa1IrqFlag && sa1IrqEnable) || (timerIrqFlag && timerIrqEnable)
                  || (dmaIrqFlag && dmaIrqEnable);
    sa1Cpu.setIrqLine(irq);

    const bool nmi = sa1NmiFlag && sa1NmiEnable;
    if (nmi && !sa1NmiLine) {
        sa1Cpu.requestNMI();
    }
    sa1NmiLine = nmi;
}

void Sa1::deliverSa1Reset()
{
    sa1Cpu.reset(); // reads $00:FFFC -> intercepted -> CRV
    budget = 0;
}

// ---------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------
void Sa1::stepSa1(uint32_t masterClocks)
{
    // SA-1 runs at master/2 (10.74 MHz).
    clockRemainder += masterClocks;
    const uint32_t sa1Cycles = clockRemainder / 2;
    clockRemainder &= 1;

    if (sa1Reset || sa1Wait) {
        budget = 0;
        return;
    }
    updateSa1IrqLine();

    budget += sa1Cycles;
    while (budget > 0 && !sa1Reset && !sa1Wait) {
        if (sa1Cpu.stopped()) {
            budget = 0;
            break;
        }
        uint32_t used = sa1Cpu.step();
        if (used == 0) {
            used = 1;
        }
        budget -= used;
    }
}

// ---------------------------------------------------------------------------
// Save state
// ---------------------------------------------------------------------------
void Sa1::saveState(std::vector<uint8_t>& out) const
{
    const cpu_r5a22::CPU::SaveState cpuState = sa1Cpu.saveState();
    appendPod(out, cpuState);
    appendPod(out, iram);
    appendPod(out, clockRemainder);
    appendPod(out, sa1IrqReq);
    appendPod(out, sa1Wait);
    appendPod(out, sa1Reset);
    appendPod(out, sa1NmiReq);
    appendPod(out, messageToSa1);
    appendPod(out, cpuIrqEnable);
    appendPod(out, chdmaIrqEnable);
    appendPod(out, crv);
    appendPod(out, cnv);
    appendPod(out, civ);
    appendPod(out, cpuIrqReq);
    appendPod(out, cpuIvSel);
    appendPod(out, cpuNvSel);
    appendPod(out, messageToCpu);
    appendPod(out, sa1IrqEnable);
    appendPod(out, timerIrqEnable);
    appendPod(out, dmaIrqEnable);
    appendPod(out, sa1NmiEnable);
    appendPod(out, snv);
    appendPod(out, siv);
    appendPod(out, romBank);
    appendPod(out, romBankMode);
    appendPod(out, cpuBwBlock);
    appendPod(out, sa1BwBlock);
    appendPod(out, sa1BwBitmap);
    appendPod(out, cpuIrqFlag);
    appendPod(out, chdmaIrqFlag);
    appendPod(out, sa1IrqFlag);
    appendPod(out, timerIrqFlag);
    appendPod(out, dmaIrqFlag);
    appendPod(out, sa1NmiFlag);
    appendPod(out, sa1NmiLine);
    appendPod(out, arithSum);
    appendPod(out, arithDivide);
    appendPod(out, mathA);
    appendPod(out, mathB);
    appendPod(out, mathResult);
    appendPod(out, mathOverflow);
    appendPod(out, dmaEnable);
    appendPod(out, dmaPriority);
    appendPod(out, ccEnable);
    appendPod(out, ccType1);
    appendPod(out, dmaDest);
    appendPod(out, dmaSource);
    appendPod(out, ccColorBits);
    appendPod(out, ccSize);
    appendPod(out, dmaSourceAddr);
    appendPod(out, dmaDestAddr);
    appendPod(out, dmaCount);
    appendPod(out, bitmapFormat);
    appendPod(out, ccBrf);
    appendPod(out, ccLine);
    appendPod(out, ccActive);
    appendPod(out, static_cast<uint32_t>(bwram.size()));
    out.insert(out.end(), bwram.begin(), bwram.end());
}

bool Sa1::loadState(const uint8_t* data, size_t size)
{
    const uint8_t* pos = data;
    const uint8_t* end = data + size;
    cpu_r5a22::CPU::SaveState cpuState;
    if (!readPod(pos, end, cpuState) || !readPod(pos, end, iram)
        || !readPod(pos, end, clockRemainder) || !readPod(pos, end, sa1IrqReq)
        || !readPod(pos, end, sa1Wait) || !readPod(pos, end, sa1Reset)
        || !readPod(pos, end, sa1NmiReq) || !readPod(pos, end, messageToSa1)
        || !readPod(pos, end, cpuIrqEnable) || !readPod(pos, end, chdmaIrqEnable)
        || !readPod(pos, end, crv) || !readPod(pos, end, cnv) || !readPod(pos, end, civ)
        || !readPod(pos, end, cpuIrqReq) || !readPod(pos, end, cpuIvSel)
        || !readPod(pos, end, cpuNvSel) || !readPod(pos, end, messageToCpu)
        || !readPod(pos, end, sa1IrqEnable) || !readPod(pos, end, timerIrqEnable)
        || !readPod(pos, end, dmaIrqEnable) || !readPod(pos, end, sa1NmiEnable)
        || !readPod(pos, end, snv) || !readPod(pos, end, siv)
        || !readPod(pos, end, romBank) || !readPod(pos, end, romBankMode)
        || !readPod(pos, end, cpuBwBlock) || !readPod(pos, end, sa1BwBlock)
        || !readPod(pos, end, sa1BwBitmap) || !readPod(pos, end, cpuIrqFlag)
        || !readPod(pos, end, chdmaIrqFlag) || !readPod(pos, end, sa1IrqFlag)
        || !readPod(pos, end, timerIrqFlag) || !readPod(pos, end, dmaIrqFlag)
        || !readPod(pos, end, sa1NmiFlag) || !readPod(pos, end, sa1NmiLine)
        || !readPod(pos, end, arithSum) || !readPod(pos, end, arithDivide)
        || !readPod(pos, end, mathA) || !readPod(pos, end, mathB)
        || !readPod(pos, end, mathResult) || !readPod(pos, end, mathOverflow)
        || !readPod(pos, end, dmaEnable) || !readPod(pos, end, dmaPriority)
        || !readPod(pos, end, ccEnable) || !readPod(pos, end, ccType1)
        || !readPod(pos, end, dmaDest) || !readPod(pos, end, dmaSource)
        || !readPod(pos, end, ccColorBits) || !readPod(pos, end, ccSize)
        || !readPod(pos, end, dmaSourceAddr) || !readPod(pos, end, dmaDestAddr)
        || !readPod(pos, end, dmaCount) || !readPod(pos, end, bitmapFormat)
        || !readPod(pos, end, ccBrf) || !readPod(pos, end, ccLine)
        || !readPod(pos, end, ccActive)) {
        return false;
    }
    sa1Cpu.loadState(cpuState);
    uint32_t bwBytes = 0;
    if (!readPod(pos, end, bwBytes) || static_cast<size_t>(end - pos) != bwBytes) {
        return false;
    }
    bwram.assign(pos, pos + bwBytes);
    bwramMask = bwram.empty() ? 0 : static_cast<uint32_t>(bwram.size() - 1);
    budget = 0;
    return true;
}

} // namespace snesquik::sa1
