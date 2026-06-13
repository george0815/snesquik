#include "CART/rom_parser.h"

#include <algorithm>
#include <array>

namespace snesquik::cartridge {

namespace {

constexpr size_t internalHeaderSize = 0x20;
constexpr size_t vectorsSize = 0x20;

uint16_t readLe16(std::span<const uint8_t> bytes, size_t offset)
{
    return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::string readTitle(std::span<const uint8_t> bytes, size_t offset)
{
    std::string title;
    title.reserve(21);
    for (size_t i = 0; i < 21; ++i) {
        const char c = static_cast<char>(bytes[offset + i]);
        title.push_back(c);
    }

    while (!title.empty() && title.back() == ' ') {
        title.pop_back();
    }
    return title;
}

bool isPrintableTitle(std::span<const uint8_t> bytes, size_t offset)
{
    bool hasNonSpace = false;
    for (size_t i = 0; i < 21; ++i) {
        const uint8_t c = bytes[offset + i];
        if (c < 0x20 || c > 0x7e) {
            return false;
        }
        hasNonSpace = hasNonSpace || c != 0x20;
    }
    return hasNonSpace;
}

uint8_t mapNibble(bus::CartridgeMap map)
{
    switch (map) {
    case bus::CartridgeMap::LoROM:
        return 0x0;
    case bus::CartridgeMap::HiROM:
        return 0x1;
    case bus::CartridgeMap::ExHiROM:
        return 0x5;
    }
    return 0xff;
}

std::optional<bus::CartridgeMap> mapFromHeaderByte(uint8_t value)
{
    switch (value & 0x0f) {
    case 0x0:
        return bus::CartridgeMap::LoROM;
    case 0x1:
        return bus::CartridgeMap::HiROM;
    case 0x5:
        return bus::CartridgeMap::ExHiROM;
    default:
        return std::nullopt;
    }
}

bool hasPlausibleVector(std::span<const uint8_t> bytes, size_t headerOffset)
{
    if (headerOffset + internalHeaderSize + vectorsSize > bytes.size()) {
        return false;
    }

    const std::array<size_t, 4> vectorOffsets = {
        headerOffset + 0x2a, // native NMI
        headerOffset + 0x2e, // native IRQ
        headerOffset + 0x3c, // emulation RESET
        headerOffset + 0x3e, // emulation IRQ/BRK
    };

    return std::any_of(vectorOffsets.begin(), vectorOffsets.end(), [&](size_t offset) {
        const uint16_t value = readLe16(bytes, offset);
        return value != 0x0000 && value != 0xffff;
    });
}

int scoreHeader(const RomHeader& header, std::span<const uint8_t> rom)
{
    int score = 0;
    if (!header.title.empty()) {
        score += 4;
    }
    if (header.checksumPairValid()) {
        score += 6;
    }
    if (header.mapMode == mapNibble(header.map)) {
        score += 5;
    }
    if ((header.mapModeByte & 0xe0) == 0x20 || (header.mapModeByte & 0xe0) == 0x30) {
        score += 2;
    }
    if (header.romSizeCode >= 5 && header.romSizeCode <= 13) {
        score += 2;
    }
    if (header.ramSizeCode <= 10) {
        score += 1;
    }
    if (header.declaredRomSizeBytes() >= rom.size() / 2 && header.declaredRomSizeBytes() <= rom.size() * 2) {
        score += 2;
    }
    if (hasPlausibleVector(rom, header.headerOffset)) {
        score += 4;
    }
    return score;
}

size_t copierHeaderSize(std::span<const uint8_t> fileBytes)
{
    if (fileBytes.size() > 512 && (fileBytes.size() % 0x8000) == 512) {
        return 512;
    }
    return 0;
}

} // namespace

bool RomHeader::checksumPairValid() const
{
    return static_cast<uint16_t>(checksum ^ checksumComplement) == 0xffff;
}

size_t RomHeader::declaredRomSizeBytes() const
{
    if (romSizeCode >= sizeof(size_t) * 8) {
        return 0;
    }
    return static_cast<size_t>(1024) << romSizeCode;
}

size_t RomHeader::declaredRamSizeBytes() const
{
    if (ramSizeCode == 0 || ramSizeCode >= sizeof(size_t) * 8) {
        return 0;
    }
    return static_cast<size_t>(1024) << ramSizeCode;
}

std::optional<RomHeader> parseRomHeaderCandidate(std::span<const uint8_t> rom, bus::CartridgeMap map)
{
    const auto headerOffset = bus::CartridgeRom::headerOffset(map, rom.size());
    if (!headerOffset || *headerOffset + internalHeaderSize > rom.size()) {
        return std::nullopt;
    }

    if (!isPrintableTitle(rom, *headerOffset)) {
        return std::nullopt;
    }

    RomHeader header;
    header.title = readTitle(rom, *headerOffset);
    header.map = map;
    header.mapModeByte = rom[*headerOffset + 0x15];
    header.mapMode = header.mapModeByte & 0x0f;
    header.fastRom = (header.mapModeByte & 0x10) != 0;
    header.chipset = rom[*headerOffset + 0x16];
    header.romSizeCode = rom[*headerOffset + 0x17];
    header.ramSizeCode = rom[*headerOffset + 0x18];
    header.countryCode = rom[*headerOffset + 0x19];
    header.developerId = rom[*headerOffset + 0x1a];
    header.version = rom[*headerOffset + 0x1b];
    header.checksumComplement = readLe16(rom, *headerOffset + 0x1c);
    header.checksum = readLe16(rom, *headerOffset + 0x1e);
    header.headerOffset = *headerOffset;
    if (*headerOffset >= 3) {
        header.expansionRamSizeCode = rom[*headerOffset - 3];
    }

    const auto mapFromHeader = mapFromHeaderByte(header.mapModeByte);
    if (mapFromHeader && *mapFromHeader == map) {
        header.confidence += 5;
    }
    header.confidence += scoreHeader(header, rom);
    return header;
}

std::optional<ParsedRom> parseRomImage(std::span<const uint8_t> fileBytes)
{
    const size_t skip = copierHeaderSize(fileBytes);
    if (fileBytes.size() <= skip) {
        return std::nullopt;
    }

    std::span<const uint8_t> romBytes = fileBytes.subspan(skip);
    std::optional<RomHeader> best;
    for (bus::CartridgeMap map : {bus::CartridgeMap::LoROM, bus::CartridgeMap::HiROM, bus::CartridgeMap::ExHiROM}) {
        auto candidate = parseRomHeaderCandidate(romBytes, map);
        if (!candidate) {
            continue;
        }
        if (!best || candidate->confidence > best->confidence) {
            best = candidate;
        }
    }

    if (!best) {
        return std::nullopt;
    }

    ParsedRom parsed;
    parsed.rom.assign(romBytes.begin(), romBytes.end());
    parsed.header = *best;
    parsed.copierHeaderSize = skip;
    return parsed;
}

const char* cartridgeMapName(bus::CartridgeMap map)
{
    switch (map) {
    case bus::CartridgeMap::LoROM:
        return "LoROM";
    case bus::CartridgeMap::HiROM:
        return "HiROM";
    case bus::CartridgeMap::ExHiROM:
        return "ExHiROM";
    }
    return "Unknown";
}

} // namespace snesquik::cartridge
