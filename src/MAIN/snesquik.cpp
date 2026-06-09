#include "BUS/bus.h"
#include "CART/rom_parser.h"
#include "CPU_R5A22/core.h"
#include "DEBUG/probe.h"
#include "MAIN/sdl_gl_renderer.h"
#include "S-PPU/ppu.h"

#include <SDL_keycode.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t cpuCycleToPpuDots = 6;
constexpr uint32_t frameDots = snesquik::ppu::Ppu::dotsPerScanline * snesquik::ppu::Ppu::ntscScanlines;

struct InputContext {
    snesquik::bus::SnesBus* bus = nullptr;
    uint16_t state = 0;
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

void handleKey(void* userData, int key, bool pressed)
{
    auto* input = static_cast<InputContext*>(userData);
    const uint16_t mask = joypadMaskForKey(key);
    if (mask == 0 || !input || !input->bus) {
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
    bus.attachPpu(&ppu);
    bus.attachCartridge(parsed->rom, parsed->header.map, parsed->header.declaredRamSizeBytes());

    snesquik::cpu_r5a22::CPU cpu(bus);
    cpu.reset();

    std::cout << parsed->header.title << " (" << snesquik::cartridge::cartridgeMapName(parsed->header.map) << ")\n";
    std::cout << "reset PC: $" << std::hex << cpu.registers().pc << std::dec << '\n';

    snesquik::platform::SdlGlRenderer renderer;
    if (!renderer.initialize("snesquik", snesquik::ppu::Ppu::screenWidth, snesquik::ppu::Ppu::screenHeight)) {
        std::cerr << "renderer init failed: " << renderer.lastError() << '\n';
        return 1;
    }
    InputContext input{&bus, 0};
    renderer.setKeyCallback(handleKey, &input);

    bool running = true;
    while (running) {
        uint32_t dotsThisFrame = 0;
        bool nmiRequested = false;
        ppu.beginFrame();
        uint16_t lastLine = ppu.verticalCounter();
        bus.setVblank(false);
        bus.beginFrame();
        if (lastLine < static_cast<uint16_t>(ppu.visibleHeight())) {
            bus.runHdmaScanline();
        }

        while (dotsThisFrame < frameDots) {
            running = renderer.pollEvents();
            if (!running) {
                break;
            }

            const uint32_t cpuCycles = cpu.step();
            const uint32_t ppuDots = cpuCycles * cpuCycleToPpuDots;
            ppu.tick(ppuDots);
            dotsThisFrame += ppuDots;

            if (ppu.verticalCounter() != lastLine) {
                const uint16_t completedLine = lastLine;
                lastLine = ppu.verticalCounter();
                if (completedLine < static_cast<uint16_t>(ppu.visibleHeight())) {
                    ppu.renderScanline(completedLine);
                }
                if (lastLine < static_cast<uint16_t>(ppu.visibleHeight())) {
                    bus.runHdmaScanline();
                }
            }

            if (!nmiRequested && ppu.verticalCounter() >= static_cast<uint16_t>(ppu.visibleHeight())) {
                bus.setVblank(true);
                bus.beginJoypadAutoRead();
                bus.finishJoypadAutoRead();
                if (bus.nmiEnabled()) {
                    cpu.requestNMI();
                }
                nmiRequested = true;
            }

            if (cpu.stopped()) {
                break;
            }
        }

        renderer.present(ppu.framebuffer());
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
}
