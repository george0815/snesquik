#include "DEBUG/probe.h"

#include "BUS/bus.h"
#include "CART/rom_parser.h"
#include "CPU_R5A22/core.h"
#include "S-PPU/ppu.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <span>
#include <sstream>
#include <vector>

namespace snesquik::debug {

namespace {

constexpr uint32_t cpuCycleToPpuDots = 6;
constexpr uint32_t frameDots = ppu::Ppu::dotsPerScanline * ppu::Ppu::ntscScanlines;

std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string hexValue(uint64_t value, int width)
{
    std::ostringstream stream;
    stream << '$' << std::hex << std::uppercase << std::setw(width) << std::setfill('0') << value;
    return stream.str();
}

void writeU16(std::ofstream& out, uint16_t value)
{
    out.put(static_cast<char>(value));
    out.put(static_cast<char>(value >> 8));
}

void writeU32(std::ofstream& out, uint32_t value)
{
    writeU16(out, static_cast<uint16_t>(value));
    writeU16(out, static_cast<uint16_t>(value >> 16));
}

uint32_t crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xffffffff;
}

uint32_t adler32(const std::vector<uint8_t>& data)
{
    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t value : data) {
        a = (a + value) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void appendBe32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value >> 24));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

void writeChunk(std::ofstream& out, const char type[4], const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> chunk;
    chunk.insert(chunk.end(), type, type + 4);
    chunk.insert(chunk.end(), payload.begin(), payload.end());
    std::array<uint8_t, 4> be = {
        static_cast<uint8_t>(payload.size() >> 24),
        static_cast<uint8_t>(payload.size() >> 16),
        static_cast<uint8_t>(payload.size() >> 8),
        static_cast<uint8_t>(payload.size()),
    };
    out.write(reinterpret_cast<const char*>(be.data()), be.size());
    out.write(type, 4);
    out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    const uint32_t crc = crc32(chunk.data(), chunk.size());
    be = {
        static_cast<uint8_t>(crc >> 24),
        static_cast<uint8_t>(crc >> 16),
        static_cast<uint8_t>(crc >> 8),
        static_cast<uint8_t>(crc),
    };
    out.write(reinterpret_cast<const char*>(be.data()), be.size());
}

bool savePng(const std::filesystem::path& path, std::span<const uint32_t> rgba, int width, int height)
{
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>((width * 4 + 1) * height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < width; ++x) {
            const uint32_t pixel = rgba[static_cast<size_t>(y * width + x)];
            raw.push_back(static_cast<uint8_t>(pixel >> 16));
            raw.push_back(static_cast<uint8_t>(pixel >> 8));
            raw.push_back(static_cast<uint8_t>(pixel));
            raw.push_back(0xff);
        }
    }

    std::vector<uint8_t> compressed;
    compressed.push_back(0x78);
    compressed.push_back(0x01);
    size_t offset = 0;
    while (offset < raw.size()) {
        const uint16_t block = static_cast<uint16_t>(std::min<size_t>(65535, raw.size() - offset));
        compressed.push_back(offset + block == raw.size() ? 0x01 : 0x00);
        compressed.push_back(static_cast<uint8_t>(block));
        compressed.push_back(static_cast<uint8_t>(block >> 8));
        compressed.push_back(static_cast<uint8_t>(~block));
        compressed.push_back(static_cast<uint8_t>((~block) >> 8));
        compressed.insert(compressed.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + block));
        offset += block;
    }
    appendBe32(compressed, adler32(raw));

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    static constexpr std::array<uint8_t, 8> signature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    out.write(reinterpret_cast<const char*>(signature.data()), signature.size());

    std::vector<uint8_t> ihdr;
    appendBe32(ihdr, static_cast<uint32_t>(width));
    appendBe32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    writeChunk(out, "IHDR", ihdr);
    writeChunk(out, "IDAT", compressed);
    writeChunk(out, "IEND", {});
    return true;
}

class ProbeTrace final : public bus::TraceListener {
public:
    explicit ProbeTrace(const std::filesystem::path& path)
        : log(path)
    {
    }

    void mmioRead(uint16_t address, uint8_t value) override
    {
        if (address == 0x4016 || address == 0x4017 || address >= 0x4200 || address < 0x2144) {
            log << "MMIO R " << hexValue(address, 4) << " = " << hexValue(value, 2) << '\n';
        }
    }

    void mmioWrite(uint16_t address, uint8_t value) override
    {
        if (address == 0x4016 || address == 0x4017 || address >= 0x4200 || address < 0x2144) {
            log << "MMIO W " << hexValue(address, 4) << " = " << hexValue(value, 2) << '\n';
        }
    }

    void dmaStart(uint8_t channel, uint16_t size) override
    {
        log << "DMA channel=" << static_cast<unsigned>(channel) << " size=" << (size == 0 ? 65536 : size) << '\n';
    }

