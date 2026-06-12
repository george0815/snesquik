#include "BUS/bus.h"
#include "S-PPU/ppu.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace snesquik;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireEq(uint32_t actual, uint32_t expected, const std::string& message)
{
    if (actual != expected) {
        throw std::runtime_error(message + ": got " + std::to_string(actual) + ", expected " + std::to_string(expected));
    }
}

void run(const char* name, void (*test)())
{
    test();
    std::cout << "[pass] " << name << '\n';
}

void writeVramWord(ppu::Ppu& p, uint16_t address, uint16_t value)
{
    p.writeRegister(0x2115, 0x80);
    p.writeRegister(0x2116, static_cast<uint8_t>(address));
    p.writeRegister(0x2117, static_cast<uint8_t>(address >> 8));
    p.writeRegister(0x2118, static_cast<uint8_t>(value));
    p.writeRegister(0x2119, static_cast<uint8_t>(value >> 8));
}

void writeCgramColor(ppu::Ppu& p, uint8_t index, uint16_t value)
{
    p.writeRegister(0x2121, index);
    p.writeRegister(0x2122, static_cast<uint8_t>(value));
    p.writeRegister(0x2122, static_cast<uint8_t>(value >> 8));
}

void writeSolidTile(ppu::Ppu& p, uint16_t base, uint16_t tile, uint8_t bpp, uint8_t pixel)
{
    const uint16_t wordsPerTile = static_cast<uint16_t>(bpp * 4);
    const uint16_t tileBase = static_cast<uint16_t>(base + tile * wordsPerTile);
    for (uint8_t y = 0; y < 8; ++y) {
        uint16_t plane01 = 0;
        uint16_t plane23 = 0;
        uint16_t plane45 = 0;
        uint16_t plane67 = 0;
        if ((pixel & 0x01) != 0) {
            plane01 |= 0x00ff;
        }
        if ((pixel & 0x02) != 0) {
            plane01 |= 0xff00;
        }
        if ((pixel & 0x04) != 0) {
            plane23 |= 0x00ff;
        }
        if ((pixel & 0x08) != 0) {
            plane23 |= 0xff00;
        }
        if ((pixel & 0x10) != 0) {
            plane45 |= 0x00ff;
        }
        if ((pixel & 0x20) != 0) {
            plane45 |= 0xff00;
        }
        if ((pixel & 0x40) != 0) {
            plane67 |= 0x00ff;
        }
        if ((pixel & 0x80) != 0) {
            plane67 |= 0xff00;
        }

        writeVramWord(p, tileBase + y, plane01);
        if (bpp >= 4) {
            writeVramWord(p, tileBase + 8 + y, plane23);
        }
        if (bpp >= 8) {
            writeVramWord(p, tileBase + 16 + y, plane45);
            writeVramWord(p, tileBase + 24 + y, plane67);
        }
    }
}

void writeSinglePixelTile(ppu::Ppu& p, uint16_t base, uint16_t tile, uint8_t bpp, int pixelX, int pixelY, uint8_t pixel)
{
    const uint16_t wordsPerTile = static_cast<uint16_t>(bpp * 4);
    const uint16_t tileBase = static_cast<uint16_t>(base + tile * wordsPerTile);
    uint16_t plane01 = 0;
    uint16_t plane23 = 0;
    const int bit = 7 - pixelX;

    if ((pixel & 0x01) != 0) {
        plane01 |= static_cast<uint16_t>(1u << bit);
    }
    if ((pixel & 0x02) != 0) {
        plane01 |= static_cast<uint16_t>(1u << (bit + 8));
    }
    if ((pixel & 0x04) != 0) {
        plane23 |= static_cast<uint16_t>(1u << bit);
    }
    if ((pixel & 0x08) != 0) {
        plane23 |= static_cast<uint16_t>(1u << (bit + 8));
    }

    writeVramWord(p, tileBase + pixelY, plane01);
    if (bpp >= 4) {
        writeVramWord(p, tileBase + 8 + pixelY, plane23);
    }
}

