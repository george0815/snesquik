#include "S-PPU/ppu.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace snesquik::ppu {

void Ppu::saveState(std::vector<uint8_t>& out) const
{
    static_assert(std::is_trivially_copyable_v<Ppu>, "Ppu state must be a flat blob");
    const auto* bytes = reinterpret_cast<const uint8_t*>(this);
    out.insert(out.end(), bytes, bytes + sizeof(Ppu));
}

bool Ppu::loadState(const uint8_t* data, size_t size)
{
    if (size != sizeof(Ppu)) {
        return false;
    }
    std::memcpy(reinterpret_cast<uint8_t*>(this), data, sizeof(Ppu));
    return true;
}

namespace {

uint8_t expand5(uint8_t value)
{
    return static_cast<uint8_t>((value << 3) | (value >> 2));
}

uint8_t clamp5(int value)
{
    return static_cast<uint8_t>(std::clamp(value, 0, 31));
}

uint16_t remapVramAddress(uint16_t address, uint8_t vmain);

} // namespace

void Ppu::reset()
{
    vram.fill(0);
    cgram.fill(0);
    oam.fill(0);
    registers.fill(0);
    frame.fill(0xff000000);
    priorityFrame.fill(0);
    inidisp = 0x80;
    objsel = 0;
    bgmode = 0;
    mosaic = 0;
    bg12nba = 0;
    bg34nba = 0;
    m7sel = 0;
    setini = 0;
    mainScreen = 0;
    subScreen = 0;
    w12sel = 0;
    w34sel = 0;
    wobjsel = 0;
    wh0 = 0;
    wh1 = 0;
    wh2 = 0;
    wh3 = 0;
    wbglog = 0;
    wobjlog = 0;
    tmw = 0;
    tsw = 0;
    cgwsel = 0;
    cgadsub = 0;
    coldata = 0;
    vramAddress = 0;
    vmain = 0;
    vramReadLatch = 0;
    cgramAddress = 0;
    cgramLatch = 0;
    cgramHighByte = false;
    oamAddress = 0;
    internalOamAddress = 0;
    oamLatch = 0;
    oamHighByte = false;
    bgofsLatches = {};
    mode7Latch = 0;
    m7hofs = 0;
    m7vofs = 0;
    m7a = 0;
    m7b = 0;
    m7c = 0;
    m7d = 0;
    m7x = 0;
    m7y = 0;
    mode7Multiply = 0;
    hCounter = 0;
    vCounter = 0;
    latchedHCounter = 0;
    latchedVCounter = 0;
    hCounterHigh = false;
    vCounterHigh = false;
    counterLatch = false;
    evenField = true;
    spriteTimeOver = false;
    spriteRangeOver = false;
    bgState = {};
}

uint8_t Ppu::readRegister(uint16_t address)
{
    switch (address) {
    case 0x2134:
        return static_cast<uint8_t>(mode7Multiply);
    case 0x2135:
        return static_cast<uint8_t>(mode7Multiply >> 8);
    case 0x2136:
        return static_cast<uint8_t>(mode7Multiply >> 16);
    case 0x2137:
        if (!counterLatch) {
            latchedHCounter = hCounter;
            latchedVCounter = vCounter;
            counterLatch = true;
        }
        return registers[address - 0x2100];
    case 0x2138:
        return oam[internalOamAddress++ % oam.size()];
    case 0x2139:
        return readVramDataLow();
    case 0x213a:
        return readVramDataHigh();
    case 0x213b:
        return readCgramData();
    case 0x213c: {
        const uint8_t value = hCounterHigh ? static_cast<uint8_t>((latchedHCounter >> 8) & 0x01) : static_cast<uint8_t>(latchedHCounter);
        hCounterHigh = !hCounterHigh;
        return value;
    }
    case 0x213d: {
        const uint8_t value = vCounterHigh ? static_cast<uint8_t>((latchedVCounter >> 8) & 0x01) : static_cast<uint8_t>(latchedVCounter);
        vCounterHigh = !vCounterHigh;
        return value;
    }
    case 0x213e:
        return static_cast<uint8_t>(0x01 | (spriteRangeOver ? 0x40 : 0) | (spriteTimeOver ? 0x80 : 0));
    case 0x213f: {
        const uint8_t value = static_cast<uint8_t>(0x03 | (counterLatch ? 0x40 : 0) | (evenField ? 0x00 : 0x80));
        hCounterHigh = false;
        vCounterHigh = false;
        counterLatch = false;
        return value;
    }
    default:
        if (address >= 0x2100 && address <= 0x213f) {
            return registers[address - 0x2100];
        }
        return 0xff;
    }
}