    void vblank(bool active) override
    {
        log << "VBLANK " << (active ? "begin" : "end") << '\n';
    }

private:
    std::ofstream log;
};

void logCpuStep(std::ofstream& log, uint64_t step, const cpu_r5a22::CPU& cpu)
{
    const auto& r = cpu.registers();
    log << std::dec << step
        << " PB:PC=" << hexValue(r.pb, 2) << ':' << hexValue(r.pc, 4)
        << " OP=" << hexValue(cpu.lastOpcode(), 2)
        << " A=" << hexValue(r.a, 4)
        << " X=" << hexValue(r.x, 4)
        << " Y=" << hexValue(r.y, 4)
        << " S=" << hexValue(r.s, 4)
        << " D=" << hexValue(r.d, 4)
        << " DB=" << hexValue(r.db, 2)
        << " P=" << hexValue(r.p, 2)
        << " E=" << (r.emulation ? 1 : 0)
        << " cycles=" << cpu.totalCycles()
        << '\n';
}

} // namespace

ProbeResult runProbe(const ProbeOptions& options)
{
    ProbeResult result;
    const std::vector<uint8_t> file = readFile(options.romPath);
    if (file.empty()) {
        result.message = "failed to read ROM";
        return result;
    }

    const auto parsed = cartridge::parseRomImage(std::span<const uint8_t>(file.data(), file.size()));
    if (!parsed) {
        result.message = "failed to parse ROM header";
        return result;
    }

    std::filesystem::create_directories(options.outputDirectory);
    ProbeTrace trace(std::filesystem::path(options.outputDirectory) / "mmio.log");
    std::ofstream cpuLog(std::filesystem::path(options.outputDirectory) / "cpu.log");
    std::ofstream summary(std::filesystem::path(options.outputDirectory) / "summary.txt");

    ppu::Ppu ppu;
    ppu.reset();

    bus::SnesBus bus;
    bus.attachPpu(&ppu);
    bus.attachCartridge(parsed->rom, parsed->header.map, parsed->header.declaredRamSizeBytes());
    bus.setTraceListener(&trace);

    cpu_r5a22::CPU cpu(bus);
    cpu.reset();

    result.resetPc = cpu.registers().pc;
    summary << "title=" << parsed->header.title << '\n';
    summary << "map=" << cartridge::cartridgeMapName(parsed->header.map) << '\n';
    summary << "reset_pc=" << hexValue(result.resetPc, 4) << '\n';
    if (options.injectedButtons != 0) {
        summary << "inject_buttons=" << hexValue(options.injectedButtons, 4) << '\n';
        summary << "inject_press_frame=" << options.pressFrame << '\n';
        summary << "inject_release_frame=" << options.releaseFrame << '\n';
    }

    uint64_t globalStep = 0;
    uint16_t injectedJoypadState = 0;
    for (uint32_t frame = 0; frame < options.frames; ++frame) {
        if (options.injectedButtons != 0 && frame == options.pressFrame) {
            injectedJoypadState |= options.injectedButtons;
            bus.setJoypadState(injectedJoypadState);
            summary << "pressed_frame=" << frame << '\n';
        }
        if (options.injectedButtons != 0 && frame == options.releaseFrame) {
            injectedJoypadState &= static_cast<uint16_t>(~options.injectedButtons);
            bus.setJoypadState(injectedJoypadState);
            summary << "released_frame=" << frame << '\n';
        }

        uint32_t dotsThisFrame = 0;
        bool nmiRequested = false;
        ppu.beginFrame();
        uint16_t lastLine = ppu.verticalCounter();
        bus.setVblank(false);
        bus.beginFrame();
        if (lastLine < static_cast<uint16_t>(ppu.visibleHeight())) {
            bus.runHdmaScanline();
        }

        while (dotsThisFrame < frameDots && !cpu.stopped()) {
            const uint32_t cpuCycles = cpu.step();
            if (globalStep < options.traceSteps) {
                logCpuStep(cpuLog, globalStep, cpu);
            }
            ++globalStep;

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
        }

        if (options.snapshotEvery != 0 && frame % options.snapshotEvery == 0) {
            std::ostringstream name;
            name << "frame_" << std::setw(4) << std::setfill('0') << frame << ".png";
            savePng(std::filesystem::path(options.outputDirectory) / name.str(), ppu.framebuffer(), ppu::Ppu::screenWidth, ppu::Ppu::screenHeight);
        }
    }

    result.ok = true;
    result.cpuCycles = cpu.totalCycles();
    result.message = "probe complete";
    summary << "frames=" << options.frames << '\n';
    summary << "cpu_cycles=" << result.cpuCycles << '\n';
    summary << "final_pc=" << hexValue(cpu.registers().pc, 4) << '\n';
    summary << "final_p=" << hexValue(cpu.registers().p, 2) << '\n';
    return result;
}

} // namespace snesquik::debug
