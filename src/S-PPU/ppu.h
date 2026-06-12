#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace snesquik::ppu {

class Ppu {
public:
    static constexpr int screenWidth = 256;
    static constexpr int screenHeight = 240;
    static constexpr size_t framebufferPixels = screenWidth * screenHeight;
    static constexpr int dotsPerScanline = 341;
    static constexpr int ntscScanlines = 262;
    static constexpr int hblankStart = 262;

    void reset();

    uint8_t readRegister(uint16_t address);
    void writeRegister(uint16_t address, uint8_t value);

    void tick(uint32_t dots);
    void beginFrame();
    void renderScanline(uint16_t y);
    void renderFrame();

    std::span<const uint32_t> framebuffer() const { return frame; }
    uint16_t cgramColor(size_t index) const { return cgram[index & 0xff]; }
    uint16_t vramWord(size_t address) const { return vram[address & 0x7fff]; }
    uint8_t oamByte(size_t address) const { return oam[address % oam.size()]; }
    uint16_t bgHorizontalScroll(size_t bg) const { return bgState[(bg - 1) & 3].hscroll; }
    uint16_t bgVerticalScroll(size_t bg) const { return bgState[(bg - 1) & 3].vscroll; }
    int visibleHeight() const { return overscan() ? 239 : 224; }
    uint16_t horizontalCounter() const { return hCounter; }
    uint16_t verticalCounter() const { return vCounter; }
    bool hblank() const { return hCounter >= hblankStart; }

    uint32_t getInidispWriteCount() const { return inidispWriteCount; }
    uint8_t getLastInidispWritten() const { return lastInidispWritten; }

    bool forceBlank() const { return (inidisp & 0x80) != 0; }
    uint8_t brightness() const { return inidisp & 0x0f; }
    bool overscan() const { return (setini & 0x04) != 0; }
    bool interlace() const { return (setini & 0x01) != 0; }
    bool extBg() const { return (setini & 0x40) != 0; }

    void setDebugFlags(uint8_t flags) { debugFlags = flags; }
    uint8_t debugFlagsMask() const { return debugFlags; }
    static constexpr uint8_t DebugNoColorMath = 0x01;
    static constexpr uint8_t DebugNoWindows = 0x02;
    static constexpr uint8_t DebugOnlyBG3 = 0x04;
    static constexpr uint8_t DebugForceBG3 = 0x08;
    uint8_t inidisp = 0x80;
        uint16_t vramAddress = 0;


private:
    struct BgState {
        uint8_t screen = 0;
        uint16_t hscroll = 0;
        uint16_t vscroll = 0;
    };

    struct Pixel {
        uint8_t paletteIndex = 0;
        uint8_t priority = 0;
        uint8_t layer = 5;
        uint8_t objPalette = 0;
        uint16_t color = 0;
        bool opaque = false;
    };

    struct WindowState {
        uint8_t selector = 0;
        uint8_t logic = 0;
        uint8_t mainEnableMask = 0;
        uint8_t subEnableMask = 0;
    };

    struct SpriteLineEntry {
        int spriteIndex;
        int x;
        int y;
        int width;
        int height;
        uint8_t priority;
    };

    void writeOamData(uint8_t value);
    void writeVramDataLow(uint8_t value);
    void writeVramDataHigh(uint8_t value);
    uint8_t readVramDataLow();
    uint8_t readVramDataHigh();
    void writeCgramData(uint8_t value);
    uint8_t readCgramData();
    void incrementVramAddress(bool highAccess);
    uint8_t bgPriorityValue(size_t bg, bool priorityBit) const;
    uint8_t objPriorityValue(uint8_t objPri) const;
    void writeBgScroll(size_t bg, bool vertical, uint8_t value);
    void writeMode7Pair(uint16_t& target, uint8_t value);
    Pixel composeScreenPixel(int x, int y, bool subScreenPixel) const;
    Pixel sampleBackground(size_t bg, int x, int y, bool subScreenPixel) const;
    Pixel sampleMode7(size_t bg, int x, int y, bool subScreenPixel) const;
    Pixel sampleSpriteLayer(int x, int y, bool subScreenPixel) const;
    Pixel sampleSpritePixel(size_t sprite, int x, int y) const;
    bool spriteBounds(size_t sprite, int& x, int& y, int& width, int& height) const;
    bool spriteVisibleOnLine(size_t sprite, int line, int& y, int& width, int& height) const;
    uint8_t modeForBg(size_t bg) const;
    uint8_t bppForBg(size_t bg) const;
    uint16_t tilemapBase(size_t bg) const;
    uint16_t chrBase(size_t bg) const;
    uint16_t tilemapEntry(size_t bg, int tileX, int tileY) const;
    uint16_t offsetMapEntry(int tileCol, int row) const;
    uint8_t decodeTilePixel(uint16_t tileIndex, uint8_t bpp, int pixelX, int pixelY, bool hflip, bool vflip, uint16_t chrBase) const;
    uint8_t cgramIndexForPixel(size_t bg, uint8_t bpp, uint8_t palette, uint8_t pixel) const;
    uint16_t directColor(uint8_t palette, uint8_t pixel) const;
    uint16_t objChrBase(uint8_t tileHighBit) const;
    void objSize(uint8_t sizeSelect, bool large, int& width, int& height) const;
    bool layerWindowMasked(uint8_t layer, int x, bool subScreenPixel) const;
    bool colorWindowActive(int x) const;
    bool colorWindowRegionActive(uint8_t region, int x) const;
    bool windowOutput(uint8_t selector, uint8_t logic, int x) const;
    bool singleWindowOutput(bool enable, bool invert, uint8_t left, uint8_t right, int x) const;
    uint16_t applyColorMath(const Pixel& main, const Pixel& sub, int x) const;
    bool colorMathEnabledForLayer(const Pixel& pixel) const;
    uint16_t blendColors(uint16_t main, uint16_t addend) const;
    uint16_t fixedColor() const;
    uint16_t sign13(uint16_t value) const;
    uint32_t rgbaFromSnesColor(uint16_t color) const;