void Ppu::writeRegister(uint16_t address, uint8_t value)
{
    if (address >= 0x2100 && address <= 0x213f) {
        registers[address - 0x2100] = value;
    }

    switch (address) {
    case 0x2100:
        inidisp = value;
        inidispWriteCount++;
        lastInidispWritten = value;
        break;
    case 0x2101:
        objsel = value;
        break;
    case 0x2105:
        bgmode = value;
        break;
    case 0x2106:
        mosaic = value;
        break;
    case 0x2107:
    case 0x2108:
    case 0x2109:
    case 0x210a:
        bgState[address - 0x2107].screen = value;
        break;
    case 0x210b:
        bg12nba = value;
        break;
    case 0x210c:
        bg34nba = value;
        break;
    case 0x210d:
        writeBgScroll(0, false, value);
        writeMode7Pair(m7hofs, value);
        break;
    case 0x210e:
        writeBgScroll(0, true, value);
        writeMode7Pair(m7vofs, value);
        break;
    case 0x210f:
        writeBgScroll(1, false, value);
        break;
    case 0x2110:
        writeBgScroll(1, true, value);
        break;
    case 0x2111:
        writeBgScroll(2, false, value);
        break;
    case 0x2112:
        writeBgScroll(2, true, value);
        break;
    case 0x2113:
        writeBgScroll(3, false, value);
        break;
    case 0x2114:
        writeBgScroll(3, true, value);
        break;
    case 0x211a:
        m7sel = value;
        break;
    case 0x211b:
        writeMode7Pair(m7a, value);
        mode7Multiply = static_cast<int16_t>(m7a) * static_cast<int8_t>(m7b >> 8);
        break;
    case 0x211c:
        writeMode7Pair(m7b, value);
        mode7Multiply = static_cast<int16_t>(m7a) * static_cast<int8_t>(value);
        break;
    case 0x211d:
        writeMode7Pair(m7c, value);
        break;
    case 0x211e:
        writeMode7Pair(m7d, value);
        break;
    case 0x211f:
        writeMode7Pair(m7x, value);
        break;
    case 0x2120:
        writeMode7Pair(m7y, value);
        break;
    case 0x2102:
        oamAddress = static_cast<uint16_t>((oamAddress & 0x0100) | value);
        internalOamAddress = static_cast<uint16_t>((oamAddress & 0x01ff) << 1);
        oamHighByte = false;
        break;
    case 0x2103:
        oamAddress = static_cast<uint16_t>(((value & 0x01) << 8) | (oamAddress & 0x00ff));
        internalOamAddress = static_cast<uint16_t>((oamAddress & 0x01ff) << 1);
        oamHighByte = false;
        break;
    case 0x2104:
        writeOamData(value);
        break;
    case 0x2115:
        vmain = value;
        break;
    case 0x2116:
        vramAddress = static_cast<uint16_t>((vramAddress & 0xff00) | value);
        vramReadLatch = vram[remapVramAddress(vramAddress, vmain) & 0x7fff];
        break;
    case 0x2117:
        vramAddress = static_cast<uint16_t>((vramAddress & 0x00ff) | (value << 8));
        vramReadLatch = vram[remapVramAddress(vramAddress, vmain) & 0x7fff];
        break;
    case 0x2118:
        writeVramDataLow(value);
        break;
    case 0x2119:
        writeVramDataHigh(value);
        break;
    case 0x2121:
        cgramAddress = value;
        cgramHighByte = false;
        break;
    case 0x2122:
        writeCgramData(value);
        break;
    case 0x2123:
        w12sel = value;
        break;
    case 0x2124:
        w34sel = value;
        break;
    case 0x2125:
        wobjsel = value;
        break;
    case 0x2126:
        wh0 = value;
        break;
    case 0x2127:
        wh1 = value;
        break;
    case 0x2128:
        wh2 = value;
        break;
    case 0x2129:
        wh3 = value;
        break;
    case 0x212a:
        wbglog = value;
        break;
    case 0x212b:
        wobjlog = value;
        break;
    case 0x212c:
        mainScreen = value;
        break;
    case 0x212d:
        subScreen = value;
        break;
    case 0x212e:
        tmw = value;
        break;
    case 0x212f:
        tsw = value;
        break;
    case 0x2130:
        cgwsel = value;
        break;
    case 0x2131:
        cgadsub = value;
        break;
    case 0x2132: {
        const uint16_t component = value & 0x1f;
        if ((value & 0x20) != 0) {
            coldata = static_cast<uint16_t>((coldata & ~0x001f) | component);
        }
        if ((value & 0x40) != 0) {
            coldata = static_cast<uint16_t>((coldata & ~0x03e0) | (component << 5));
        }
        if ((value & 0x80) != 0) {
            coldata = static_cast<uint16_t>((coldata & ~0x7c00) | (component << 10));
        }
        break;
    }
    case 0x2133:
        setini = value;
        break;
    default:
        break;
    }
}

void Ppu::renderFrame()
{
    tileCacheKeys.fill(0);
    frame.fill(0xff000000);
    priorityFrame.fill(0);
    spriteTimeOver = false;
    spriteRangeOver = false;

    for (int y = 0; y < visibleHeight(); ++y) {
        renderScanline(static_cast<uint16_t>(y));
    }
}

void Ppu::renderScanline(uint16_t y)
{
    if (y >= screenHeight) {
        return;
    }

    if (forceBlank() || y > static_cast<uint16_t>(visibleHeight())) {
        const size_t base = static_cast<size_t>(y * screenWidth);
        std::fill_n(&frame[base], screenWidth, 0xff000000);
        std::fill_n(&priorityFrame[base], screenWidth, 0);
        return;
    }

    tileCacheKeys.fill(0);
    buildVisibleSpriteList(y);

    if ((debugFlags & DebugOnlyBG3) != 0) {
        renderBgLine(2, y);
        for (int x = 0; x < screenWidth; ++x) {
            const size_t offset = static_cast<size_t>(y * screenWidth + x);
            if (bgOpaque[2][x]) {
                frame[offset] = rgbaFromSnesColor(bgLineBuffer[2][x]);
            } else {
                frame[offset] = rgbaFromSnesColor(cgram[0]);
            }
            priorityFrame[offset] = bgPriBuffer[2][x];
        }
        return;
    }

    if (std::getenv("SNESQUIK_TMLOG") && (y % 28) == 0) {
        std::fprintf(stderr, "[tm] y=%u bgmode=%02x main=%02x sub=%02x m7sel=%02x\n",
                     y, bgmode, mainScreen, subScreen, m7sel);
    }
    const uint8_t eitherScreen = static_cast<uint8_t>(mainScreen | subScreen);
    for (size_t bg = 0; bg < 4; ++bg) {
        if (eitherScreen & (1u << bg)) {
            renderBgLine(bg, y);
        }
    }
    if (eitherScreen & 0x10) {
        renderObjLine(y);
    }

    for (int x = 0; x < screenWidth; ++x) {
        Pixel main = composeFromBuffers(x, false);

        uint16_t color;
        if ((debugFlags & DebugNoColorMath) != 0) {
            color = main.color;
        } else {
            Pixel sub = composeFromBuffers(x, true);
            color = applyColorMath(main, sub, x);
        }
        const size_t offset = static_cast<size_t>(y * screenWidth + x);
        frame[offset] = rgbaFromSnesColor(color);
        priorityFrame[offset] = main.priority;
    }
}

void Ppu::buildVisibleSpriteList(int y)
{
    visibleSpriteCount = 0;
    int spritesOnLine = 0;
    int tilesOnLine = 0;

    for (int sprite = 0; sprite < 128; ++sprite) {
        int spriteY = 0;
        int width = 0;
        int height = 0;
        if (!spriteVisibleOnLine(sprite, y, spriteY, width, height)) {
            continue;
        }

        if (spritesOnLine >= 32) {
            spriteTimeOver = true;
            continue;
        }
        ++spritesOnLine;

        const int spriteTiles = std::max(1, width / 8);
        if (tilesOnLine + spriteTiles > 34) {
            spriteRangeOver = true;
            continue;
        }
        tilesOnLine += spriteTiles;

        int spriteX = 0;
        int fullY = 0;
        int fullWidth = 0;
        int fullHeight = 0;
        if (!spriteBounds(sprite, spriteX, fullY, fullWidth, fullHeight)) {
            continue;
        }

        auto& entry = visibleSprites[visibleSpriteCount++];
        entry.spriteIndex = sprite;
        entry.x = spriteX;
        entry.y = spriteY;
        entry.width = fullWidth;
        entry.height = fullHeight;
        entry.priority = objPriorityValue(static_cast<uint8_t>((oam[sprite * 4 + 3] >> 4) & 0x03));
    }
}

