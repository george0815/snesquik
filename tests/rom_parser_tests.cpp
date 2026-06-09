#include "CART/rom_parser.h"

#include "CPU_R5A22/core.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

void requireEq(const std::string& actual, const std::string& expected, const std::string& message)
{
    if (actual != expected) {
        throw std::runtime_error(message + ": got '" + actual + "', expected '" + expected + "'");
    }
}

void run(const char* name, void (*test)())
{
    test();
    std::cout << "[pass] " << name << '\n';
}

std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    require(file.good(), "fixture file could not be opened");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void writeLe16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

std::vector<uint8_t> makeLoRomFixture()
{
    std::vector<uint8_t> rom(0x8000);
    const size_t header = 0x7fc0;
    const std::string title = "UNIT TEST ROM        ";
    for (size_t i = 0; i < title.size(); ++i) {
        rom[header + i] = static_cast<uint8_t>(title[i]);
    }
    rom[header + 0x15] = 0x30;
    rom[header + 0x16] = 0x00;
    rom[header + 0x17] = 0x05;
    rom[header + 0x18] = 0x00;
    rom[header + 0x19] = 0x01;
    rom[header + 0x1a] = 0x33;
    rom[header + 0x1b] = 0x02;
    writeLe16(rom, header + 0x1c, 0xabcd);
    writeLe16(rom, header + 0x1e, 0x5432);
    writeLe16(rom, header + 0x3c, 0x8000);
    return rom;
}

void testSyntheticLoRomHeader()
{
    auto rom = makeLoRomFixture();
    const auto parsed = cartridge::parseRomImage(rom);
    require(parsed.has_value(), "synthetic LoROM parses");
    requireEq(parsed->copierHeaderSize, 0, "no copier header detected");
    requireEq(parsed->header.title, "UNIT TEST ROM", "title is trimmed");
    require(parsed->header.map == bus::CartridgeMap::LoROM, "LoROM map is detected");
    require(parsed->header.fastRom, "fast ROM bit is decoded");
    requireEq(parsed->header.headerOffset, 0x7fc0, "LoROM header offset");
    requireEq(parsed->header.mapModeByte, 0x30, "map mode byte");
    requireEq(parsed->header.romSizeCode, 0x05, "ROM size code");
    requireEq(static_cast<uint32_t>(parsed->header.declaredRomSizeBytes()), 32768, "declared ROM byte size");
    require(parsed->header.checksumPairValid(), "checksum pair validates");
}

void testCopierHeaderIsStripped()
{
    auto rom = makeLoRomFixture();
    std::vector<uint8_t> file(512, 0);
    file.insert(file.end(), rom.begin(), rom.end());

    const auto parsed = cartridge::parseRomImage(file);
    require(parsed.has_value(), "copier-header LoROM parses");
    requireEq(parsed->copierHeaderSize, 512, "copier header size");
    requireEq(parsed->rom.size(), rom.size(), "normalized ROM size");
    requireEq(parsed->header.headerOffset, 0x7fc0, "header offset after copier strip");
}

void testRealCpuTestRom(const char* path)
{
    const auto file = readFile(path);
    const auto parsed = cartridge::parseRomImage(file);
    require(parsed.has_value(), "cputest ROM parses");
    requireEq(parsed->header.title, "65C816 TEST", "cputest title");
    require(parsed->header.map == bus::CartridgeMap::LoROM, "cputest is LoROM");
    require(parsed->header.fastRom, "cputest is fast ROM");
    requireEq(parsed->header.mapModeByte, 0x30, "cputest map byte");
    requireEq(parsed->header.chipset, 0x00, "cputest chipset");
    requireEq(parsed->header.romSizeCode, 0x08, "cputest ROM size code");
    requireEq(static_cast<uint32_t>(parsed->header.declaredRomSizeBytes()), 262144, "cputest declared ROM size");
    requireEq(parsed->header.ramSizeCode, 0x00, "cputest RAM size code");
    require(parsed->header.checksumPairValid(), "cputest checksum pair validates");

    bus::SnesBus snesBus;
    snesBus.attachCartridge(parsed->rom, parsed->header.map);
    requireEq(snesBus.read8(bus::CartridgeRom::headerCpuAddress), '6', "parsed ROM header is visible through bus");

    cpu_r5a22::CPU cpu(snesBus);
    cpu.reset();
    requireEq(cpu.registers().pc, 0x8000, "cputest reset vector through parsed bus");
}

void testRealCpuBasicRom()
{
    testRealCpuTestRom("tests/snes-tests/cputest/cputest-basic.sfc");
}

void testRealCpuFullRom()
{
    testRealCpuTestRom("tests/snes-tests/cputest/cputest-full.sfc");
}

void testRejectsMissingHeader()
{
    std::vector<uint8_t> rom(0x8000);
    require(!cartridge::parseRomImage(rom), "empty ROM without printable header is rejected");
}

} // namespace

int main()
{
    try {
        run("synthetic LoROM header", testSyntheticLoRomHeader);
        run("copier header stripping", testCopierHeaderIsStripped);
        run("real CPU basic test ROM", testRealCpuBasicRom);
        run("real CPU full test ROM", testRealCpuFullRom);
        run("missing header rejection", testRejectsMissingHeader);
    } catch (const std::exception& error) {
        std::cerr << "[fail] " << error.what() << '\n';
        return 1;
    }
}