    void buildVisibleSpriteList(int y);
    void renderBgLine(size_t bg, int y);
    void renderObjLine(int y);
    Pixel composeFromBuffers(int x, bool sub) const;

    static constexpr int tileCacheSize = 128;
    mutable std::array<uint64_t, tileCacheSize> tileCacheKeys{};
    mutable std::array<std::array<uint8_t, 8>, tileCacheSize> tileCachePixels{};

    std::array<std::array<uint16_t, screenWidth>, 4> bgLineBuffer{};
    std::array<std::array<uint8_t, screenWidth>, 4> bgPriBuffer{};
    std::array<std::array<uint8_t, screenWidth>, 4> bgOpaque{};

    std::array<uint16_t, screenWidth> objLineBuffer{};
    std::array<uint8_t, screenWidth> objPriBuffer{};
    std::array<uint8_t, screenWidth> objOpaque{};
    std::array<uint8_t, screenWidth> objPalBuffer{};

    std::array<SpriteLineEntry, 32> visibleSprites{};
    int visibleSpriteCount = 0;

    std::array<uint16_t, 32 * 1024> vram{};
    std::array<uint16_t, 256> cgram{};
    std::array<uint8_t, 544> oam{};
    std::array<uint32_t, framebufferPixels> frame{};
    std::array<uint8_t, framebufferPixels> priorityFrame{};

    std::array<uint8_t, 0x40> registers{};

    uint8_t objsel = 0;
    uint8_t bgmode = 0;
    uint8_t mosaic = 0;
    uint8_t bg12nba = 0;
    uint8_t bg34nba = 0;
    uint8_t m7sel = 0;
    uint8_t setini = 0;
    uint8_t mainScreen = 0;
    uint8_t subScreen = 0;
    uint8_t w12sel = 0;
    uint8_t w34sel = 0;
    uint8_t wobjsel = 0;
    uint8_t wh0 = 0;
    uint8_t wh1 = 0;
    uint8_t wh2 = 0;
    uint8_t wh3 = 0;
    uint8_t wbglog = 0;
    uint8_t wobjlog = 0;
    uint8_t tmw = 0;
    uint8_t tsw = 0;
    uint8_t cgwsel = 0;
    uint8_t cgadsub = 0;
    uint16_t coldata = 0;
    uint8_t vmain = 0;
    uint16_t vramReadLatch = 0;
    uint8_t cgramAddress = 0;
    uint8_t cgramLatch = 0;
    bool cgramHighByte = false;
    uint16_t oamAddress = 0;
    uint16_t internalOamAddress = 0;
    uint8_t oamLatch = 0;
    bool oamHighByte = false;
    std::array<uint8_t, 8> bgofsLatches{};
    uint8_t mode7Latch = 0;
    uint16_t m7hofs = 0;
    uint16_t m7vofs = 0;
    uint16_t m7a = 0;
    uint16_t m7b = 0;
    uint16_t m7c = 0;
    uint16_t m7d = 0;
    uint16_t m7x = 0;
    uint16_t m7y = 0;
    int32_t mode7Multiply = 0;
    uint16_t hCounter = 0;
    uint16_t vCounter = 0;
    uint16_t latchedHCounter = 0;
    uint16_t latchedVCounter = 0;
    bool hCounterHigh = false;
    bool vCounterHigh = false;
    bool counterLatch = false;
    bool evenField = true;
    mutable bool spriteTimeOver = false;
    mutable bool spriteRangeOver = false;
    uint8_t debugFlags = 0;
    std::array<BgState, 4> bgState{};

    uint32_t inidispWriteCount = 0;
    uint8_t lastInidispWritten = 0x80;
};

} // namespace snesquik::ppu