void Ppu::renderBgLine(size_t bg, int y)
{
    auto& colorBuf = bgLineBuffer[bg];
    auto& priBuf = bgPriBuffer[bg];
    auto& opaqueBuf = bgOpaque[bg];

    std::fill_n(&colorBuf[0], screenWidth, uint16_t(0));
    std::fill_n(&priBuf[0], screenWidth, uint8_t(0));
    std::fill_n(&opaqueBuf[0], screenWidth, uint8_t(0));

    if ((bgmode & 0x07) == 7) {
        for (int x = 0; x < screenWidth; ++x) {
            Pixel p = sampleMode7(bg, x, y, false);
            if (p.opaque) {
                colorBuf[x] = p.color;
                priBuf[x] = p.priority;
                opaqueBuf[x] = 1;
            }
        }
        return;
    }

    const uint8_t bpp = bppForBg(bg);
    if (bpp == 0) {
        return;
    }

    const uint8_t mode = bgmode & 0x07;
    const BgState& state = bgState[bg];
    const int mosaicSize = ((mosaic >> 4) & 0x0f) + 1;
    const bool largeTiles = (bgmode & (0x10u << bg)) != 0;
    const int tileSize = largeTiles ? 16 : 8;

    const bool isOptMode = (mode == 2 || mode == 4 || mode == 6) && bg < 2;
    const BgState& bg3 = bgState[2];

    std::array<int16_t, 33> tileHScroll{};
    std::array<int16_t, 33> tileVScroll{};

    if (isOptMode) {
        const int bg3FineY = bg3.vscroll & 0x07;
        for (int t = 0; t <= 32; ++t) {
            if (t == 0) {
                tileHScroll[0] = static_cast<int16_t>(state.hscroll);
                tileVScroll[0] = static_cast<int16_t>(state.vscroll);
                continue;
            }
            const int offsetCol = ((t - 1) + (bg3.hscroll >> 3)) & 0x1f;
            const uint16_t hEntry = offsetMapEntry(offsetCol, bg3FineY);
            const uint16_t vEntry = offsetMapEntry(offsetCol, (bg3FineY + 8) & 0x1f);
            const bool hApplies = (bg == 0) ? ((hEntry & 0x2000) != 0) : ((hEntry & 0x4000) != 0);
            const bool vApplies = (bg == 0) ? ((vEntry & 0x2000) != 0) : ((vEntry & 0x4000) != 0);
            if (!hApplies && !vApplies) {
                tileHScroll[t] = static_cast<int16_t>(state.hscroll);
                tileVScroll[t] = static_cast<int16_t>(state.vscroll);
                continue;
            }
            if (mode == 4) {
                const bool isVertical = (hEntry & 0x8000) != 0;
                if (isVertical) {
                    tileHScroll[t] = static_cast<int16_t>(state.hscroll);
                    tileVScroll[t] = static_cast<int16_t>(vApplies ? (vEntry & 0x03ff) : state.vscroll);
                } else {
                    tileHScroll[t] = static_cast<int16_t>(hApplies ? ((hEntry & 0x1ff8) | (state.hscroll & 0x07)) : state.hscroll);
                    tileVScroll[t] = static_cast<int16_t>(state.vscroll);
                }
            } else {
                tileHScroll[t] = static_cast<int16_t>(hApplies ? ((hEntry & 0x1ff8) | (state.hscroll & 0x07)) : state.hscroll);
                tileVScroll[t] = static_cast<int16_t>(vApplies ? (vEntry & 0x03ff) : state.vscroll);
            }
        }
    }

    for (int x = 0; x < screenWidth; ++x) {
        int sampleX = x;
        int sampleY = y;
        if ((mosaic & (1u << bg)) != 0 && mosaicSize > 1) {
            sampleX -= sampleX % mosaicSize;
            sampleY -= sampleY % mosaicSize;
        }

        int16_t hscroll = state.hscroll;
        int16_t vscroll = state.vscroll;

        if (isOptMode) {
            const int fineX = sampleX & 0x07;
            const int baseTileX = (sampleX + state.hscroll) >> 3;
            const int tileCol = (baseTileX - (state.hscroll >> 3)) & 0x1f;
            hscroll = tileHScroll[tileCol];
            vscroll = tileVScroll[tileCol];
            sampleX = (sampleX & ~7) | fineX;
        }

        const int worldX = (sampleX + hscroll) & 0x03ff;
        const int worldY = (sampleY + vscroll) & 0x03ff;
        const int tileX = worldX / tileSize;
        const int tileY = worldY / tileSize;
        const uint16_t entry = tilemapEntry(bg, tileX, tileY);
        const uint16_t baseTile = entry & 0x03ff;
        const uint8_t palette = static_cast<uint8_t>((entry >> 10) & 0x07);
        const bool priorityBit = (entry & 0x2000) != 0;
        const bool hflip = (entry & 0x4000) != 0;
        const bool vflip = (entry & 0x8000) != 0;

        int pixelX = worldX & (tileSize - 1);
        int pixelY = worldY & (tileSize - 1);
        uint16_t tile = baseTile;
        if (largeTiles) {
            if (pixelX >= 8) { tile += 1; pixelX -= 8; }
            if (pixelY >= 8) { tile += 16; pixelY -= 8; }
        }

        const uint8_t pixel = decodeTilePixel(tile, bpp, pixelX, pixelY, hflip, vflip, chrBase(bg));
        if (pixel == 0) {
            continue;
        }

        const uint8_t idx = cgramIndexForPixel(bg, bpp, palette, pixel);
        colorBuf[x] = ((cgwsel & 0x01) != 0 && bpp == 8) ? directColor(palette, pixel) : cgram[idx];
        priBuf[x] = bgPriorityValue(bg, priorityBit);
        opaqueBuf[x] = 1;
    }
}

void Ppu::renderObjLine(int y)
{
    std::fill_n(&objLineBuffer[0], screenWidth, uint16_t(0));
    std::fill_n(&objPriBuffer[0], screenWidth, uint8_t(0));
    std::fill_n(&objOpaque[0], screenWidth, uint8_t(0));
    std::fill_n(&objPalBuffer[0], screenWidth, uint8_t(0));

    for (int s = 0; s < visibleSpriteCount; ++s) {
        const auto& sprite = visibleSprites[s];
        const int xStart = std::max(0, sprite.x);
        const int xEnd = std::min(screenWidth, sprite.x + sprite.width);

        for (int x = xStart; x < xEnd; ++x) {
            // Among overlapping sprites the lowest OAM index wins the pixel
            // regardless of its OBJ priority (which only orders it against
            // backgrounds). Games layer sprites by OAM position alone.
            if (objOpaque[x]) {
                continue;
            }
            Pixel p = sampleSpritePixel(sprite.spriteIndex, x - sprite.x, y - sprite.y);
            if (!p.opaque) {
                continue;
            }
            objLineBuffer[x] = p.color;
            objPriBuffer[x] = p.priority;
            objOpaque[x] = 1;
            objPalBuffer[x] = p.objPalette;
        }
    }
}