void writeSprite(ppu::Ppu& p, uint8_t sprite, uint8_t x, uint8_t y, uint8_t tile, uint8_t attrs)
{
    const uint16_t address = static_cast<uint16_t>(sprite * 2);
    p.writeRegister(0x2102, static_cast<uint8_t>(address));
    p.writeRegister(0x2103, static_cast<uint8_t>(address >> 8));
    p.writeRegister(0x2104, x);
    p.writeRegister(0x2104, y);
    p.writeRegister(0x2104, tile);
    p.writeRegister(0x2104, attrs);
}

void hideSprites(ppu::Ppu& p)
{
    for (uint8_t sprite = 0; sprite < 128; ++sprite) {
        writeSprite(p, sprite, 0, 240, 0, 0);
    }
}

void writeMode7Pair(ppu::Ppu& p, uint16_t address, uint16_t value)
{
    p.writeRegister(address, static_cast<uint8_t>(value));
    p.writeRegister(address, static_cast<uint8_t>(value >> 8));
}

void testCgramWritesAndBackdropRendering()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2121, 0x00);
    p.writeRegister(0x2122, 0x1f);
    p.writeRegister(0x2122, 0x00);

    requireEq(p.cgramColor(0), 0x001f, "CGRAM stores low/high color writes");
    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xffff0000, "backdrop red renders to RGBA framebuffer");
}

void testForceBlankRendersBlack()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x8f);
    p.writeRegister(0x2121, 0x00);
    p.writeRegister(0x2122, 0x1f);
    p.writeRegister(0x2122, 0x00);
    p.renderFrame();

    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xff000000, "force blank renders black");
}

void testVramWritesIncrementOnHighByte()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2115, 0x80);
    p.writeRegister(0x2116, 0x34);
    p.writeRegister(0x2117, 0x12);
    p.writeRegister(0x2118, 0xaa);
    p.writeRegister(0x2119, 0xbb);
    p.writeRegister(0x2118, 0xcc);
    p.writeRegister(0x2119, 0xdd);

    requireEq(p.vramWord(0x1234), 0xbbaa, "first VRAM word");
    requireEq(p.vramWord(0x1235), 0xddcc, "second VRAM word after increment");
}

void testPpuMmioThroughBus()
{
    ppu::Ppu p;
    p.reset();
    bus::SnesBus snesBus;
    snesBus.attachPpu(&p);

    snesBus.write8(0x002100, 0x0f);
    snesBus.write8(0x002121, 0x00);
    snesBus.write8(0x002122, 0x00);
    snesBus.write8(0x002122, 0x7c);
    p.renderFrame();

    requireEq(p.cgramColor(0), 0x7c00, "bus routes CGRAM writes to PPU");
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xff0000ff, "bus-written blue backdrop renders");
}

void testOamWrites()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2102, 0x00);
    p.writeRegister(0x2103, 0x00);
    p.writeRegister(0x2104, 0x12);
    p.writeRegister(0x2104, 0x34);

    requireEq(p.oamByte(0), 0x12, "OAM low byte write");
    requireEq(p.oamByte(1), 0x34, "OAM high byte write");
}

void testMode1Bg1TileRendering()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x01);
    p.writeRegister(0x2107, 0x00);
    p.writeRegister(0x210b, 0x01);
    p.writeRegister(0x212c, 0x01);
    writeCgramColor(p, 1, 0x03e0);
    writeVramWord(p, 0x0000, 0x0000);
    writeSolidTile(p, 0x1000, 0, 4, 1);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xff00ff00, "Mode 1 BG1 4bpp tile renders through palette");
}

void testMode0PaletteWindowsPerBackground()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x00);
    p.writeRegister(0x2109, 0x00);
    p.writeRegister(0x210c, 0x01);
    p.writeRegister(0x212c, 0x04);
    writeCgramColor(p, 65, 0x001f);
    writeVramWord(p, 0x0000, 0x0000);
    writeSolidTile(p, 0x1000, 0, 2, 1);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xffff0000, "Mode 0 BG3 uses its own 32-color CGRAM window");
}

