#include "BUS/bus.h"
#include "CART/rom_parser.h"
#include "CPU_R5A22/core.h"
#include "DEBUG/probe.h"
#include "MAIN/sdl_gl_renderer.h"
#include "S-PPU/ppu.h"
#include "STATE/savestate.h"

#include <SDL.h>
#include <SDL_keycode.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <span>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr uint32_t frameDots = snesquik::ppu::Ppu::dotsPerScanline * snesquik::ppu::Ppu::ntscScanlines;

struct InputContext {
    snesquik::bus::SnesBus* bus = nullptr;
    snesquik::ppu::Ppu* ppu = nullptr;
    uint16_t state = 0;
    bool logActive = false;
    bool f5WasDown = false;
    bool saveStateRequested = false;
    bool loadStateRequested = false;
};


uint32_t frameNumber = 0;
std::ofstream logFile;

void logEvent(const std::string& text)
{
    if (!logFile.is_open()) {
        return;
    }

    logFile << text << '\n';
}

class MainTraceListener final : public snesquik::bus::TraceListener {
public:
    void mmioRead(uint16_t, uint8_t) override {}

    void mmioWrite(uint16_t address, uint8_t value) override {
        if (!logFile.is_open()) return;

        if ((address >= 0x210d && address <= 0x2112) ||
            (address >= 0x2126 && address <= 0x2131) ||
            address == 0x2100 ||
            address == 0x2105 ||
            address == 0x212c ||
            address == 0x212d) {
            logFile << "[MMIO]"
                    << " frame=" << frameNumber
                    << " reg=$" << std::hex << address
                    << " value=$" << int(value)
                    << std::dec << '\n';
        }
    }

    void dmaTransfer(
    uint8_t channel,
    uint32_t sourceAddress,
    uint8_t bbad,
    uint8_t value,
    uint16_t vramAddress) override
{
    if (!logFile.is_open()) {
        return;
    }

    logFile << "[DMA-XFER]"
            << " frame=" << frameNumber
            << " ch=" << std::dec << int(channel)
            << " src=$" << std::hex << sourceAddress
            << " bbad=$" << int(bbad)
            << " value=$" << int(value)
            << " vmadd=$" << vramAddress
            << std::dec
            << '\n';
}

    void dmaStart(uint8_t channel, uint16_t size) override {
        if (!logFile.is_open()) return;
        logFile << "[DMA]"
                << " frame=" << frameNumber
                << " ch=" << int(channel)
                << " size=" << size
                << '\n';
    }

    void vblank(bool active) override {
        if (!logFile.is_open()) return;
        logFile << "[VBLANK]"
                << " frame=" << frameNumber
                << " active=" << active
                << '\n';
    }

    void hdmaWrite(uint8_t channel, uint16_t address, uint8_t value) override {
        if (!logFile.is_open()) return;
        logFile << "[HDMA]"
                << " frame=" << frameNumber
                << " ch=" << int(channel)
                << " reg=$" << std::hex << address
                << " value=$" << int(value)
                << std::dec << '\n';
    }
};

std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool isOption(const char* text, const char* option)
{
    return std::string(text) == option;
}

// Locate the DSP-1 program+data ROM dump. Honors $SNESQUIK_DSP1_ROM, then
// falls back to the common dump filenames under tests/roms/. Returns the
// first path that exists, or "" if none do.
std::string resolveDspRomPath()
{
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("SNESQUIK_DSP1_ROM")) {
        candidates.emplace_back(env);
    }
    candidates.emplace_back("tests/roms/dsp1b.rom");
    candidates.emplace_back("tests/roms/dsp1.rom");
    candidates.emplace_back("tests/roms/dsp1a.rom");
    for (const auto& path : candidates) {
        std::ifstream file(path, std::ios::binary);
        if (file.good()) {
            return path;
        }
    }
    return std::string();
}

uint16_t joypadMaskForKey(int key)
{
    using namespace snesquik::bus;
    switch (key) {
    case SDLK_z:
        return JoypadB;
    case SDLK_x:
        return JoypadA;
    case SDLK_a:
        return JoypadY;
    case SDLK_s:
        return JoypadX;
    case SDLK_q:
        return JoypadL;
    case SDLK_w:
        return JoypadR;
    case SDLK_RETURN:
        return JoypadStart;
    case SDLK_BACKSPACE:
        return JoypadSelect;
    case SDLK_UP:
        return JoypadUp;
    case SDLK_DOWN:
        return JoypadDown;
    case SDLK_LEFT:
        return JoypadLeft;
    case SDLK_RIGHT:
        return JoypadRight;
    default:
        return 0;
    }
}