Ppu::Pixel Ppu::composeFromBuffers(int x, bool sub) const
{
    const uint8_t screenMask = sub ? subScreen : mainScreen;
    const bool forceBG3 = (debugFlags & DebugForceBG3) != 0;

    Pixel best;
    best.layer = 5;
    best.color = cgram[0];
    best.opaque = true;

    for (size_t bg = 0; bg < 4; ++bg) {
        if ((screenMask & (1u << bg)) == 0 || layerWindowMasked(static_cast<uint8_t>(bg), x, sub)) {
            continue;
        }
        if (!bgOpaque[bg][x]) {
            continue;
        }
        if (forceBG3 && bg == 2) {
            best.color = bgLineBuffer[bg][x];
            best.priority = bgPriBuffer[bg][x];
            best.layer = static_cast<uint8_t>(bg);
            break;
        }
        if (bgPriBuffer[bg][x] >= best.priority) {
            best.color = bgLineBuffer[bg][x];
            best.priority = bgPriBuffer[bg][x];
            best.layer = static_cast<uint8_t>(bg);
        }
    }

    if ((screenMask & 0x10) != 0 && !layerWindowMasked(4, x, sub)) {
        if (objOpaque[x] && objPriBuffer[x] >= best.priority) {
            best.color = objLineBuffer[x];
            best.priority = objPriBuffer[x];
            best.layer = 4;
            best.objPalette = objPalBuffer[x];
        }
    }

    return best;
}

void Ppu::tick(uint32_t dots)
{
    uint32_t total = hCounter + dots;
    while (total >= dotsPerScanline) {
        total -= dotsPerScanline;
        ++vCounter;
        if (vCounter >= ntscScanlines) {
            vCounter = 0;
            evenField = !evenField;
        }
    }
    hCounter = static_cast<uint16_t>(total);
}

void Ppu::beginFrame()
{
    // VRAM is only writable during blanking, so any CHR data uploaded since
    // the last frame invalidates the decoded-tile cache. The cache key has
    // no VRAM-content component, so stale entries would otherwise serve
    // pixels from overwritten tiles (e.g. SM's pause menu reuses the
    // gameplay CHR base).
    tileCacheKeys.fill(0);

    hCounter = 0;
    vCounter = 0;
    evenField = !evenField;
    spriteTimeOver = false;
    spriteRangeOver = false;
    inidispWriteCount = 0;
    lastInidispWritten = inidisp;

    const int vh = visibleHeight();
    for (int y = vh; y < screenHeight; ++y) {
        const size_t base = static_cast<size_t>(y * screenWidth);
        std::fill_n(&frame[base], screenWidth, 0xff000000);
        std::fill_n(&priorityFrame[base], screenWidth, 0);
    }
}

void Ppu::writeOamData(uint8_t value)
{
    const size_t address = internalOamAddress % oam.size();
    if (address < 0x200) {
        if (!oamHighByte) {
            oamLatch = value;
            oamHighByte = true;
            return;
        }
        oam[internalOamAddress++ % oam.size()] = oamLatch;
        oam[internalOamAddress++ % oam.size()] = value;
        oamHighByte = false;
        return;
    }

    oam[internalOamAddress++ % oam.size()] = value;
}

namespace {

uint16_t remapVramAddress(uint16_t address, uint8_t vmain)
{
    const uint8_t mapping = (vmain >> 2) & 0x03;
    switch (mapping) {
    case 0x01:
        return static_cast<uint16_t>((address & 0xff00) | ((address & 0x00e0) >> 5) | ((address & 0x001f) << 5));
    case 0x02:
    case 0x03:
        return static_cast<uint16_t>((address & 0xff00) | ((address & 0x00c0) >> 6) | ((address & 0x003f) << 6));
    default:
        return address;
    }
}

} // namespace

void Ppu::writeVramDataLow(uint8_t value)
{
    const uint16_t effective = remapVramAddress(vramAddress, vmain);
    uint16_t& word = vram[effective & 0x7fff];
    word = static_cast<uint16_t>((word & 0xff00) | value);
    incrementVramAddress(false);
}

void Ppu::writeVramDataHigh(uint8_t value)
{
    const uint16_t effective = remapVramAddress(vramAddress, vmain);
    uint16_t& word = vram[effective & 0x7fff];
    word = static_cast<uint16_t>((word & 0x00ff) | (value << 8));
    incrementVramAddress(true);
}

uint8_t Ppu::readVramDataLow()
{
    const uint8_t value = static_cast<uint8_t>(vramReadLatch);
    if ((vmain & 0x80) == 0) {
        // Increment-trigger read: the prefetch latch reloads from the
        // current (pre-increment) address, then the address advances.
        // Games rely on this for the "dummy read" convention.
        vramReadLatch = vram[remapVramAddress(vramAddress, vmain) & 0x7fff];
        incrementVramAddress(false);
    }
    return value;
}

uint8_t Ppu::readVramDataHigh()
{
    const uint8_t value = static_cast<uint8_t>(vramReadLatch >> 8);
    if ((vmain & 0x80) != 0) {
        vramReadLatch = vram[remapVramAddress(vramAddress, vmain) & 0x7fff];
        incrementVramAddress(true);
    }
    return value;
}

void Ppu::writeCgramData(uint8_t value)
{
    if (!cgramHighByte) {
        cgramLatch = value;
        cgramHighByte = true;
        return;
    }

    cgram[cgramAddress] = static_cast<uint16_t>(cgramLatch | ((value & 0x7f) << 8));
    ++cgramAddress;
    cgramHighByte = false;
}

uint8_t Ppu::readCgramData()
{
    const uint16_t color = cgram[cgramAddress];
    if (!cgramHighByte) {
        cgramHighByte = true;
        return static_cast<uint8_t>(color);
    }

    ++cgramAddress;
    cgramHighByte = false;
    return static_cast<uint8_t>((color >> 8) & 0x7f);
}

void Ppu::incrementVramAddress(bool highAccess)
{
    const bool incrementOnHigh = (vmain & 0x80) != 0;
    if (incrementOnHigh != highAccess) {
        return;
    }

    uint16_t increment = 1;
    switch (vmain & 0x03) {
    case 0x00:
        increment = 1;
        break;
    case 0x01:
        increment = 32;
        break;
    case 0x02:
    case 0x03:
        increment = 128;
        break;
    }
    vramAddress = static_cast<uint16_t>(vramAddress + increment);
}

