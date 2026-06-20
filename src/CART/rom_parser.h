#pragma once

#include "BUS/bus.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace snesquik::cartridge {

struct RomHeader {
    std::string title;
    bus::CartridgeMap map = bus::CartridgeMap::LoROM;
    bool fastRom = false;
    uint8_t mapMode = 0;
    uint8_t mapModeByte = 0;
    uint8_t chipset = 0;
    uint8_t romSizeCode = 0;
    uint8_t ramSizeCode = 0;
    uint8_t countryCode = 0;
    uint8_t developerId = 0;
    uint8_t version = 0;
    uint16_t checksumComplement = 0;
    uint16_t checksum = 0;
    size_t headerOffset = 0;
    int confidence = 0;

    uint8_t expansionRamSizeCode = 0;

    bool checksumPairValid() const;
    size_t declaredRomSizeBytes() const;
    size_t declaredRamSizeBytes() const;
    bool hasSuperFx() const { return chipset >= 0x13 && chipset <= 0x1a; }
    size_t superFxRamSizeBytes() const
    {
        // Header $xFBD declares expansion RAM (1 << code KB); GSU carts
        // without a declaration get the GSU-2 default of 64 KB.
        if (expansionRamSizeCode == 0 || expansionRamSizeCode > 7) {
            return 64 * 1024;
        }
        return static_cast<size_t>(1024) << expansionRamSizeCode;
    }
    // SA-1: coprocessor type $3x (high nibble 3) with a coprocessor present
    // (chipset low nibble >= 3, i.e. ROM+coprocessor variants).
    bool hasSa1() const { return (chipset & 0xf0) == 0x30 && (chipset & 0x0f) >= 0x03; }
    // DSP (NEC uPD7725): coprocessor type $0x (high nibble 0) with a
    // coprocessor present (low nibble >= 3). Covers DSP-1/1A/1B (SMK $05,
    // Suzuka 8 Hours $03); DSP-2/3/4 share the encoding but need their dumps.
    bool hasDsp() const { return (chipset & 0xf0) == 0x00 && (chipset & 0x0f) >= 0x03; }
    // S-DD1 (graphics decompressor): coprocessor type $4x with a coprocessor
    // present (low nibble >= 3). Street Fighter Alpha 2 ($43), Star Ocean ($45).
    bool hasSdd1() const { return (chipset & 0xf0) == 0x40 && (chipset & 0x0f) >= 0x03; }
    // Battery-backed save RAM present. The ROM-type (chipset) low nibble encodes
    // it: 2 = ROM+RAM+Battery, 5 = ROM+Coprocessor+RAM+Battery, 6 = ROM+Co+Battery
    // (covers standard, DSP, S-DD1 and SA-1 battery carts). Super FX uses a
    // separate $13-$1A chipset encoding where the low nibble is not a +Battery
    // flag, so it's excluded here (its save RAM, if any, is GSU expansion RAM).
    bool hasBattery() const
    {
        if (hasSuperFx()) {
            return false;
        }
        const uint8_t kind = chipset & 0x0f;
        return kind == 0x02 || kind == 0x05 || kind == 0x06;
    }
    size_t sa1BwRamSizeBytes() const
    {
        // The SA-1 addresses up to 256 KB of BW-RAM through its linear and
        // projected views regardless of how much the cartridge battery-backs;
        // allocate the full space so the window/block mapping never wraps.
        return 256 * 1024;
    }
};

struct ParsedRom {
    std::vector<uint8_t> rom;
    RomHeader header;
    size_t copierHeaderSize = 0;
};

std::optional<RomHeader> parseRomHeaderCandidate(std::span<const uint8_t> rom, bus::CartridgeMap map);
std::optional<ParsedRom> parseRomImage(std::span<const uint8_t> fileBytes);
const char* cartridgeMapName(bus::CartridgeMap map);

} // namespace snesquik::cartridge