void testBgScrollSelectsDifferentTile()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x01);
    p.writeRegister(0x2107, 0x00);
    p.writeRegister(0x210b, 0x01);
    p.writeRegister(0x212c, 0x01);
    p.writeRegister(0x210d, 0x08);
    p.writeRegister(0x210d, 0x00);
    writeCgramColor(p, 1, 0x03e0);
    writeCgramColor(p, 2, 0x001f);
    writeVramWord(p, 0x0000, 0x0000);
    writeVramWord(p, 0x0001, 0x0001);
    writeSolidTile(p, 0x1000, 0, 4, 1);
    writeSolidTile(p, 0x1000, 1, 4, 2);

    p.renderFrame();
    requireEq(p.bgHorizontalScroll(1), 8, "BG1 scroll latch builds 10-bit scroll value");
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xffff0000, "horizontal scroll samples second tile");
}

void testMode1Bg3PriorityLowTilesStayBack()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x09); // mode 1 + BG3 priority
    p.writeRegister(0x2108, 0x04); // BG2 tilemap $0400
    p.writeRegister(0x2109, 0x08); // BG3 tilemap $0800
    p.writeRegister(0x210b, 0x10); // BG2 CHR $1000 (high nibble)
    p.writeRegister(0x210c, 0x02); // BG3 CHR $2000
    p.writeRegister(0x212c, 0x06); // BG2 + BG3 on main screen
    writeCgramColor(p, 1, 0x03e0); // green (BG2 pixel 1, palette 0)
    writeCgramColor(p, 5, 0x001f); // red (BG3 pixel 1, palette 1)
    // BG2: low-priority opaque tile; BG3: low-priority opaque tile.
    // With BGMODE bit 3 set, only BG3's HIGH-priority tiles move to the
    // front; its low-priority tiles are the backmost layer, so BG2 wins.
    writeVramWord(p, 0x0400, 0x0000);
    writeVramWord(p, 0x0800, 0x0400); // palette 1, priority clear
    writeSolidTile(p, 0x1000, 0, 4, 1);
    writeSolidTile(p, 0x2000, 0, 2, 1);

    p.renderFrame();
    requireEq(p.framebuffer()[0], 0xff00ff00, "BG2 low priority renders above BG3 low priority in BG3-priority mode");
}

void testTileCacheInvalidatedOnNewFrame()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x01);
    p.writeRegister(0x2107, 0x00);
    p.writeRegister(0x210b, 0x01);
    p.writeRegister(0x212c, 0x01);
    writeCgramColor(p, 1, 0x03e0);
    writeCgramColor(p, 2, 0x001f);
    writeVramWord(p, 0x0000, 0x0000);
    writeSolidTile(p, 0x1000, 0, 4, 1);
    p.renderFrame();
    requireEq(p.framebuffer()[0], 0xff00ff00, "first render caches tile pixels");

    // Overwrite the same tile's CHR (as games do when loading new graphics
    // over the same base, e.g. SM's pause menu) and render again.
    writeSolidTile(p, 0x1000, 0, 4, 2);
    p.renderFrame();
    requireEq(p.framebuffer()[0], 0xffff0000, "rewritten CHR replaces cached tile pixels");
}

void testTallTilemapLowerScreenAddress()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x01);
    p.writeRegister(0x2107, 0x02); // BG1 tilemap at $0000, 32x64 (tall)
    p.writeRegister(0x210b, 0x01);
    p.writeRegister(0x212c, 0x01);
    // Scroll down 256 px so the visible window is in the lower 32x32 screen,
    // which lives at base + $400 words.
    p.writeRegister(0x210e, 0x00);
    p.writeRegister(0x210e, 0x01);
    writeCgramColor(p, 1, 0x03e0);
    writeCgramColor(p, 2, 0x001f);
    writeVramWord(p, 0x0000, 0x0000);
    writeVramWord(p, 0x0400, 0x0001); // first entry of the lower screen
    writeSolidTile(p, 0x1000, 0, 4, 1);
    writeSolidTile(p, 0x1000, 1, 4, 2);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xffff0000, "tall tilemap lower screen reads base + $400");
}

void testBgPriorityChoosesFrontPixel()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x01);
    p.writeRegister(0x2107, 0x00);
    p.writeRegister(0x2108, 0x04);
    p.writeRegister(0x210b, 0x21);
    p.writeRegister(0x212c, 0x03);
    writeCgramColor(p, 1, 0x03e0);
    writeCgramColor(p, 2, 0x001f);
    writeVramWord(p, 0x0000, 0x0000);
    writeVramWord(p, 0x0400, 0x2001);
    writeSolidTile(p, 0x1000, 0, 4, 1);
    writeSolidTile(p, 0x2000, 1, 4, 2);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xffff0000, "priority tilemap bit places BG2 over BG1");
}

