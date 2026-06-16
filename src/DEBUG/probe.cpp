#include "DEBUG/probe.h"

#include "BUS/bus.h"
#include "CART/rom_parser.h"
#include "CPU_R5A22/core.h"
#include "S-PPU/ppu.h"
#include "STATE/savestate.h"

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

    void setEnabled(bool value) { enabled = value; }

    void mmioRead(uint16_t address, uint8_t value) override
    {
        if (!shouldLog()) {
            return;
        }
        if (address == 0x4016 || address == 0x4017 || address >= 0x4200 || address < 0x2144) {
            log << "MMIO R " << hexValue(address, 4) << " = " << hexValue(value, 2) << '\n';
            ++linesWritten;
        }
    }

    void mmioWrite(uint16_t address, uint8_t value) override
    {
        if (!shouldLog()) {
            return;
        }
        if (address == 0x4016 || address == 0x4017 || address >= 0x4200 || address < 0x2144) {
            log << "MMIO W " << hexValue(address, 4) << " = " << hexValue(value, 2) << '\n';
            ++linesWritten;
        }
    }

    void dmaStart(uint8_t channel, uint16_t size) override
    {
        if (!shouldLog()) {
            return;
        }
        ++linesWritten;
        log << "DMA channel=" << static_cast<unsigned>(channel) << " size=" << (size == 0 ? 65536 : size) << '\n';
    }

    void vblank(bool active) override
    {
        if (!shouldLog()) {
            return;
        }
        ++linesWritten;
        log << "VBLANK " << (active ? "begin" : "end") << '\n';
    }

    void hdmaWrite(uint8_t channel, uint16_t address, uint8_t value) override
{
    if (!log.is_open() || !shouldLog()) {
        return;
    }
    ++linesWritten;

    log << "HDMA"
        << " ch=" << static_cast<int>(channel)
        << " addr=$" << std::hex << address
        << " value=$" << static_cast<int>(value)
        << std::dec << '\n';
}


void dmaTransfer(
    uint8_t channel,
    uint32_t sourceAddress,
    uint8_t bbad,
    uint8_t value,
    uint16_t vramAddress) override
{
    if (!log.is_open() || !shouldLog()) {
        return;
    }
    ++linesWritten;

    log << "DMA-XFER"
        << " ch=" << std::dec << static_cast<int>(channel)
        << " src=" << hexValue(sourceAddress, 6)
        << " bbad=$" << std::hex << static_cast<int>(bbad)
        << " value=$" << static_cast<int>(value)
        << " vmadd=$" << vramAddress
        << std::dec << '\n';
}