// Layer ordering values per mode (higher = in front), matching hardware.
// All values within a mode are distinct so composition order can't matter.
uint8_t Ppu::bgPriorityValue(size_t bg, bool priorityBit) const
{
    const uint8_t mode = bgmode & 0x07;
    const bool bg3pri = (bgmode & 0x08) != 0;
    const int p = priorityBit ? 1 : 0;

    switch (mode) {
    case 0: {
        static constexpr uint8_t tbl[2][4] = {
            {8, 7, 2, 1},
            {11, 10, 5, 4},
        };
        return tbl[p][bg & 3];
    }
    case 1: {
        if (bg3pri) {
            // BGMODE bit 3 promotes only BG3's high-priority tiles to the
            // front; its low-priority tiles stay the backmost layer.
            static constexpr uint8_t tbl[2][4] = {
                {5, 4, 1, 0},
                {8, 7, 10, 0},
            };
            return tbl[p][bg & 3];
        }
        static constexpr uint8_t tbl[2][4] = {
            {6, 5, 1, 0},
            {9, 8, 3, 0},
        };
        return tbl[p][bg & 3];
    }
    case 6: {
        static constexpr uint8_t tbl[2] = {2, 5};
        return bg == 0 ? tbl[p] : 0;
    }
    case 7: {
        if (extBg()) {
            if (bg == 0) {
                return 3;
            }
            static constexpr uint8_t tbl[2] = {1, 5};
            return bg == 1 ? tbl[p] : 0;
        }
        return bg == 0 ? 2 : 0;
    }
    default: { // modes 2-5
        static constexpr uint8_t tbl[2][2] = {
            {3, 1},
            {7, 5},
        };
        return bg < 2 ? tbl[p][bg] : 0;
    }
    }
}

uint8_t Ppu::objPriorityValue(uint8_t objPri) const
{
    const uint8_t mode = bgmode & 0x07;
    const bool bg3pri = (bgmode & 0x08) != 0;

    switch (mode) {
    case 0: {
        static constexpr uint8_t tbl[4] = {3, 6, 9, 12};
        return tbl[objPri & 3];
    }
    case 1: {
        if (bg3pri) {
            static constexpr uint8_t tbl[4] = {2, 3, 6, 9};
            return tbl[objPri & 3];
        }
        static constexpr uint8_t tbl[4] = {2, 4, 7, 10};
        return tbl[objPri & 3];
    }
    case 6: {
        static constexpr uint8_t tbl[4] = {1, 3, 4, 6};
        return tbl[objPri & 3];
    }
    case 7: {
        if (extBg()) {
            static constexpr uint8_t tbl[4] = {2, 4, 6, 7};
            return tbl[objPri & 3];
        }
        static constexpr uint8_t tbl[4] = {1, 3, 4, 5};
        return tbl[objPri & 3];
    }
    default: { // modes 2-5
        static constexpr uint8_t tbl[4] = {2, 4, 6, 8};
        return tbl[objPri & 3];
    }
    }
}

void Ppu::writeBgScroll(size_t bg, bool vertical, uint8_t value)
{
    BgState& state = bgState[bg & 3];
    const size_t latchIdx = (bg & 3) * 2 + (vertical ? 1 : 0);
    if (vertical) {
        state.vscroll = static_cast<uint16_t>(((value & 0x03) << 8) | bgofsLatches[latchIdx]);
        bgofsLatches[latchIdx] = value;
        return;
    }

    state.hscroll = static_cast<uint16_t>(((value & 0x03) << 8) | bgofsLatches[latchIdx]);
    bgofsLatches[latchIdx] = value;
}

void Ppu::writeMode7Pair(uint16_t& target, uint8_t value)
{
    target = static_cast<uint16_t>((value << 8) | mode7Latch);
    mode7Latch = value;
}

Ppu::Pixel Ppu::composeScreenPixel(int x, int y, bool subScreenPixel) const
{
    Pixel best;
    best.layer = 5;
    best.color = cgram[0];
    best.opaque = true;

    for (size_t bg = 0; bg < 4; ++bg) {
        Pixel pixel = sampleBackground(bg, x, y, subScreenPixel);
        if (pixel.opaque && pixel.priority >= best.priority) {
            best = pixel;
        }
    }

    Pixel sprite = sampleSpriteLayer(x, y, subScreenPixel);
    if (sprite.opaque && sprite.priority >= best.priority) {
        best = sprite;
    }

    return best;
}

Ppu::Pixel Ppu::sampleBackground(size_t bg, int x, int y, bool subScreenPixel) const
{
    const uint8_t screenMask = subScreenPixel ? subScreen : mainScreen;
    if ((screenMask & (1u << bg)) == 0 || layerWindowMasked(static_cast<uint8_t>(bg), x, subScreenPixel)) {
        return {};
    }

    if ((bgmode & 0x07) == 7) {
        return sampleMode7(bg, x, y, subScreenPixel);
    }

    if (bg >= 4) {
        return {};
    }

    const uint8_t bpp = bppForBg(bg);
    if (bpp == 0) {
        return {};
    }

    const BgState& state = bgState[bg];
    int sampleX = x;
    int sampleY = y;
    const int mosaicSize = ((mosaic >> 4) & 0x0f) + 1;
    if ((mosaic & (1u << bg)) != 0 && mosaicSize > 1) {
        sampleX -= sampleX % mosaicSize;
        sampleY -= sampleY % mosaicSize;
    }

    const int worldX = (sampleX + state.hscroll) & 0x03ff;
    const int worldY = (sampleY + state.vscroll) & 0x03ff;
    const bool largeTiles = (bgmode & (0x10u << bg)) != 0;
    const int tileSize = largeTiles ? 16 : 8;
    const int tileX = worldX / tileSize;
    const int tileY = worldY / tileSize;
    const uint16_t entry = tilemapEntry(bg, tileX, tileY);
    const uint16_t baseTile = entry & 0x03ff;
    const uint8_t palette = static_cast<uint8_t>((entry >> 10) & 0x07);
    const bool priorityBit = (entry & 0x2000) != 0;
    const bool hflip = (entry & 0x4000) != 0;
    const bool vflip = (entry & 0x8000) != 0;

    int pixelX = worldX & (tileSize - 1);
    int pixelY = worldY & (tileSize - 1);
    uint16_t tile = baseTile;
    if (largeTiles) {
        if (pixelX >= 8) {
            tile += 1;
            pixelX -= 8;
        }
        if (pixelY >= 8) {
            tile += 16;
            pixelY -= 8;
        }
    }

    const uint8_t pixel = decodeTilePixel(tile, bpp, pixelX, pixelY, hflip, vflip, chrBase(bg));
    if (pixel == 0) {
        return {};
    }

    Pixel result;
    result.paletteIndex = cgramIndexForPixel(bg, bpp, palette, pixel);
    result.color = ((cgwsel & 0x01) != 0 && bpp == 8) ? directColor(palette, pixel) : cgram[result.paletteIndex];
    result.priority = bgPriorityValue(bg, priorityBit);
    result.layer = static_cast<uint8_t>(bg);
    result.opaque = true;
    return result;
}