void testSpriteRenderingUsesObjPalette()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2101, 0x00);
    p.writeRegister(0x212c, 0x10);
    hideSprites(p);
    writeCgramColor(p, 129, 0x03e0);
    writeSolidTile(p, 0x0000, 0, 4, 1);
    writeSprite(p, 0, 0, 1, 0, 0x30);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xff00ff00, "OBJ tile renders through sprite CGRAM window");
}

void testSpriteHorizontalFlip()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2101, 0x00);
    p.writeRegister(0x212c, 0x10);
    hideSprites(p);
    writeCgramColor(p, 129, 0x001f);
    writeSinglePixelTile(p, 0x0000, 0, 4, 0, 0, 1);
    writeSprite(p, 0, 0, 1, 0, 0x70);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xff000000, "hflipped OBJ leaves original left pixel transparent");
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth + 7], 0xffff0000, "hflipped OBJ moves left tile pixel to right edge");
}

void testMode7RendersChunkyTileData()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x07);
    p.writeRegister(0x212c, 0x01);
    writeMode7Pair(p, 0x211b, 0x0100);
    writeMode7Pair(p, 0x211e, 0x0100);
    writeCgramColor(p, 2, 0x001f);
    writeVramWord(p, 0x0000, 0x0001);
    writeVramWord(p, 0x0040, 0x0200);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xffff0000, "Mode 7 samples tilemap low bytes and chunky tile high bytes");
}

void testWindowMasksMainBackground()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x01);
    p.writeRegister(0x2107, 0x00);
    p.writeRegister(0x210b, 0x01);
    p.writeRegister(0x212c, 0x01);
    p.writeRegister(0x2123, 0x02);
    p.writeRegister(0x2126, 0x00);
    p.writeRegister(0x2127, 0x07);
    p.writeRegister(0x212e, 0x01);
    writeCgramColor(p, 1, 0x03e0);
    writeVramWord(p, 0x0000, 0x0000);
    writeSolidTile(p, 0x1000, 0, 4, 1);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xff000000, "window masks BG1 inside WH0-WH1");
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth + 8], 0xff00ff00, "BG1 renders outside window mask");
}

void testMosaicSamplesTopLeftPixelOfBlock()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x01);
    p.writeRegister(0x2106, 0x11);
    p.writeRegister(0x2107, 0x00);
    p.writeRegister(0x210b, 0x01);
    p.writeRegister(0x212c, 0x01);
    writeCgramColor(p, 1, 0x001f);
    writeVramWord(p, 0x0000, 0x0000);
    writeSinglePixelTile(p, 0x1000, 0, 4, 0, 0, 1);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth + 1], 0xffff0000, "mosaic reuses the first pixel in the block");
}

void testColorMathAddsFixedColorBeforeBrightness()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x01);
    p.writeRegister(0x2107, 0x00);
    p.writeRegister(0x210b, 0x01);
    p.writeRegister(0x212c, 0x01);
    p.writeRegister(0x2131, 0x01);
    p.writeRegister(0x2132, 0x5f);
    writeCgramColor(p, 1, 0x001f);
    writeVramWord(p, 0x0000, 0x0000);
    writeSolidTile(p, 0x1000, 0, 4, 1);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xffffff00, "fixed color math adds in SNES 5-bit color space");
}

void testBrightnessScalesFinalColor()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x07);
    writeCgramColor(p, 0, 0x001f);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xff770000, "brightness scales final DAC output");
}

void testOverscanAndInterlaceState()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2133, 0x05);

    require(p.overscan(), "SETINI enables overscan");
    require(p.interlace(), "SETINI enables interlace");
    requireEq(static_cast<uint32_t>(p.visibleHeight()), 239, "overscan visible height");
}

void testHvCountersLatchAndToggle()
{
    ppu::Ppu p;
    p.reset();
    p.tick(400);
    p.readRegister(0x2137);

    requireEq(p.readRegister(0x213c), 59, "latched H counter low byte");
    requireEq(p.readRegister(0x213c), 0, "latched H counter high byte");
    requireEq(p.readRegister(0x213d), 1, "latched V counter low byte");
    requireEq(p.readRegister(0x213d), 0, "latched V counter high byte");
}

