#include "BUS/bus.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace snesquik::bus;

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

std::vector<uint8_t> makeRom(size_t size)
{
    std::vector<uint8_t> rom(size);
    for (size_t i = 0; i < rom.size(); ++i) {
        rom[i] = static_cast<uint8_t>((i * 17 + 3) & 0xff);
    }
    return rom;
}

void testHeaderOffsets()
{
    requireEq(*CartridgeRom::headerOffset(CartridgeMap::LoROM, 0x8000), 0x7fc0, "LoROM header file offset");
    requireEq(*CartridgeRom::headerOffset(CartridgeMap::HiROM, 0x10000), 0xffc0, "HiROM header file offset");
    require(!CartridgeRom::headerOffset(CartridgeMap::ExHiROM, 0x400000), "ExHiROM header rejects short ROM");
    requireEq(*CartridgeRom::headerOffset(CartridgeMap::ExHiROM, 0x410000), 0x40ffc0, "ExHiROM header file offset");
}

void testLoRomHeaderCpuMapping()
{
    auto rom = makeRom(0x10000);
    rom[0x7fc0] = 0x42;

    SnesBus bus;
    bus.attachCartridge(std::move(rom), CartridgeMap::LoROM);

    requireEq(bus.read8(CartridgeRom::headerCpuAddress), 0x42, "LoROM maps ROM header to CPU $00FFC0");
    requireEq(bus.cartridge().mapCpuAddress(CartridgeRom::headerCpuAddress).value(), 0x7fc0, "LoROM CPU header maps to file offset");
}

void testHiRomHeaderCpuMapping()
{
    auto rom = makeRom(0x20000);
    rom[0xffc0] = 0x84;

    SnesBus bus;
    bus.attachCartridge(std::move(rom), CartridgeMap::HiROM);

    requireEq(bus.read8(CartridgeRom::headerCpuAddress), 0x84, "HiROM maps ROM header to CPU $00FFC0");
    requireEq(bus.cartridge().mapCpuAddress(CartridgeRom::headerCpuAddress).value(), 0xffc0, "HiROM CPU header maps to file offset");
}

void testLoRomSramMapping()
{
    auto rom = makeRom(0x8000);

    SnesBus bus;
    bus.attachCartridge(std::move(rom), CartridgeMap::LoROM, 0x2000);

    bus.write8(0x706123, 0x5a);
    requireEq(bus.read8(0x706123), 0x5a, "LoROM maps bank $70 SRAM");
    requireEq(bus.read8(0x006123), 0x5a, "LoROM mirrors small SRAM at $00:6000-$7FFF");

    bus.write8(0x806124, 0xa5);
    requireEq(bus.read8(0x706124), 0xa5, "LoROM SRAM writes through bank $80 mirror");
}

void testExHiRomHeaderCpuMapping()
{
    auto rom = makeRom(0x410000);
    rom[0x40ffc0] = 0xc5;

    SnesBus bus;
    bus.attachCartridge(std::move(rom), CartridgeMap::ExHiROM);

    requireEq(bus.read8(CartridgeRom::headerCpuAddress), 0xc5, "ExHiROM maps ROM header to CPU $00FFC0");
    requireEq(bus.cartridge().mapCpuAddress(CartridgeRom::headerCpuAddress).value(), 0x40ffc0, "ExHiROM CPU header maps to file offset");
}

void testWramMappingAndMirrors()
{
    SnesBus bus;

    bus.write8(0x7e1234, 0xaa);
    requireEq(bus.read8(0x7e1234), 0xaa, "WRAM bank $7E read/write");
    requireEq(bus.readWram(0x1234), 0xaa, "WRAM direct storage");

    bus.write8(0x000012, 0x55);
    requireEq(bus.read8(0x800012), 0x55, "first 8 KiB WRAM mirrors into banks $80-$BF");
    requireEq(bus.read8(0x7e0012), 0x55, "first 8 KiB mirror writes underlying WRAM");
}

void testMmioAndOpenBus()
{
    SnesBus bus;

    bus.write8(0x002100, 0x31);
    requireEq(bus.read8(0x002100), 0x31, "MMIO register placeholder read/write");
    requireEq(bus.read8(0x802100), 0x31, "MMIO mirrors into banks $80-$BF");

    bus.write8(0x006000, 0x77);
    requireEq(bus.read8(0x006000), 0x77, "unmapped write updates open bus");
}

void testCpuCanResetThroughMappedCartridge()
{
    auto rom = makeRom(0x8000);
    rom[0x7ffc] = 0x34;
    rom[0x7ffd] = 0x12;

    SnesBus bus;
    bus.attachCartridge(std::move(rom), CartridgeMap::LoROM);

    snesquik::cpu_r5a22::CPU cpu(bus);
    cpu.reset();
    requireEq(cpu.registers().pc, 0x1234, "CPU reset vector is read through LoROM bus mapping");
}

