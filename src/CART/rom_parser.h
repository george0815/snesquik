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

    bool checksumPairValid() const;
    size_t declaredRomSizeBytes() const;
    size_t declaredRamSizeBytes() const;
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