void testDmaWritesToPpuRegisters()
{
    ppu::Ppu p;
    p.reset();
    bus::SnesBus snesBus;
    snesBus.attachPpu(&p);
    snesBus.writeWram(0, 0x1f);
    snesBus.writeWram(1, 0x00);
    snesBus.write8(0x002121, 0x00);
    snesBus.write8(0x004300, 0x00);
    snesBus.write8(0x004301, 0x22);
    snesBus.write8(0x004302, 0x00);
    snesBus.write8(0x004303, 0x00);
    snesBus.write8(0x004304, 0x7e);
    snesBus.write8(0x004305, 0x02);
    snesBus.write8(0x004306, 0x00);
    snesBus.write8(0x00420b, 0x01);

    requireEq(p.cgramColor(0), 0x001f, "DMA channel writes through B-bus PPU register");
    requireEq(snesBus.readMmio(0x4305), 0, "DMA size low clears after transfer");
}

void testSpriteScanlineLimitsSetStatusFlags()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2101, 0x60);
    p.writeRegister(0x212c, 0x10);
    hideSprites(p);
    writeCgramColor(p, 129, 0x001f);
    writeSolidTile(p, 0x0000, 0, 4, 1);
    for (uint8_t sprite = 0; sprite < 18; ++sprite) {
        writeSprite(p, sprite, static_cast<uint8_t>(sprite * 8), 1, 0, 0x30);
    }

    p.renderFrame();
    require((p.readRegister(0x213e) & 0x40) != 0, "sprite tile overflow sets range-over flag");
}

void testMode2OffsetPerTileHorizontal()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x02);
    p.writeRegister(0x2107, 0x00);
    p.writeRegister(0x210b, 0x01);
    p.writeRegister(0x212c, 0x01);

    writeCgramColor(p, 1, 0x03e0);
    writeCgramColor(p, 2, 0x001f);
    writeVramWord(p, 0x0000, 0x0000);
    writeVramWord(p, 0x0001, 0x0001);
    writeVramWord(p, 0x0002, 0x0000);
    writeSolidTile(p, 0x1000, 0, 4, 1);
    writeSolidTile(p, 0x1000, 1, 4, 2);

    const uint16_t bg3Base = 0x0800;
    p.writeRegister(0x2109, static_cast<uint8_t>((bg3Base >> 8) & 0x3f));
    writeVramWord(p, bg3Base, 0xa008);
    writeVramWord(p, bg3Base + 32, 0x2000);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth + 8], 0xff00ff00, "Mode 2 OPT shifts tile column 1 horizontally");
}

void testMode2OffsetPerTileLeftmostColumnUnchanged()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    p.writeRegister(0x2105, 0x02);
    p.writeRegister(0x2107, 0x00);
    p.writeRegister(0x210b, 0x01);
    p.writeRegister(0x212c, 0x01);

    writeCgramColor(p, 1, 0x03e0);
    writeCgramColor(p, 2, 0x001f);
    writeVramWord(p, 0x0000, 0x0000);
    writeVramWord(p, 0x0001, 0x0001);
    writeSolidTile(p, 0x1000, 0, 4, 1);
    writeSolidTile(p, 0x1000, 1, 4, 2);

    const uint16_t bg3Base = 0x0800;
    p.writeRegister(0x2109, static_cast<uint8_t>((bg3Base >> 8) & 0x3f));
    writeVramWord(p, bg3Base, 0xa008);

    p.renderFrame();
    requireEq(p.framebuffer()[ppu::Ppu::screenWidth], 0xff00ff00, "Mode 2 OPT leftmost column unaffected by offset");
}

void testHBlankFlagTiming()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);

    require(!p.hblank(), "HBlank clear at dot 0");

    p.tick(262);
    require(p.hblank(), "HBlank set at dot 262");

    p.tick(ppu::Ppu::dotsPerScanline - 262);
    require(!p.hblank(), "HBlank clear after scanline wraps");
}