uint16_t joypadMaskForName(std::string name)
{
    using namespace snesquik::bus;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (name == "b") {
        return JoypadB;
    }
    if (name == "y") {
        return JoypadY;
    }
    if (name == "select") {
        return JoypadSelect;
    }
    if (name == "start") {
        return JoypadStart;
    }
    if (name == "up") {
        return JoypadUp;
    }
    if (name == "down") {
        return JoypadDown;
    }
    if (name == "left") {
        return JoypadLeft;
    }
    if (name == "right") {
        return JoypadRight;
    }
    if (name == "a") {
        return JoypadA;
    }
    if (name == "x") {
        return JoypadX;
    }
    if (name == "l") {
        return JoypadL;
    }
    if (name == "r") {
        return JoypadR;
    }
    return 0;
}

void logFrameState(snesquik::ppu::Ppu& ppu, snesquik::bus::SnesBus& bus, const snesquik::cpu_r5a22::CPU& cpu, uint32_t frame)
{
    if (!logFile.is_open()) {
        return;
    }

    logFile << "=== Frame " << frame << " ===\n";

    // CPU state
    const auto& cpuReg = cpu.registers();
    logFile << "  CPU PB:PC=$" << std::hex << static_cast<int>(cpuReg.pb) << ":" << static_cast<int>(cpuReg.pc)
            << " A=$" << static_cast<int>(cpuReg.a)
            << " X=$" << static_cast<int>(cpuReg.x)
            << " Y=$" << static_cast<int>(cpuReg.y)
            << " S=$" << static_cast<int>(cpuReg.s)
            << " D=$" << static_cast<int>(cpuReg.d)
            << " DB=$" << static_cast<int>(cpuReg.db)
            << " P=$" << static_cast<int>(cpuReg.p)
            << " E=" << (cpuReg.emulation ? 1 : 0) << std::dec << '\n';

    // PPU registers
    logFile << "  INIDISP=$" << std::hex << static_cast<int>(ppu.readRegister(0x2100))
            << " INIDISP_WRITES=" << std::dec << ppu.getInidispWriteCount()
            << " LAST_INIDISP_WRITTEN=$" << std::hex << static_cast<int>(ppu.getLastInidispWritten())
            << " BGMODE=$" << static_cast<int>(ppu.readRegister(0x2105))
            << " MOSAIC=$" << static_cast<int>(ppu.readRegister(0x2106))
            << " TM=$" << static_cast<int>(ppu.readRegister(0x212c))
            << " TS=$" << static_cast<int>(ppu.readRegister(0x212d))
            << " TMW=$" << static_cast<int>(ppu.readRegister(0x212e))
            << " TSW=$" << static_cast<int>(ppu.readRegister(0x212f))
            << " CGSEL=$" << static_cast<int>(ppu.readRegister(0x2130))
            << " CGADSUB=$" << static_cast<int>(ppu.readRegister(0x2131))
            << " SETINI=$" << static_cast<int>(ppu.readRegister(0x2133)) << std::dec << '\n';

    // BG scroll registers
    logFile << "  BG1HOFS=$" << std::hex << static_cast<int>(ppu.readRegister(0x210d))
            << " BG1VOFS=$" << static_cast<int>(ppu.readRegister(0x210e))
            << " BG2HOFS=$" << static_cast<int>(ppu.readRegister(0x210f))
            << " BG2VOFS=$" << static_cast<int>(ppu.readRegister(0x2110))
            << " BG3HOFS=$" << static_cast<int>(ppu.readRegister(0x2111))
            << " BG3VOFS=$" << static_cast<int>(ppu.readRegister(0x2112)) << std::dec << '\n';

    // BG tilemap bases
    logFile << "  BG1SC=$" << std::hex << static_cast<int>(ppu.readRegister(0x2107))
            << " BG2SC=$" << static_cast<int>(ppu.readRegister(0x2108))
            << " BG3SC=$" << static_cast<int>(ppu.readRegister(0x2109))
            << " BG4SC=$" << static_cast<int>(ppu.readRegister(0x210a))
            << " BG12NBA=$" << static_cast<int>(ppu.readRegister(0x210b))
            << " BG34NBA=$" << static_cast<int>(ppu.readRegister(0x210c))
            << " VMADD=$" << static_cast<int>(ppu.readRegister(0x2116) | (ppu.readRegister(0x2117) << 8))
            << " CGADD=$" << static_cast<int>(ppu.readRegister(0x2121)) << std::dec << '\n';

    // Window registers
    logFile << "  W12SEL=$" << std::hex << static_cast<int>(ppu.readRegister(0x2123))
            << " W34SEL=$" << static_cast<int>(ppu.readRegister(0x2124))
            << " WOBJSEL=$" << static_cast<int>(ppu.readRegister(0x2125))
            << " WH0=$" << static_cast<int>(ppu.readRegister(0x2126))
            << " WH1=$" << static_cast<int>(ppu.readRegister(0x2127))
            << " WH2=$" << static_cast<int>(ppu.readRegister(0x2128))
            << " WH3=$" << static_cast<int>(ppu.readRegister(0x2129))
            << " WBGLOG=$" << static_cast<int>(ppu.readRegister(0x212a))
            << " WOBJLOG=$" << static_cast<int>(ppu.readRegister(0x212b)) << std::dec << '\n';

    // NMI/IRQ
    logFile << "  NMI_EN=$" << std::hex << static_cast<int>(bus.readMmio(0x4200))
            << " VTIME=$" << static_cast<int>(bus.readMmio(0x4209) | (bus.readMmio(0x420a) << 8))
            << " HTIME=$" << static_cast<int>(bus.readMmio(0x4207) | (bus.readMmio(0x4208) << 8))
            << " DMAEN=$" << static_cast<int>(bus.readMmio(0x420b))
            << " HDMAEN=$" << static_cast<int>(bus.readMmio(0x420c)) << std::dec << '\n';

    // OBJ registers
    logFile << "  OBJSEL=$" << std::hex << static_cast<int>(ppu.readRegister(0x2101)) << std::dec << '\n';

    // Mode 7 registers
    logFile << "  M7A=$" << std::hex << static_cast<int>(ppu.readRegister(0x211b) | (ppu.readRegister(0x211c) << 8))
            << " M7B=$" << static_cast<int>(ppu.readRegister(0x211d) | (ppu.readRegister(0x211e) << 8))
            << " M7C=$" << static_cast<int>(ppu.readRegister(0x211f) | (ppu.readRegister(0x2120) << 8))
            << " M7D=$" << static_cast<int>(ppu.readRegister(0x211b) | (ppu.readRegister(0x211c) << 8))
            << " M7X=$" << static_cast<int>(ppu.readRegister(0x211a) | (ppu.readRegister(0x211b) << 8))
            << " M7Y=$" << static_cast<int>(ppu.readRegister(0x211c) | (ppu.readRegister(0x211d) << 8))
            << " M7SEL=$" << static_cast<int>(ppu.readRegister(0x211a)) << std::dec << '\n';

    // Debug flags
    logFile << "  debugFlags=$" << std::hex << static_cast<int>(ppu.debugFlagsMask()) << std::dec << '\n';

    // First 64 bytes of VRAM (tilemap area for BG1 at $0000)
    logFile << "  VRAM[0x0000-0x003F]: ";
    for (int i = 0; i < 64; ++i) {
        logFile << std::hex << static_cast<int>(ppu.vramWord(i) & 0xff) << ' ';
    }
    logFile << std::dec << '\n';

    // First 16 CGRAM entries
    logFile << "  CGRAM[0-15]: ";
    for (int i = 0; i < 16; ++i) {
        logFile << std::hex << "0x" << static_cast<int>(ppu.cgramColor(i)) << ' ';
    }
    logFile << std::dec << '\n';

    // APU port state
    const auto& apl = bus.getApuPortLog();
    logFile << "  APU_R=[" << std::hex
            << static_cast<int>(apl.lastRead[0]) << ' '
            << static_cast<int>(apl.lastRead[1]) << ' '
            << static_cast<int>(apl.lastRead[2]) << ' '
            << static_cast<int>(apl.lastRead[3]) << "]"
            << " W=[" 
            << static_cast<int>(apl.lastWrite[0]) << ' '
            << static_cast<int>(apl.lastWrite[1]) << ' '
            << static_cast<int>(apl.lastWrite[2]) << ' '
            << static_cast<int>(apl.lastWrite[3]) << "]"
            << " RDcnt=[" << std::dec
            << apl.readCount[0] << ' '
            << apl.readCount[1] << ' '
            << apl.readCount[2] << ' '
            << apl.readCount[3] << "]"
            << " WRcnt=[" << std::dec
            << apl.writeCount[0] << ' '
            << apl.writeCount[1] << ' '
            << apl.writeCount[2] << ' '
            << apl.writeCount[3] << "]"
            << " totalR=" << apl.totalReads
            << " totalW=" << apl.totalWrites
            << '\n';

    // Key WRAM locations
    logFile << "  WRAM[05B4]=$" << std::hex << static_cast<int>(bus.read8(0x0005b4))
            << " WRAM[05B5]=$" << static_cast<int>(bus.read8(0x0005b5))
            << " WRAM[05B6]=$" << static_cast<int>(bus.read8(0x0005b6))
            << " WRAM[05BA]=$" << static_cast<int>(bus.read8(0x0005ba))
            << std::dec << '\n';

    // Game flag ($05B4) write tracking
    const auto& gfl = bus.getGameFlagLog();
    logFile << "  05B4_WRITES=" << std::dec << gfl.writeCount
            << " LAST_05B4_WRITTEN=$" << std::hex << static_cast<int>(gfl.lastValueWritten)
            << std::dec << '\n';

    logFile.flush();
}