Ppu::Pixel Ppu::sampleMode7(size_t bg, int x, int y, bool subScreenPixel) const
{
    if (bg > 1 || (bg == 1 && !extBg())) {
        return {};
    }
    if (layerWindowMasked(static_cast<uint8_t>(bg), x, subScreenPixel)) {
        return {};
    }

    if (std::getenv("SNESQUIK_M7FILL")) {
        Pixel marker;
        marker.color = (m7sel & 0x01) != 0 ? 0x7c1f : 0x03e0; // rear=magenta, fwd=green
        marker.priority = bgPriorityValue(bg, false);
        marker.layer = static_cast<uint8_t>(bg);
        marker.opaque = true;
        return marker;
    }

    int screenX = (m7sel & 0x01) != 0 ? screenWidth - 1 - x : x;
    const int renderY = y - 1;
    int screenY = (m7sel & 0x02) != 0 ? visibleHeight() - 1 - renderY : renderY;
    const int cx = static_cast<int16_t>(sign13(m7x));
    const int cy = static_cast<int16_t>(sign13(m7y));
    const int hofs = static_cast<int16_t>(sign13(m7hofs));
    const int vofs = static_cast<int16_t>(sign13(m7vofs));

    // Hardware formula (anomie's register doc): scroll offsets are applied
    // inside the matrix transform, clipped to signed 11 bits, and the
    // per-term products are truncated to multiples of 64.
    const auto clip10 = [](int v) {
        return (v & 0x2000) != 0 ? (v | ~0x3ff) : (v & 0x3ff);
    };
    const int a = static_cast<int16_t>(m7a);
    const int b = static_cast<int16_t>(m7b);
    const int c = static_cast<int16_t>(m7c);
    const int d = static_cast<int16_t>(m7d);
    const int ox = clip10(hofs - cx);
    const int oy = clip10(vofs - cy);
    const int originX = ((a * ox) & ~63) + ((b * oy) & ~63) + (cx << 8);
    const int originY = ((c * ox) & ~63) + ((d * oy) & ~63) + (cy << 8);
    int mapX = (originX + ((b * screenY) & ~63) + a * screenX) >> 8;
    int mapY = (originY + ((d * screenY) & ~63) + c * screenX) >> 8;

    const bool outside = mapX < 0 || mapX >= 1024 || mapY < 0 || mapY >= 1024;
    if (outside && (m7sel & 0x80) != 0) {
        if ((m7sel & 0x40) == 0) {
            return {};
        }
        mapX &= 0x07;
        mapY &= 0x07;
    } else {
        mapX &= 0x03ff;
        mapY &= 0x03ff;
    }

    const uint16_t tilemapAddress = static_cast<uint16_t>(((mapY >> 3) * 128 + (mapX >> 3)) & 0x7fff);
    const uint8_t tile = outside && (m7sel & 0x80) != 0 && (m7sel & 0x40) != 0 ? 0 : static_cast<uint8_t>(vram[tilemapAddress]);
    const uint16_t pixelAddress = static_cast<uint16_t>(tile * 64 + (mapY & 7) * 8 + (mapX & 7));
    uint8_t pixel = static_cast<uint8_t>(vram[pixelAddress & 0x7fff] >> 8);
    if (pixel == 0) {
        return {};
    }

    if (extBg()) {
        const bool highPriority = (pixel & 0x80) != 0;
        if ((bg == 0 && highPriority) || (bg == 1 && !highPriority)) {
            return {};
        }
        pixel &= 0x7f;
    }

    Pixel result;
    result.paletteIndex = pixel;
    result.color = (cgwsel & 0x01) != 0 ? directColor(0, pixel) : cgram[pixel];
    // EXTBG BG2 pixels reaching this point carry the high-priority bit.
    result.priority = bgPriorityValue(bg, bg == 1);
    result.layer = static_cast<uint8_t>(bg);
    result.opaque = true;
    return result;
}

Ppu::Pixel Ppu::sampleSpriteLayer(int x, int y, bool subScreenPixel) const
{
    const uint8_t screenMask = subScreenPixel ? subScreen : mainScreen;
    if ((screenMask & 0x10) == 0 || layerWindowMasked(4, x, subScreenPixel)) {
        return {};
    }

    Pixel best;
    int bestSprite = 128;
    int spritesOnLine = 0;
    int tilesOnLine = 0;

    for (size_t sprite = 0; sprite < 128; ++sprite) {
        int spriteY = 0;
        int width = 0;
        int height = 0;
        if (!spriteVisibleOnLine(sprite, y, spriteY, width, height)) {
            continue;
        }

        if (spritesOnLine >= 32) {
            spriteTimeOver = true;
            continue;
        }
        ++spritesOnLine;

        const int spriteTiles = std::max(1, width / 8);
        if (tilesOnLine + spriteTiles > 34) {
            spriteRangeOver = true;
            continue;
        }
        tilesOnLine += spriteTiles;

        int spriteX = 0;
        int fullY = 0;
        int fullWidth = 0;
        int fullHeight = 0;
        if (!spriteBounds(sprite, spriteX, fullY, fullWidth, fullHeight)) {
            continue;
        }
        if (x < spriteX || x >= spriteX + fullWidth) {
            continue;
        }

        Pixel pixel = sampleSpritePixel(sprite, x - spriteX, y - spriteY);
        if (!pixel.opaque) {
            continue;
        }
        if (!best.opaque || pixel.priority > best.priority || (pixel.priority == best.priority && static_cast<int>(sprite) < bestSprite)) {
            best = pixel;
            bestSprite = static_cast<int>(sprite);
        }
    }

    return best;
}

bool Ppu::spriteBounds(size_t sprite, int& x, int& y, int& width, int& height) const
{
    const uint8_t xLow = oam[sprite * 4];
    const uint8_t yRaw = oam[sprite * 4 + 1];
    const uint8_t high = oam[0x200 + sprite / 4];
    const uint8_t pair = static_cast<uint8_t>((high >> ((sprite & 3) * 2)) & 0x03);
    x = xLow;
    if ((pair & 0x01) != 0) {
        x -= 256;
    }
    objSize(objsel >> 5, (pair & 0x02) != 0, width, height);
    y = yRaw;
    return x < screenWidth && x + width > 0;
}

bool Ppu::spriteVisibleOnLine(size_t sprite, int line, int& y, int& width, int& height) const
{
    int x = 0;
    if (!spriteBounds(sprite, x, y, width, height)) {
        return false;
    }
    if (y + height > 256 && line < ((y + height) & 0xff)) {
        y -= 256;
    }
    return line >= y && line < y + height;
}