void testHBlankThroughBus()
{
    ppu::Ppu p;
    p.reset();
    p.writeRegister(0x2100, 0x0f);
    bus::SnesBus snesBus;
    snesBus.attachPpu(&p);

    snesBus.write8(0x004200, 0x00);
    uint8_t val = snesBus.read8(0x004212);
    require((val & 0x40) == 0, "$4212 HBlank clear at start");

    p.tick(262);
    val = snesBus.read8(0x004212);
    require((val & 0x40) != 0, "$4212 HBlank set after dot 262");
}

void testIrqHvCounterMatch()
{
    ppu::Ppu p;
    p.reset();
    bus::SnesBus snesBus;
    snesBus.attachPpu(&p);

    snesBus.write8(0x004207, 0x06);
    snesBus.write8(0x004208, 0x00);
    snesBus.write8(0x004209, 0x01);
    snesBus.write8(0x00420a, 0x00);
    snesBus.write8(0x004200, 0x30);

    require(snesBus.irqEnabled(), "IRQ enabled after $4200 write");
    requireEq(static_cast<uint32_t>(snesBus.irqHTime()), 6u, "HTIME latch");
    requireEq(static_cast<uint32_t>(snesBus.irqVTime()), 1u, "VTIME latch");

    p.tick(6);
    snesBus.checkIrq(p.horizontalCounter(), p.verticalCounter());
    require(!snesBus.irqFlag(), "IRQ not yet set before V match");

    p.tick(ppu::Ppu::dotsPerScanline);
    snesBus.checkIrq(p.horizontalCounter(), p.verticalCounter());
    require(snesBus.irqFlag(), "IRQ flag set when H/V match");
}

void testIrqFlagClearOnRead()
{
    bus::SnesBus snesBus;
    snesBus.setIrq();
    require(snesBus.irqFlag(), "IRQ flag set");

    snesBus.read8(0x004211);
    require(!snesBus.irqFlag(), "IRQ flag cleared after $4211 read");
}

} // namespace

int main()
{
    try {
        run("CGRAM writes and backdrop rendering", testCgramWritesAndBackdropRendering);
        run("force blank rendering", testForceBlankRendersBlack);
        run("VRAM writes and increment", testVramWritesIncrementOnHighByte);
        run("PPU MMIO through bus", testPpuMmioThroughBus);
        run("OAM writes", testOamWrites);
        run("Mode 1 BG1 tile rendering", testMode1Bg1TileRendering);
        run("Mode 0 per-BG palette windows", testMode0PaletteWindowsPerBackground);
        run("BG scroll selects tile", testBgScrollSelectsDifferentTile);
        run("mode 1 BG3-priority low tiles stay back", testMode1Bg3PriorityLowTilesStayBack);
        run("tile cache invalidated on new frame", testTileCacheInvalidatedOnNewFrame);
        run("tall tilemap lower screen address", testTallTilemapLowerScreenAddress);
        run("BG priority composition", testBgPriorityChoosesFrontPixel);
        run("OBJ palette rendering", testSpriteRenderingUsesObjPalette);
        run("OBJ horizontal flip", testSpriteHorizontalFlip);
        run("Mode 7 rendering", testMode7RendersChunkyTileData);
        run("window masking", testWindowMasksMainBackground);
        run("mosaic sampling", testMosaicSamplesTopLeftPixelOfBlock);
        run("fixed color math", testColorMathAddsFixedColorBeforeBrightness);
        run("brightness scaling", testBrightnessScalesFinalColor);
        run("overscan/interlace state", testOverscanAndInterlaceState);
        run("H/V counter latch", testHvCountersLatchAndToggle);
        run("DMA to PPU", testDmaWritesToPpuRegisters);
        run("sprite scanline limits", testSpriteScanlineLimitsSetStatusFlags);
        run("Mode 2 OPT horizontal", testMode2OffsetPerTileHorizontal);
        run("Mode 2 OPT leftmost column", testMode2OffsetPerTileLeftmostColumnUnchanged);
        run("HBlank flag timing", testHBlankFlagTiming);
        run("HBlank through bus", testHBlankThroughBus);
        run("IRQ H/V counter match", testIrqHvCounterMatch);
        run("IRQ flag clear on read", testIrqFlagClearOnRead);
    } catch (const std::exception& error) {
        std::cerr << "[fail] " << error.what() << '\n';
        return 1;
    }
}