void handleKey(void* userData, int key, bool pressed)
{
    auto* input = static_cast<InputContext*>(userData);
    if (!input) {
        return;
    }

    if (pressed) {
        if (key == SDLK_F1 && input->ppu) {
            input->ppu->setDebugFlags(input->ppu->debugFlagsMask() ^ snesquik::ppu::Ppu::DebugNoColorMath);
            return;
        }
        if (key == SDLK_F2 && input->ppu) {
            input->ppu->setDebugFlags(input->ppu->debugFlagsMask() ^ snesquik::ppu::Ppu::DebugNoWindows);
            return;
        }
        if (key == SDLK_F3 && input->ppu) {
            input->ppu->setDebugFlags(input->ppu->debugFlagsMask() ^ snesquik::ppu::Ppu::DebugOnlyBG3);
            return;
        }
        if (key == SDLK_F4 && input->ppu) {
            input->ppu->setDebugFlags(input->ppu->debugFlagsMask() ^ snesquik::ppu::Ppu::DebugForceBG3);
            return;
        }
        if (key == SDLK_F6) {
            input->saveStateRequested = true;
            return;
        }
        if (key == SDLK_F7) {
            input->loadStateRequested = true;
            return;
        }
        if (key == SDLK_F5) {
            if (!input->f5WasDown) {
                input->logActive = !input->logActive;
                if (input->logActive) {
                    logFile.open("snesquik_log.txt", std::ios::out | std::ios::trunc);
                    if (logFile.is_open()) {
                        std::cout << "Logging ON -> snesquik_log.txt\n";
                    } else {
                        std::cerr << "Failed to open snesquik_log.txt\n";
                        input->logActive = false;
                    }
                } else {
                    logFile.close();
                    std::cout << "Logging OFF\n";
                }
                input->f5WasDown = true;
            }
            return;
        }
    }

    if (key == SDLK_F5 && !pressed) {
        input->f5WasDown = false;
    }

    const uint16_t mask = joypadMaskForKey(key);
    if (mask == 0 || !input->bus) {
        return;
    }
    if (pressed) {
        input->state |= mask;
    } else {
        input->state &= static_cast<uint16_t>(~mask);
    }
    input->bus->setJoypadState(input->state);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: snesquik <rom.sfc> [--probe outdir] [--frames n] [--trace-steps n] [--snapshot-every n]\n";
        return 1;
    }

    snesquik::debug::ProbeOptions probeOptions;
    probeOptions.romPath = argv[1];
    bool probeMode = false;
    uint32_t logFromFrame = UINT32_MAX;
    for (int i = 2; i < argc; ++i) {
        if (isOption(argv[i], "--probe") && i + 1 < argc) {
            probeMode = true;
            probeOptions.outputDirectory = argv[++i];
        } else if (isOption(argv[i], "--frames") && i + 1 < argc) {
            probeOptions.frames = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (isOption(argv[i], "--trace-steps") && i + 1 < argc) {
            probeOptions.traceSteps = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (isOption(argv[i], "--snapshot-every") && i + 1 < argc) {
            probeOptions.snapshotEvery = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (isOption(argv[i], "--probe-press-a") && i + 1 < argc) {
            probeOptions.pressFrame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            probeOptions.injectedButtons |= snesquik::bus::JoypadA;
        } else if (isOption(argv[i], "--probe-release-a") && i + 1 < argc) {
            probeOptions.releaseFrame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            probeOptions.injectedButtons |= snesquik::bus::JoypadA;
        } else if (isOption(argv[i], "--probe-press") && i + 2 < argc) {
            const uint16_t mask = joypadMaskForName(argv[++i]);
            probeOptions.pressFrame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            probeOptions.injectedButtons |= mask;
        } else if (isOption(argv[i], "--probe-release") && i + 2 < argc) {
            const uint16_t mask = joypadMaskForName(argv[++i]);
            probeOptions.releaseFrame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            probeOptions.injectedButtons |= mask;
        } else if (isOption(argv[i], "--load-state") && i + 1 < argc) {
            probeOptions.loadStatePath = argv[++i];
        } else if (isOption(argv[i], "--save-state") && i + 2 < argc) {
            probeOptions.saveStateFrame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            probeOptions.saveStatePath = argv[++i];
        } else if (isOption(argv[i], "--dump-audio") && i + 1 < argc) {
            probeOptions.dumpAudioPath = argv[++i];
        } else if (isOption(argv[i], "--gsu-trace") && i + 2 < argc) {
            probeOptions.gsuTraceFrame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            probeOptions.gsuTraceCount = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (isOption(argv[i], "--probe-poke") && i + 3 < argc) {
            const uint32_t frame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            const uint32_t addr = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 16));
            const uint8_t val = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 16));
            probeOptions.wramPokes.push_back({frame, addr, val});
        } else if (isOption(argv[i], "--dump-state") && i + 1 < argc) {
            probeOptions.dumpStateFrame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (isOption(argv[i], "--probe-tap") && i + 2 < argc) {
            const uint16_t mask = joypadMaskForName(argv[++i]);
            const uint32_t frame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            probeOptions.joypadEvents.push_back({frame, mask, true});
            probeOptions.joypadEvents.push_back({frame + 10, mask, false});
        } else if (isOption(argv[i], "--probe-hold") && i + 3 < argc) {
            const uint16_t mask = joypadMaskForName(argv[++i]);
            const uint32_t pressFrame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            const uint32_t releaseFrame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            probeOptions.joypadEvents.push_back({pressFrame, mask, true});
            probeOptions.joypadEvents.push_back({releaseFrame, mask, false});
        } else if (isOption(argv[i], "--log-from") && i + 1 < argc) {
            logFromFrame = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        }
    }

    if (probeMode) {
        const auto result = snesquik::debug::runProbe(probeOptions);
        if (!result.ok) {
            std::cerr << "probe failed: " << result.message << '\n';
            return 1;
        }
        std::cout << result.message << ": reset PC=$" << std::hex << result.resetPc << std::dec
                  << " cycles=" << result.cpuCycles << '\n';
        return 0;
    }

    const std::vector<uint8_t> file = readFile(argv[1]);
    if (file.empty()) {
        std::cerr << "failed to read ROM: " << argv[1] << '\n';
        return 1;
    }

    const auto parsed = snesquik::cartridge::parseRomImage(std::span<const uint8_t>(file.data(), file.size()));
    if (!parsed) {
        std::cerr << "failed to parse ROM header\n";
        return 1;
    }

    snesquik::ppu::Ppu ppu;
    ppu.reset();

    snesquik::bus::SnesBus bus;
    MainTraceListener trace;
bus.setTraceListener(&trace);
    bus.attachPpu(&ppu);
    bus.attachCartridge(parsed->rom, parsed->header.map, parsed->header.declaredRamSizeBytes());
    bus.initApu();
    if (parsed->header.hasSuperFx()) {
        bus.attachGsu(parsed->header.superFxRamSizeBytes());
        std::cout << "Super FX (GSU) coprocessor attached, "
                  << parsed->header.superFxRamSizeBytes() / 1024 << " KB RAM\n";
    }
    if (parsed->header.hasSa1()) {
        bus.attachSa1(parsed->header.sa1BwRamSizeBytes());
        std::cout << "SA-1 coprocessor attached, "
                  << parsed->header.sa1BwRamSizeBytes() / 1024 << " KB BW-RAM\n";
    }
    if (parsed->header.hasDsp()) {
        std::string dspPath = resolveDspRomPath();
        bus.attachDsp(dspPath, parsed->header.map);
        if (bus.getDsp().loaded()) {
            std::cout << "DSP-1 coprocessor attached (" << dspPath << ")\n";
        } else {
            std::cerr << "warning: DSP-1 cartridge but no usable ROM dump found "
                         "(set SNESQUIK_DSP1_ROM or place tests/roms/dsp1b.rom); "
                         "the DSP is inert and the game will not run correctly\n";
        }
    }

    snesquik::cpu_r5a22::CPU cpu(bus);
    cpu.reset();

    std::cout << parsed->header.title << " (" << snesquik::cartridge::cartridgeMapName(parsed->header.map) << ")\n";
    if (parsed->header.chipset >= 0x03 && !parsed->header.hasSuperFx()
        && !parsed->header.hasSa1() && !parsed->header.hasDsp()) {
        std::cerr << "warning: cartridge requires an enhancement coprocessor (chipset $"
                  << std::hex << static_cast<int>(parsed->header.chipset) << std::dec
                  << ") which is not emulated; the game will not run correctly\n";
    }
    std::cout << "reset PC: $" << std::hex << cpu.registers().pc << std::dec << '\n';
    std::cout << "F1=color math  F2=windows  F3=BG3 only  F4=force BG3  F5=log  F6=save state  F7=load state\n";
    const std::string statePath = std::string(argv[1]) + ".state";

    snesquik::platform::SdlGlRenderer renderer;
    if (!renderer.initialize("snesquik", snesquik::ppu::Ppu::screenWidth, snesquik::ppu::Ppu::screenHeight)) {
        std::cerr << "renderer init failed: " << renderer.lastError() << '\n';
        return 1;
    }
    InputContext input{&bus, &ppu, 0, false, false};
    renderer.setKeyCallback(handleKey, &input);

    SDL_AudioDeviceID audioDevice = 0;
    // SDL_GetQueuedAudioSize reports bytes in the *device* format, which can
    // differ from what we queue (e.g. 48 kHz float on PipeWire); convert
    // queue depth to seconds using the actual device spec.
    double audioBytesPerSecond = 32000.0 * 4.0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want{};
        SDL_AudioSpec have{};
        want.freq = snesquik::apu::Apu::sampleRate;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;
        audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (audioDevice != 0) {
            audioBytesPerSecond = static_cast<double>(have.freq) * have.channels
                                * (SDL_AUDIO_BITSIZE(have.format) / 8);
            // Prime the queue with ~100 ms of silence so small timing
            // drift between vsync and the SNES frame rate doesn't
            // immediately underrun (audible as crackle).
            static const int16_t silence[3200 * 2] = {};
            SDL_QueueAudio(audioDevice, silence, sizeof(silence));
            SDL_PauseAudioDevice(audioDevice, 0);
        } else {
            std::cerr << "audio device unavailable: " << SDL_GetError() << '\n';
        }
    }

    uint32_t audioUnderruns = 0;
    uint32_t audioOverruns = 0;
    uint32_t audioProducedLastSecond = 0;
    double audioQueuedMsLastSample = 0.0;
    double audioRatioLastSample = 1.0;
    std::ofstream audioTee;
    if (const char* teePath = std::getenv("SNESQUIK_AUDIO_DUMP")) {
        audioTee.open(teePath, std::ios::binary | std::ios::trunc);
        if (audioTee.is_open()) {
            std::cout << "audio tee (s16le stereo 32000 Hz) -> " << teePath << '\n';
        }
    }
    uint32_t fpsFrameCount = 0;
    uint32_t fpsStartTime = SDL_GetTicks();
    float currentFps = 0.0f;

    // Frame limiter: pace to the NTSC frame rate. One frame is
    // 341 dots * 262 lines * 4 master clocks = 357368 clocks of the
    // 21.477272 MHz master clock, i.e. ~60.0988 Hz.
    const uint64_t perfFreq = SDL_GetPerformanceFrequency();
    const double framePeriod = static_cast<double>(perfFreq) * 357368.0 / 21477272.0;
    double frameDeadline = static_cast<double>(SDL_GetPerformanceCounter()) + framePeriod;

    bool running = true;
    uint32_t dotRemainder = 0;
    uint32_t dotsSincePoll = 0;
    if (logFromFrame < UINT32_MAX) {
        logFile.open("snesquik_log.txt", std::ios::out | std::ios::trunc);
        if (logFile.is_open()) {
            std::cout << "Auto-logging from frame " << logFromFrame << " -> snesquik_log.txt\n";
        } else {
            std::cerr << "Failed to open snesquik_log.txt\n";
            logFromFrame = UINT32_MAX;
        }
    }
    constexpr uint32_t pollInterval = 341;
    while (running) {
        uint32_t dotsThisFrame = 0;
        bool nmiRequested = false;
        dotsSincePoll = 0;
        bus.clearNmiEdge();
        bus.resetApuPortLog();
        ppu.beginFrame();
        uint16_t lastLine = ppu.verticalCounter();
        bus.setVblank(false);
        bus.beginFrame();
        // Hardware performs the first HDMA transfer during scanline 0,
        // before the first visible line is rendered.
        bus.runHdmaScanline();

        while (dotsThisFrame < frameDots) {
            const uint32_t cpuCycles = cpu.step();
            const uint32_t dotAccum = cpuCycles * 3 + dotRemainder;
            uint32_t ppuDots = dotAccum / 2;
            dotRemainder = dotAccum % 2;
            const uint32_t dmaDots = bus.consumeDmaDots();
            ppuDots += dmaDots;
            // The SPC keeps running while the CPU is halted for DMA, so
            // advance the APU for that time too (1 dot = 4 master clocks,
            // 1 cycle ~= 6, hence dots * 2 / 3).
            bus.stepApu(cpuCycles + dmaDots * 2 / 3);
            // The GSU runs in parallel as well (units: master clocks).
            bus.stepGsu(cpuCycles * 6 + dmaDots * 4);
            // The SA-1 second 65816 likewise runs in parallel (master clocks).
            bus.stepSa1(cpuCycles * 6 + dmaDots * 4);
            // The DSP-1 runs continuously; advance it proportional to S-CPU
            // cycles (the RQM handshake handles exact synchronization).
            bus.stepDsp(cpuCycles + dmaDots);

            const uint16_t prevH = ppu.horizontalCounter();
            const uint16_t prevV = ppu.verticalCounter();
            ppu.tick(ppuDots);
            dotsThisFrame += ppuDots;

            dotsSincePoll += ppuDots;
            if (dotsSincePoll >= pollInterval) {
                dotsSincePoll = 0;
                running = renderer.pollEvents();
                if (!running) {
                    break;
                }
            }

            bus.checkIrqCrossing(prevH, prevV, ppu.horizontalCounter(), ppu.verticalCounter());
            // Level-triggered: the line stays asserted until the game acks
            // via $4211 / $3031 (or disables the source); taken when I=0.
            cpu.setIrqLine(bus.irqFlag() || bus.gsuIrqPending() || bus.sa1IrqPending());

            if (ppu.verticalCounter() != lastLine) {
                const uint16_t completedLine = lastLine;
                lastLine = ppu.verticalCounter();
                const uint16_t vh = static_cast<uint16_t>(ppu.visibleHeight());
                if (completedLine < vh) {
                    ppu.renderScanline(completedLine);
                }
                if (lastLine <= vh) {
                    bus.runHdmaScanline();
                }
            }

            if (!nmiRequested && ppu.verticalCounter() >= static_cast<uint16_t>(ppu.visibleHeight())) {
                bus.setVblank(true);
                bus.beginJoypadAutoRead();
                if (bus.nmiEnabled()) {
                    cpu.requestNMI();
                }
                nmiRequested = true;
            }

            bus.tickJoypadAutoRead(cpuCycles);

            if (bus.nmiEdgePending() && bus.nmiEnabled()) {
                cpu.requestNMI();
                bus.clearNmiEdge();
            }

            if (cpu.stopped()) {
                break;
            }
        }

        bus.endApuFrame();

        if (audioDevice != 0) {
            const auto samples = bus.getApu().frameSamples();
            // Dynamic rate control: hold the queue near the 100 ms target by
            // gently stretching audio when it drifts. The instantaneous
            // queue depth jitters by a whole audio-callback chunk, so the
            // measurement is smoothed and the ratio slew-limited; within the
            // deadband samples pass through bit-exact (the frame limiter
            // already matches production and consumption rates).
            const double queuedSeconds =
                static_cast<double>(SDL_GetQueuedAudioSize(audioDevice)) / audioBytesPerSecond;
            if (!samples.empty() && queuedSeconds >= 0.25) {
                // Dropping a frame of audio is an audible splice; track it.
                ++audioOverruns;
            } else if (!samples.empty()) {
                constexpr double targetSeconds = 0.1;
                static double queuedSmoothed = targetSeconds;
                static double ratio = 1.0;
                static double rateBias = 0.0;

                if (queuedSeconds <= 0.0) {
                    ++audioUnderruns;
                }
                queuedSmoothed += (queuedSeconds - queuedSmoothed) * 0.05;

                double error = (targetSeconds - queuedSmoothed) / targetSeconds;
                error = std::clamp(error, -1.0, 1.0);
                // PI control: the integral learns the device's true clock
                // rate, which can deviate from nominal by far more than the
                // proportional band (measured ~+0.6% under PipeWire here).
                rateBias = std::clamp(rateBias + error * 0.00005, -0.02, 0.02);
                const double ratioTarget = std::clamp(1.0 + rateBias + error * 0.01, 0.97, 1.03);
                ratio += std::clamp(ratioTarget - ratio, -0.0005, 0.0005);

                audioProducedLastSecond += static_cast<uint32_t>(samples.size() / 2);
                audioQueuedMsLastSample = queuedSeconds * 1000.0;
                audioRatioLastSample = ratio;

                const int16_t* out = samples.data();
                size_t outCount = samples.size();
                static std::vector<int16_t> resampled;
                if (std::abs(ratio - 1.0) >= 0.0005) {
                    const int inFrames = static_cast<int>(samples.size() / 2);
                    const int outFrames = std::max(1, static_cast<int>(inFrames * ratio + 0.5));
                    resampled.resize(static_cast<size_t>(outFrames) * 2);
                    for (int i = 0; i < outFrames; ++i) {
                        const double pos = static_cast<double>(i) * inFrames / outFrames;
                        const int i0 = static_cast<int>(pos);
                        const int i1 = std::min(i0 + 1, inFrames - 1);
                        const double frac = pos - i0;
                        for (int c = 0; c < 2; ++c) {
                            const double a = samples[static_cast<size_t>(i0) * 2 + c];
                            const double b = samples[static_cast<size_t>(i1) * 2 + c];
                            resampled[static_cast<size_t>(i) * 2 + c] =
                                static_cast<int16_t>(a + (b - a) * frac);
                        }
                    }
                    out = resampled.data();
                    outCount = resampled.size();
                }
                SDL_QueueAudio(audioDevice, out, static_cast<Uint32>(outCount * sizeof(int16_t)));
                if (audioTee.is_open()) {
                    audioTee.write(reinterpret_cast<const char*>(out),
                                   static_cast<std::streamsize>(outCount * sizeof(int16_t)));
                }
            }
        }

        if (input.saveStateRequested) {
            input.saveStateRequested = false;
            std::string err;
            if (snesquik::state::save(statePath, cpu, bus, ppu, &err)) {
                std::cout << "state saved: " << statePath << '\n';
            } else {
                std::cerr << "state save failed: " << err << '\n';
            }
        }
        if (input.loadStateRequested) {
            input.loadStateRequested = false;
            std::string err;
            if (snesquik::state::load(statePath, cpu, bus, ppu, &err)) {
                std::cout << "state loaded: " << statePath << '\n';
            } else {
                std::cerr << "state load failed: " << err << '\n';
            }
        }

        const bool shouldLog = input.logActive || frameNumber >= logFromFrame;
        if (shouldLog) {
            logFrameState(ppu, bus, cpu, frameNumber);
        }

        renderer.present(ppu.framebuffer());
        ++frameNumber;

        uint64_t perfNow = SDL_GetPerformanceCounter();
        if (static_cast<double>(perfNow) > frameDeadline + 4.0 * framePeriod) {
            // Fell far behind (e.g. window dragged, heavy load): resync
            // instead of fast-forwarding to catch up.
            frameDeadline = static_cast<double>(perfNow) + framePeriod;
        } else {
            while (static_cast<double>(perfNow) < frameDeadline) {
                const double remainingMs =
                    (frameDeadline - static_cast<double>(perfNow)) * 1000.0 / static_cast<double>(perfFreq);
                if (remainingMs > 2.0) {
                    SDL_Delay(static_cast<Uint32>(remainingMs - 1.0));
                } else {
                    SDL_Delay(0);
                }
                perfNow = SDL_GetPerformanceCounter();
            }
            frameDeadline += framePeriod;
        }

        ++fpsFrameCount;
        const uint32_t now = SDL_GetTicks();
        const uint32_t elapsed = now - fpsStartTime;
        if (elapsed >= 1000) {
            currentFps = static_cast<float>(fpsFrameCount) * 1000.0f / static_cast<float>(elapsed);
            fpsFrameCount = 0;
            fpsStartTime = now;
            char title[128];
            const bool audioVerbose = std::getenv("SNESQUIK_AUDIO_STATS") != nullptr;
            if (audioVerbose && (audioUnderruns == 0 && audioOverruns == 0)) {
                std::cerr << "audio: ur=0 ov=0 produced=" << audioProducedLastSecond
                          << " queuedMs=" << static_cast<int>(audioQueuedMsLastSample)
                          << " ratio=" << audioRatioLastSample
                          << " fps=" << currentFps << '\n';
            }
            if (audioUnderruns > 0 || audioOverruns > 0) {
                std::snprintf(title, sizeof(title), "snesquik [F:%.1f] [AUD ur:%u ov:%u] %s%s",
                              currentFps, audioUnderruns, audioOverruns,
                              input.logActive ? "[LOG] " : "",
                              parsed->header.title.c_str());
                std::cerr << "audio: ur=" << audioUnderruns << " ov=" << audioOverruns
                          << " produced=" << audioProducedLastSecond
                          << " queuedMs=" << static_cast<int>(audioQueuedMsLastSample)
                          << " ratio=" << audioRatioLastSample
                          << " fps=" << currentFps << '\n';
                audioUnderruns = 0;
                audioOverruns = 0;
            } else {
                std::snprintf(title, sizeof(title), "snesquik [F:%.1f] %s%s",
                              currentFps,
                              input.logActive ? "[LOG] " : "",
                              parsed->header.title.c_str());
            }
            audioProducedLastSecond = 0;
            renderer.setWindowTitle(title);
        }
    }

    if (audioDevice != 0) {
        SDL_CloseAudioDevice(audioDevice);
    }

    if (logFile.is_open()) {
        logFile.close();
    }

    return 0;
}