private:
    bool shouldLog()
    {
        if (!enabled) {
            return false;
        }
        if (linesWritten >= maxLogLines) {
            if (!truncated) {
                truncated = true;
                log << "... mmio log truncated at " << maxLogLines << " lines ...\n";
            }
            return false;
        }
        return true;
    }

    // Long probe runs can otherwise produce multi-gigabyte logs.
    static constexpr uint64_t maxLogLines = 20'000'000;

    std::ofstream log;
    uint64_t linesWritten = 0;
    bool truncated = false;
    bool enabled = true;
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
    if (parsed->header.hasSuperFx()) {
        bus.attachGsu(parsed->header.superFxRamSizeBytes());
    }
    if (parsed->header.hasSa1()) {
        bus.attachSa1(parsed->header.sa1BwRamSizeBytes());
    }
    if (parsed->header.hasDsp()) {
        std::string dspPath;
        if (const char* env = std::getenv("SNESQUIK_DSP1_ROM")) {
            dspPath = env;
        } else {
            dspPath = "tests/roms/dsp1b.rom";
        }
        bus.attachDsp(dspPath, parsed->header.map);
    }
    if (parsed->header.hasSdd1()) {
        bus.attachSdd1();
    }
    bus.setTraceListener(&trace);
    bus.initApu();

    cpu_r5a22::CPU cpu(bus);
    bus.setCpu(cpu);
    cpu.reset();

    if (!options.loadStatePath.empty()) {
        std::string stateError;
        if (!state::load(options.loadStatePath, cpu, bus, ppu, &stateError)) {
            result.message = "failed to load state: " + stateError;
            return result;
        }
        summary << "loaded_state=" << options.loadStatePath << '\n';
    }

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
    uint32_t irqFires = 0;
    std::vector<int16_t> audioCapture;
    for (uint32_t frame = 0; frame < options.frames; ++frame) {
        irqFires = 0;
        if (options.dumpStateFrame != UINT32_MAX) {
            // With a dump frame set, restrict the bulky MMIO/DMA logging to
            // the window leading up to it.
            trace.setEnabled(frame + 2500 >= options.dumpStateFrame && frame <= options.dumpStateFrame);
        }
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
        if (frame == options.gsuTraceFrame && options.gsuTraceCount > 0) {
            static std::FILE* gsuTraceFile =
                std::fopen((std::filesystem::path(options.outputDirectory) / "gsu_trace.log").c_str(), "w");
            bus.getGsu().traceSink = gsuTraceFile;
            bus.getGsu().traceRemaining = options.gsuTraceCount;
        }
        if (frame == options.saveStateFrame && !options.saveStatePath.empty()) {
            std::string stateError;
            if (state::save(options.saveStatePath, cpu, bus, ppu, &stateError)) {
                summary << "saved_state=" << options.saveStatePath << " frame=" << frame << '\n';
            } else {
                summary << "state_save_failed=" << stateError << '\n';
            }
        }
        for (const WramPoke& poke : options.wramPokes) {
            if (poke.frame == frame) {
                bus.writeWram(poke.address, poke.value);
                summary << "poke_frame=" << frame << " addr=" << hexValue(poke.address, 6)
                        << " val=" << hexValue(poke.value, 2) << '\n';
            }
        }
        for (const JoypadEvent& event : options.joypadEvents) {
            if (event.frame != frame) {
                continue;
            }
            if (event.press) {
                injectedJoypadState |= event.buttons;
            } else {
                injectedJoypadState &= static_cast<uint16_t>(~event.buttons);
            }
            bus.setJoypadState(injectedJoypadState);
            summary << (event.press ? "pressed_frame=" : "released_frame=") << frame << '\n';
        }

        uint32_t dotsThisFrame = 0;
        bool nmiRequested = false;
        int nmiArmCountdown = -1; // delay NMI service so $4210 polls can latch
        uint32_t dotRemainder = 0;
        ppu.beginFrame();
        uint16_t lastLine = ppu.verticalCounter();
        bus.setVblank(false);
        bus.beginFrame();
        bus.runHdmaScanline();

        while (dotsThisFrame < frameDots && !cpu.stopped()) {
            const uint32_t cpuCycles = cpu.step();
            if (globalStep < options.traceSteps) {
                logCpuStep(cpuLog, globalStep, cpu);
            }
            ++globalStep;

            const uint32_t dotAccum = cpuCycles * 3 + dotRemainder;
            uint32_t ppuDots = dotAccum / 2;
            dotRemainder = dotAccum % 2;
            const uint32_t dmaDots = bus.consumeDmaDots();
            ppuDots += dmaDots;
            // The SPC keeps running while the CPU is halted for DMA (see
            // the main loop in snesquik.cpp). The CPU's own cycles are taken
            // from its counter inside stepApu; only the DMA halt time is extra.
            bus.stepApu(dmaDots * 2 / 3);
            bus.stepGsu(cpuCycles * 6 + dmaDots * 4);
            bus.stepSa1(cpuCycles * 6 + dmaDots * 4);
            bus.stepDsp(cpuCycles + dmaDots);

            const uint16_t prevH = ppu.horizontalCounter();
            const uint16_t prevV = ppu.verticalCounter();
            ppu.tick(ppuDots);
            dotsThisFrame += ppuDots;

            const bool irqBefore = bus.irqFlag();
            bus.checkIrqCrossing(prevH, prevV, ppu.horizontalCounter(), ppu.verticalCounter());
            if (!irqBefore && bus.irqFlag()) {
                ++irqFires;
            }
            cpu.setIrqLine(bus.irqFlag() || bus.gsuIrqPending() || bus.sa1IrqPending());

            if (ppu.verticalCounter() != lastLine) {
                const uint16_t completedLine = lastLine;
                lastLine = ppu.verticalCounter();
                if (completedLine < static_cast<uint16_t>(ppu.visibleHeight())) {
                    ppu.renderScanline(completedLine);
                }
                if (lastLine <= static_cast<uint16_t>(ppu.visibleHeight())) {
                    bus.runHdmaScanline();
                }
            }

            if (!nmiRequested && ppu.verticalCounter() >= static_cast<uint16_t>(ppu.visibleHeight())) {
                bus.setVblank(true);
                bus.beginJoypadAutoRead();
                // The NMI is recognised a few instructions after the vblank
                // flag is raised. Modelling that latency lets a main-loop
                // `LDA $4210` poll latch bit 7 before the NMI handler acks it
                // (e.g. Street Fighter Alpha 2's frame-sync loop).
                nmiArmCountdown = bus.nmiEnabled() ? 4 : -1;
                nmiRequested = true;
            }
            if (nmiArmCountdown > 0 && --nmiArmCountdown == 0) {
                if (bus.nmiEnabled()) {
                    cpu.requestNMI();
                }
                nmiArmCountdown = -1;
            }

            bus.tickJoypadAutoRead(cpuCycles);

            if (bus.nmiEdgePending() && bus.nmiEnabled()) {
                cpu.requestNMI();
                bus.clearNmiEdge();
            }
        }

        bus.endApuFrame();

        if (!options.dumpAudioPath.empty()) {
            const auto frameAudio = bus.getApu().frameSamples();
            audioCapture.insert(audioCapture.end(), frameAudio.begin(), frameAudio.end());
        }

        if (frame == options.dumpStateFrame) {
            std::ofstream vramOut(std::filesystem::path(options.outputDirectory) / "vram.bin", std::ios::binary);
            for (size_t i = 0; i < 32 * 1024; ++i) {
                const uint16_t w = ppu.vramWord(i);
                vramOut.put(static_cast<char>(w & 0xff));
                vramOut.put(static_cast<char>(w >> 8));
            }
            std::ofstream cgramOut(std::filesystem::path(options.outputDirectory) / "cgram.bin", std::ios::binary);
            for (size_t i = 0; i < 256; ++i) {
                const uint16_t w = ppu.cgramColor(i);
                cgramOut.put(static_cast<char>(w & 0xff));
                cgramOut.put(static_cast<char>(w >> 8));
            }
            std::ofstream oamOut(std::filesystem::path(options.outputDirectory) / "oam.bin", std::ios::binary);
            for (size_t i = 0; i < 544; ++i) {
                oamOut.put(static_cast<char>(ppu.oamByte(i)));
            }
            std::ofstream wramOut(std::filesystem::path(options.outputDirectory) / "wram.bin", std::ios::binary);
            for (uint32_t i = 0; i < 128 * 1024; ++i) {
                wramOut.put(static_cast<char>(bus.readWram(i)));
            }
            std::ofstream spcOut(std::filesystem::path(options.outputDirectory) / "spcram.bin", std::ios::binary);
            for (int i = 0; i < 64 * 1024; ++i) {
                spcOut.put(static_cast<char>(bus.getApu().debugRam(i) & 0xff));
            }
            if (bus.hasGsu()) {
                std::ofstream gsuOut(std::filesystem::path(options.outputDirectory) / "gsuram.bin", std::ios::binary);
                const size_t gn = bus.getGsu().ramSize();
                for (size_t i = 0; i < gn; ++i) {
                    gsuOut.put(static_cast<char>(bus.getGsu().readRam(static_cast<uint32_t>(i))));
                }
                auto& g = bus.getGsu();
                std::ofstream gr(std::filesystem::path(options.outputDirectory) / "gsu_regs.txt");
                gr << "scbr=" << hexValue(g.scbr, 2) << " scmrHt=" << static_cast<int>(g.scmrHt)
                   << " scmrMd=" << static_cast<int>(g.scmrMd) << " por=" << hexValue(g.por, 2)
                   << " ron=" << g.scmrRon << " ran=" << g.scmrRan << " clsr=" << g.clsr << '\n';
            }
            std::ofstream regsOut(std::filesystem::path(options.outputDirectory) / "ppu_regs.txt");
            for (uint16_t reg = 0x2100; reg <= 0x2133; ++reg) {
                regsOut << hexValue(reg, 4) << " = " << hexValue(ppu.readRegister(reg), 2) << '\n';
            }
            for (size_t bg = 1; bg <= 4; ++bg) {
                regsOut << "bg" << bg << "hofs = " << hexValue(ppu.bgHorizontalScroll(bg), 4)
                        << " bg" << bg << "vofs = " << hexValue(ppu.bgVerticalScroll(bg), 4) << '\n';
            }
        }

        if (options.snapshotEvery != 0 && frame % options.snapshotEvery == 0) {
            std::ostringstream name;
            name << "frame_" << std::setw(4) << std::setfill('0') << frame << ".png";
            savePng(std::filesystem::path(options.outputDirectory) / name.str(), ppu.framebuffer(), ppu::Ppu::screenWidth, ppu::Ppu::screenHeight);
            const auto& reg = cpu.registers();
            const auto& apl = bus.getApuPortLog();
            summary << "frame=" << frame
                    << " pc=" << hexValue(reg.pb, 2) << ':' << hexValue(reg.pc, 4)
                    << " p=" << hexValue(reg.p, 2)
                    << " apuR=" << apl.totalReads
                    << " apuW=" << apl.totalWrites
                    << " apuP0R=" << hexValue(apl.lastRead[0], 2)
                    << " apuP0W=" << hexValue(apl.lastWrite[0], 2)
                    << " spcPc=" << hexValue(static_cast<uint64_t>(bus.getApu().debugPc()) & 0xffff, 4)
                    << " spcErr=" << (bus.getApu().debugError() ? bus.getApu().debugError() : "-")
                    << " spcIn=" << hexValue(static_cast<uint64_t>(bus.getApu().debugInPort(0)), 2)
                    << ',' << hexValue(static_cast<uint64_t>(bus.getApu().debugInPort(1)), 2)
                    << ',' << hexValue(static_cast<uint64_t>(bus.getApu().debugInPort(2)), 2)
                    << ',' << hexValue(static_cast<uint64_t>(bus.getApu().debugInPort(3)), 2)
                    << " spcOut=" << hexValue(static_cast<uint64_t>(bus.getApu().debugOutPort(0)), 2)
                    << ',' << hexValue(static_cast<uint64_t>(bus.getApu().debugOutPort(1)), 2)
                    << ',' << hexValue(static_cast<uint64_t>(bus.getApu().debugOutPort(2)), 2)
                    << ',' << hexValue(static_cast<uint64_t>(bus.getApu().debugOutPort(3)), 2)
                    << " joy=" << hexValue(bus.readMmio(0x4218) | (bus.readMmio(0x4219) << 8), 4)
                    << " inidisp=" << hexValue(ppu.readRegister(0x2100), 2)
                    << " bgmode=" << hexValue(ppu.readRegister(0x2105), 2)
                    << " tm=" << hexValue(ppu.readRegister(0x212c), 2)
                    << " nmitimen=" << hexValue(bus.readMmio(0x4200), 2)
                    << " vtime=" << hexValue(bus.readMmio(0x4209) | (bus.readMmio(0x420a) << 8), 4)
                    << " htime=" << hexValue(bus.readMmio(0x4207) | (bus.readMmio(0x4208) << 8), 4)
                    << " irqstage=" << hexValue(bus.readWram(0xAB), 2)
                    << " gsu=" << (bus.getGsu().running() ? "GO" : "--")
                    << ' ' << hexValue(bus.getGsu().pbr, 2) << ':' << hexValue(bus.getGsu().reg(15), 4)
                    << " sfr=" << hexValue(bus.getGsu().sfr, 4)
                    << " scmr=" << (bus.getGsu().scmrRon ? "RON" : "ron") << (bus.getGsu().scmrRan ? "+RAN" : "+ran")
                    << " md=" << static_cast<int>(bus.getGsu().scmrMd)
                    << " stops=" << bus.getGsu().stopCount
                    << " gins=" << bus.getGsu().instructionCount
                    << " mb=" << hexValue(bus.readWram(0x070B) | (bus.readWram(0x070C) << 8), 4)
                    << ',' << hexValue(bus.readWram(0x070D) | (bus.readWram(0x070E) << 8), 4)
                    << ',' << hexValue(bus.readWram(0x070F) | (bus.readWram(0x0710) << 8), 4)
                    << " gsuirq=" << (bus.gsuIrqPending() ? 1 : 0)
                    << " irqFires=" << irqFires
                    << " cpuI=" << (cpu.flag(cpu_r5a22::InterruptDisable) ? 1 : 0)
                    << " w6A=" << hexValue(bus.readWram(0x6A), 2)
                    << " hdmaObj=" << hexValue(bus.readWram(0x18B0) | (bus.readWram(0x18B1) << 8), 4)
                    << " blend=" << hexValue(bus.readWram(0x1982) | (bus.readWram(0x1983) << 8), 4)
                    << '/' << hexValue(bus.readWram(0x1986) | (bus.readWram(0x1987) << 8), 4)
                    << " hdmaen=" << hexValue(bus.readMmio(0x420c), 2)
                    << " sa1=" << (bus.hasSa1() ? (bus.getSa1().running() ? "GO" : "--") : "no")
                    << ' ' << hexValue(bus.getSa1().cpu().registers().pb, 2)
                    << ':' << hexValue(bus.getSa1().cpu().registers().pc, 4)
                    << " sa1cyc=" << bus.getSa1().cpu().totalCycles()
                    << " sa1irq=" << (bus.sa1IrqPending() ? 1 : 0)
                    << " dsp=" << (bus.hasDsp() ? (bus.getDsp().loaded() ? "ld" : "--") : "no")
                    << " pc=" << hexValue(bus.getDsp().regs.pc, 4)
                    << " sr=" << hexValue(bus.getDsp().sr, 4)
                    << " dr=" << hexValue(bus.getDsp().dr, 4)
                    << '\n';
            bus.resetApuPortLog();
        }
    }

    if (!options.dumpAudioPath.empty()) {
        std::ofstream wav(options.dumpAudioPath, std::ios::binary);
        const uint32_t dataBytes = static_cast<uint32_t>(audioCapture.size() * sizeof(int16_t));
        const uint32_t sampleRate = 32000;
        const uint32_t byteRate = sampleRate * 4;
        auto w32 = [&](uint32_t v) { wav.write(reinterpret_cast<const char*>(&v), 4); };
        auto w16 = [&](uint16_t v) { wav.write(reinterpret_cast<const char*>(&v), 2); };
        wav.write("RIFF", 4); w32(36 + dataBytes); wav.write("WAVE", 4);
        wav.write("fmt ", 4); w32(16); w16(1); w16(2); w32(sampleRate); w32(byteRate); w16(4); w16(16);
        wav.write("data", 4); w32(dataBytes);
        wav.write(reinterpret_cast<const char*>(audioCapture.data()), dataBytes);
        summary << "audio_dump=" << options.dumpAudioPath
                << " samples=" << audioCapture.size() / 2 << '\n';
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