Ppu::Pixel Ppu::sampleSpritePixel(size_t sprite, int x, int y) const
{
    const size_t base = sprite * 4;
    const uint8_t tileLow = oam[base + 2];
    const uint8_t attrs = oam[base + 3];
    const uint8_t high = oam[0x200 + sprite / 4];
    const uint8_t pair = static_cast<uint8_t>((high >> ((sprite & 3) * 2)) & 0x03);
    const bool large = (pair & 0x02) != 0;
    int width = 8;
    int height = 8;
    objSize(objsel >> 5, large, width, height);

    if ((attrs & 0x40) != 0) {
        x = width - 1 - x;
    }
    if ((attrs & 0x80) != 0) {
        y = height - 1 - y;
    }

    const uint8_t tileHigh = attrs & 0x01;
    const uint8_t tileX = static_cast<uint8_t>(x / 8);
    const uint8_t tileY = static_cast<uint8_t>(y / 8);
    const uint16_t tile = static_cast<uint16_t>(tileLow + tileX + tileY * 16);
    const uint8_t pixel = decodeTilePixel(tile, 4, x & 7, y & 7, false, false, objChrBase(tileHigh));
    if (pixel == 0) {
        return {};
    }

    Pixel result;
    result.paletteIndex = static_cast<uint8_t>(128 + (((attrs >> 1) & 0x07) << 4) + pixel);
    result.color = cgram[result.paletteIndex];
    result.priority = objPriorityValue(static_cast<uint8_t>((attrs >> 4) & 0x03));
    result.layer = 4;
    result.objPalette = static_cast<uint8_t>((attrs >> 1) & 0x07);
    result.opaque = true;
    return result;
}

uint8_t Ppu::modeForBg(size_t) const
{
    return bgmode & 0x07;
}

uint8_t Ppu::bppForBg(size_t bg) const
{
    switch (modeForBg(bg)) {
    case 0:
        return bg < 4 ? 2 : 0;
    case 1:
        return bg < 2 ? 4 : (bg == 2 ? 2 : 0);
    case 2:
        return bg < 2 ? 4 : 0;
    case 3:
        return bg == 0 ? 8 : (bg == 1 ? 4 : 0);
    case 4:
        return bg == 0 ? 8 : (bg == 1 ? 2 : 0);
    case 5:
        return bg == 0 ? 4 : (bg == 1 ? 2 : 0);
    case 6:
        return bg == 0 ? 4 : 0;
    default:
        return 0;
    }
}

uint16_t Ppu::tilemapBase(size_t bg) const
{
    return static_cast<uint16_t>(((bgState[bg & 3].screen >> 2) & 0x3f) << 10);
}

uint16_t Ppu::chrBase(size_t bg) const
{
    const uint8_t nibble = bg < 2
        ? static_cast<uint8_t>((bg12nba >> (bg * 4)) & 0x0f)
        : static_cast<uint8_t>((bg34nba >> ((bg - 2) * 4)) & 0x0f);
    return static_cast<uint16_t>(nibble << 12);
}

uint16_t Ppu::offsetMapEntry(int tileCol, int row) const
{
    const BgState& bg3 = bgState[2];
    const uint16_t base = tilemapBase(2);

    const int mapY = row & 0x1f;
    const int mapX = tileCol & 0x1f;

    const uint16_t address = static_cast<uint16_t>(base + mapY * 32 + mapX);
    return vram[address & 0x7fff];
}

uint16_t Ppu::tilemapEntry(size_t bg, int tileX, int tileY) const
{
    const uint8_t screen = bgState[bg & 3].screen;
    const bool wide = (screen & 0x01) != 0;
    const bool tall = (screen & 0x02) != 0;
    const int mapX = wide ? tileX & 0x3f : tileX & 0x1f;
    const int mapY = tall ? tileY & 0x3f : tileY & 0x1f;
        const int quadrantX = mapX / 32;
        const int quadrantY = mapY / 32;
        const int localX = mapX & 0x1f;
        const int localY = mapY & 0x1f;
        int quadrant;
        if (wide && tall) {
            quadrant = quadrantX + quadrantY * 2;
        } else if (wide) {
            quadrant = quadrantX;
        } else if (tall) {
            // 32x64: the lower 32x32 screen immediately follows the upper
            // one at base + $400 (unlike 64x64 where rows are at +$800).
            quadrant = quadrantY;
        } else {
            quadrant = 0;
        }
    const uint16_t address = static_cast<uint16_t>(tilemapBase(bg) + quadrant * 0x400 + localY * 32 + localX);
    return vram[address & 0x7fff];
}

uint8_t Ppu::decodeTilePixel(uint16_t tileIndex, uint8_t bpp, int pixelX, int pixelY, bool hflip, bool vflip, uint16_t base) const
{
    if (hflip) {
        pixelX = 7 - pixelX;
    }
    if (vflip) {
        pixelY = 7 - pixelY;
    }

    const uint64_t key = (static_cast<uint64_t>(base) << 20)
                       | (static_cast<uint64_t>(tileIndex) << 8)
                       | (static_cast<uint64_t>(bpp) << 5)
                       | static_cast<uint64_t>(pixelY);
    const size_t idx = (static_cast<size_t>(base >> 12)
                      ^ static_cast<size_t>(tileIndex)
                      ^ static_cast<size_t>(bpp << 4)
                      ^ static_cast<size_t>(pixelY)) & (tileCacheSize - 1);

    if (tileCacheKeys[idx] == key) {
        return tileCachePixels[idx][pixelX];
    }

    const uint16_t wordsPerTile = static_cast<uint16_t>(bpp * 4);
    const uint16_t tileBase = static_cast<uint16_t>(base + tileIndex * wordsPerTile);

    uint8_t planeBytes[8];
    for (uint8_t plane = 0; plane < bpp; ++plane) {
        const uint16_t planeGroup = plane / 2;
        const uint16_t word = vram[(tileBase + planeGroup * 8 + pixelY) & 0x7fff];
        planeBytes[plane] = (plane & 1) == 0
            ? static_cast<uint8_t>(word)
            : static_cast<uint8_t>(word >> 8);
    }

    std::array<uint8_t, 8> row{};
    for (int px = 0; px < 8; ++px) {
        const int bit = 7 - px;
        uint8_t result = 0;
        for (uint8_t plane = 0; plane < bpp; ++plane) {
            result |= static_cast<uint8_t>(((planeBytes[plane] >> bit) & 1) << plane);
        }
        row[px] = result;
    }

    tileCacheKeys[idx] = key;
    tileCachePixels[idx] = row;
    return row[pixelX];
}

uint8_t Ppu::cgramIndexForPixel(size_t bg, uint8_t bpp, uint8_t palette, uint8_t pixel) const
{
    if (bpp == 8) {
        return pixel;
    }

    uint8_t base = static_cast<uint8_t>(palette << bpp);
    if ((bgmode & 0x07) == 0 && bpp == 2) {
        base = static_cast<uint8_t>(bg * 32 + palette * 4);
    }
    return static_cast<uint8_t>(base + pixel);
}

uint16_t Ppu::directColor(uint8_t palette, uint8_t pixel) const
{
    const uint16_t r = static_cast<uint16_t>(((pixel & 0x07) << 2) | ((palette & 0x01) << 1));
    const uint16_t g = static_cast<uint16_t>((((pixel >> 3) & 0x07) << 2) | (((palette >> 1) & 0x01) << 1));
    const uint16_t b = static_cast<uint16_t>((((pixel >> 6) & 0x03) << 3) | (((palette >> 2) & 0x01) << 2));
    return static_cast<uint16_t>(r | (g << 5) | (b << 10));
}