void testVblankAndNmiRegisters()
{
    SnesBus bus;

    bus.write8(0x004200, 0x80);
    require(bus.nmiEnabled(), "$4200 enables NMI");

    bus.setVblank(true);
    requireEq(bus.read8(0x004212), 0x80, "$4212 reports VBlank active");
    requireEq(bus.read8(0x004210) & 0x80, 0x80, "$4210 reports NMI flag");
    requireEq(bus.read8(0x004210) & 0x80, 0x00, "$4210 read clears NMI flag");
    requireEq(bus.read8(0x004212), 0x80, "$4210 read does not clear VBlank status");
    bus.setVblank(false);
    requireEq(bus.read8(0x004212), 0x00, "$4212 clears when VBlank ends");
}

void testManualJoypadSerialRead()
{
    SnesBus bus;
    bus.setJoypadState(JoypadB | JoypadA | JoypadR);

    bus.write8(0x004016, 0x01);
    bus.write8(0x004016, 0x00);

    std::vector<uint8_t> bits;
    for (int i = 0; i < 12; ++i) {
        bits.push_back(static_cast<uint8_t>(bus.read8(0x004016) & 0x01));
    }

    requireEq(bits[0], 1, "$4016 serial read returns B first");
    requireEq(bits[1], 0, "$4016 serial read returns Y second");
    requireEq(bits[8], 1, "$4016 serial read returns A ninth");
    requireEq(bits[11], 1, "$4016 serial read returns R twelfth");
    requireEq(bus.read8(0x004017) & 0x01, 0, "$4017 second controller defaults unpressed");
}

void testJoypadAutoReadRegisters()
{
    SnesBus bus;
    bus.write8(0x004200, 0x01);
    require(bus.joypadAutoReadEnabled(), "$4200 bit 0 enables joypad auto-read");

    bus.setJoypadState(JoypadA | JoypadStart);
    bus.beginJoypadAutoRead();
    requireEq(bus.read8(0x004212) & 0x01, 0x01, "$4212 bit 0 reports joypad auto-read busy");
    bus.finishJoypadAutoRead();

    requireEq(bus.read8(0x004212) & 0x01, 0x00, "$4212 bit 0 clears when auto-read finishes");
    requireEq(bus.read8(0x004218), 0x80, "$4218 reports low joypad byte with A on bit 7");
    requireEq(bus.read8(0x004219), 0x10, "$4219 reports high joypad byte with Start on bit 4");
}

void testApuIplReadySignature()
{
    SnesBus bus;

    requireEq(bus.read8(0x002140), 0xaa, "APU port 0 reports IPL ready low byte");
    requireEq(bus.read8(0x002141), 0xbb, "APU port 1 reports IPL ready high byte");
}

void testApuIplPort0Acknowledgement()
{
    SnesBus bus;

    bus.write8(0x002140, 0xff);
    requireEq(bus.read8(0x002140), 0xaa, "APU IPL ignores port 0 preamble before transfer start");

    bus.write8(0x002142, 0x00);
    bus.write8(0x002143, 0x02);
    bus.write8(0x002141, 0x01);
    bus.write8(0x002140, 0xcc);
    requireEq(bus.read8(0x002140), 0xcc, "APU IPL acknowledges transfer start on port 0");

    bus.write8(0x002141, 0x9a);
    bus.write8(0x002140, 0x00);
    requireEq(bus.read8(0x002140), 0x00, "APU IPL acknowledges first data byte counter");
    bus.write8(0x002141, 0xbc);
    bus.write8(0x002140, 0x01);
    requireEq(bus.read8(0x002140), 0x01, "APU IPL acknowledges incrementing data byte counter");

    bus.write8(0x002140, 0x00);
    bus.write8(0x002141, 0x00);
    bus.write8(0x002142, 0x00);
    bus.write8(0x002143, 0x00);
    requireEq(bus.read8(0x002140), 0xaa, "APU IPL returns to ready signature after CPU clears ports");
    requireEq(bus.read8(0x002141), 0xbb, "APU IPL restores ready high byte after CPU clears ports");
}

void configureHdmaChannel0(SnesBus& bus, uint8_t dmap, uint8_t bbad, uint16_t table, uint8_t bank)
{
    bus.write8(0x004300, dmap);
    bus.write8(0x004301, bbad);
    bus.write8(0x004302, static_cast<uint8_t>(table));
    bus.write8(0x004303, static_cast<uint8_t>(table >> 8));
    bus.write8(0x004304, bank);
    bus.write8(0x00420c, 0x01);
}