uint16_t Ppu::objChrBase(uint8_t tileHighBit) const
{
    const uint16_t base = static_cast<uint16_t>((objsel & 0x07) << 13);
    if (tileHighBit == 0) {
        return base;
    }
    const uint16_t nameSelect = static_cast<uint16_t>((((objsel >> 3) & 0x03) + 1) << 12);
    return static_cast<uint16_t>(base + nameSelect);
}

void Ppu::objSize(uint8_t sizeSelect, bool large, int& width, int& height) const
{
    static constexpr int sizes[8][4] = {
        {8, 8, 16, 16},
        {8, 8, 32, 32},
        {8, 8, 64, 64},
        {16, 16, 32, 32},
        {16, 16, 64, 64},
        {32, 32, 64, 64},
        {16, 32, 32, 64},
        {16, 32, 32, 32},
    };
    const int index = large ? 2 : 0;
    width = sizes[sizeSelect & 0x07][index];
    height = sizes[sizeSelect & 0x07][index + 1];
}

bool Ppu::layerWindowMasked(uint8_t layer, int x, bool subScreenPixel) const
{
    if ((debugFlags & DebugNoWindows) != 0) {
        return false;
    }

    const uint8_t windowMask = subScreenPixel ? tsw : tmw;
    if ((windowMask & (1u << layer)) == 0) {
        return false;
    }

    uint8_t selector = 0;
    uint8_t logic = 0;
    switch (layer) {
    case 0:
        selector = w12sel & 0x0f;
        logic = wbglog & 0x03;
        break;
    case 1:
        selector = static_cast<uint8_t>(w12sel >> 4);
        logic = static_cast<uint8_t>((wbglog >> 2) & 0x03);
        break;
    case 2:
        selector = w34sel & 0x0f;
        logic = static_cast<uint8_t>((wbglog >> 4) & 0x03);
        break;
    case 3:
        selector = static_cast<uint8_t>(w34sel >> 4);
        logic = static_cast<uint8_t>((wbglog >> 6) & 0x03);
        break;
    case 4:
        selector = wobjsel & 0x0f;
        logic = wobjlog & 0x03;
        break;
    default:
        return false;
    }

    return windowOutput(selector, logic, x);
}

bool Ppu::colorWindowActive(int x) const
{
    return windowOutput(static_cast<uint8_t>(wobjsel >> 4), static_cast<uint8_t>((wobjlog >> 2) & 0x03), x);
}

bool Ppu::colorWindowRegionActive(uint8_t region, int x) const
{
    const bool inside = colorWindowActive(x);
    switch (region & 0x03) {
    case 0:
        return false;
    case 1:
        return !inside;
    case 2:
        return inside;
    case 3:
        return true;
    }
    return false;
}

bool Ppu::windowOutput(uint8_t selector, uint8_t logic, int x) const
{
    const bool w1Enabled = (selector & 0x02) != 0;
    const bool w2Enabled = (selector & 0x08) != 0;
    const bool w1 = singleWindowOutput(w1Enabled, (selector & 0x01) != 0, wh0, wh1, x);
    const bool w2 = singleWindowOutput(w2Enabled, (selector & 0x04) != 0, wh2, wh3, x);

    if (!w1Enabled && !w2Enabled) {
        return false;
    }
    if (w1Enabled && !w2Enabled) {
        return w1;
    }
    if (!w1Enabled && w2Enabled) {
        return w2;
    }

    switch (logic & 0x03) {
    case 0:
        return w1 || w2;
    case 1:
        return w1 && w2;
    case 2:
        return w1 != w2;
    case 3:
        return w1 == w2;
    }
    return false;
}

bool Ppu::singleWindowOutput(bool enable, bool invert, uint8_t left, uint8_t right, int x) const
{
    if (!enable) {
        return false;
    }
    const bool inRange = left <= right && x >= left && x <= right;
    return invert ? !inRange : inRange;
}

uint16_t Ppu::applyColorMath(const Pixel& main, const Pixel& sub, int x) const
{
    if ((debugFlags & DebugNoColorMath) != 0) {
        return main.color;
    }
    if (!colorMathEnabledForLayer(main)) {
        return main.color;
    }

    if ((cgwsel & 0x02) != 0 && colorWindowRegionActive((cgwsel >> 4) & 0x03, x)) {
        return main.color;
    }

    const uint16_t addend = (cgwsel & 0x01) ? sub.color : fixedColor();
    return blendColors(main.color, addend);
}

bool Ppu::colorMathEnabledForLayer(const Pixel& pixel) const
{
    if (pixel.layer < 4) {
        return (cgadsub & (1u << pixel.layer)) != 0;
    }
    if (pixel.layer == 4) {
        return pixel.objPalette >= 4 && (cgadsub & 0x10) != 0;
    }
    return (cgadsub & 0x20) != 0;
}

uint16_t Ppu::blendColors(uint16_t main, uint16_t addend) const
{
    const bool subtract = (cgadsub & 0x80) != 0;
    const bool half = (cgadsub & 0x40) != 0;
    const int mr = main & 0x1f;
    const int mg = (main >> 5) & 0x1f;
    const int mb = (main >> 10) & 0x1f;
    const int ar = addend & 0x1f;
    const int ag = (addend >> 5) & 0x1f;
    const int ab = (addend >> 10) & 0x1f;
    int r = subtract ? mr - ar : mr + ar;
    int g = subtract ? mg - ag : mg + ag;
    int b = subtract ? mb - ab : mb + ab;
    if (half) {
        r >>= 1;
        g >>= 1;
        b >>= 1;
    }
    return static_cast<uint16_t>(clamp5(r) | (clamp5(g) << 5) | (clamp5(b) << 10));
}

uint16_t Ppu::fixedColor() const
{
    return coldata;
}

uint16_t Ppu::sign13(uint16_t value) const
{
    value &= 0x1fff;
    if ((value & 0x1000) != 0) {
        value |= 0xe000;
    }
    return value;
}

uint32_t Ppu::rgbaFromSnesColor(uint16_t color) const
{
    uint8_t r = expand5(static_cast<uint8_t>(color & 0x001f));
    uint8_t g = expand5(static_cast<uint8_t>((color >> 5) & 0x001f));
    uint8_t b = expand5(static_cast<uint8_t>((color >> 10) & 0x001f));
    const uint8_t level = brightness();
    r = static_cast<uint8_t>((r * level) / 15);
    g = static_cast<uint8_t>((g * level) / 15);
    b = static_cast<uint8_t>((b * level) / 15);
    return 0xff000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

} // namespace snesquik::ppu