void testHdmaDirectTableReloadsAndTransfersPerLine()
{
    SnesBus bus;
    configureHdmaChannel0(bus, 0x00, 0x00, 0x1000, 0x7e);
    bus.writeWram(0x1000, 0x02);
    bus.writeWram(0x1001, 0x0f);
    bus.writeWram(0x1002, 0x8f);
    bus.writeWram(0x1003, 0x00);

    bus.beginFrame();
    bus.runHdmaScanline();
    requireEq(bus.readMmio(0x2100), 0x0f, "HDMA direct first line writes B-bus register");
    bus.runHdmaScanline();
    requireEq(bus.readMmio(0x2100), 0x8f, "HDMA direct second line advances table data");
    bus.runHdmaScanline();
    requireEq(bus.readMmio(0x2100), 0x8f, "HDMA terminator stops channel");
}

void testHdmaRepeatLineDoesNotRetransfer()
{
    SnesBus bus;
    configureHdmaChannel0(bus, 0x00, 0x00, 0x1100, 0x7e);
    bus.writeWram(0x1100, 0x82);
    bus.writeWram(0x1101, 0x0f);
    bus.writeWram(0x1102, 0x00);

    bus.beginFrame();
    bus.runHdmaScanline();
    requireEq(bus.readMmio(0x2100), 0x0f, "HDMA repeat line transfers first line");
    bus.writeMmio(0x2100, 0x44);
    bus.runHdmaScanline();
    requireEq(bus.readMmio(0x2100), 0x44, "HDMA repeat line suppresses later transfers");
}

void testHdmaIndirectReloadsSourcePointer()
{
    SnesBus bus;
    configureHdmaChannel0(bus, 0x40, 0x00, 0x1200, 0x7e);
    bus.write8(0x004307, 0x7f);
    bus.writeWram(0x1200, 0x02);
    bus.writeWram(0x1201, 0x00);
    bus.writeWram(0x1202, 0x20);
    bus.writeWram(0x1203, 0x00);
    bus.writeWram(0x12000, 0x0f);
    bus.writeWram(0x12001, 0x8f);

    bus.beginFrame();
    bus.runHdmaScanline();
    requireEq(bus.readMmio(0x2100), 0x0f, "HDMA indirect first line uses indirect bank/source");
    bus.runHdmaScanline();
    requireEq(bus.readMmio(0x2100), 0x8f, "HDMA indirect second line advances indirect source");
}

void testHdmaMode1WritesTwoBbusRegisters()
{
    SnesBus bus;
    configureHdmaChannel0(bus, 0x01, 0x0d, 0x1300, 0x7e);
    bus.writeWram(0x1300, 0x01);
    bus.writeWram(0x1301, 0x34);
    bus.writeWram(0x1302, 0x12);
    bus.writeWram(0x1303, 0x00);

    bus.beginFrame();
    bus.runHdmaScanline();
    requireEq(bus.readMmio(0x210d), 0x34, "HDMA mode 1 writes BBAD");
    requireEq(bus.readMmio(0x210e), 0x12, "HDMA mode 1 writes BBAD+1");
}

} // namespace

int main()
{
    try {
        run("header file offsets", testHeaderOffsets);
        run("LoROM header CPU mapping", testLoRomHeaderCpuMapping);
        run("HiROM header CPU mapping", testHiRomHeaderCpuMapping);
        run("LoROM SRAM mapping", testLoRomSramMapping);
        run("ExHiROM header CPU mapping", testExHiRomHeaderCpuMapping);
        run("WRAM mapping and mirrors", testWramMappingAndMirrors);
        run("MMIO placeholders and open bus", testMmioAndOpenBus);
        run("CPU reset through mapped cartridge", testCpuCanResetThroughMappedCartridge);
        run("VBlank and NMI registers", testVblankAndNmiRegisters);
        run("manual joypad serial read", testManualJoypadSerialRead);
        run("joypad auto-read registers", testJoypadAutoReadRegisters);
        run("APU IPL ready signature", testApuIplReadySignature);
        run("APU IPL port 0 acknowledgement", testApuIplPort0Acknowledgement);
        run("HDMA direct table reloads and transfers per line", testHdmaDirectTableReloadsAndTransfersPerLine);
        run("HDMA repeat line suppresses later transfers", testHdmaRepeatLineDoesNotRetransfer);
        run("HDMA indirect reloads source pointer", testHdmaIndirectReloadsSourcePointer);
        run("HDMA mode 1 writes two B-bus registers", testHdmaMode1WritesTwoBbusRegisters);
    } catch (const std::exception& error) {
        std::cerr << "[fail] " << error.what() << '\n';
        return 1;
    }
}
