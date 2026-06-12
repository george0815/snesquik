#include "STATE/savestate.h"

#include "BUS/bus.h"
#include "CPU_R5A22/core.h"
#include "S-PPU/ppu.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace snesquik::state {

namespace {

constexpr uint32_t kMagic = 0x54535153; // "SQST"
constexpr uint32_t kVersion = 1;

void appendU32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 24));
}

bool readU32(const uint8_t*& pos, const uint8_t* end, uint32_t& value)
{
    if (end - pos < 4) {
        return false;
    }
    value = static_cast<uint32_t>(pos[0]) | (static_cast<uint32_t>(pos[1]) << 8)
          | (static_cast<uint32_t>(pos[2]) << 16) | (static_cast<uint32_t>(pos[3]) << 24);
    pos += 4;
    return true;
}

void fail(std::string* error, const char* message)
{
    if (error) {
        *error = message;
    }
}

} // namespace

bool save(const std::string& path,
          const cpu_r5a22::CPU& cpu,
          bus::SnesBus& bus,
          const ppu::Ppu& ppu,
          std::string* error)
{
    std::vector<uint8_t> blob;
    appendU32(blob, kMagic);
    appendU32(blob, kVersion);

    const cpu_r5a22::CPU::SaveState cpuState = cpu.saveState();
    appendU32(blob, sizeof(cpuState));
    const auto* cpuBytes = reinterpret_cast<const uint8_t*>(&cpuState);
    blob.insert(blob.end(), cpuBytes, cpuBytes + sizeof(cpuState));

    std::vector<uint8_t> section;
    ppu.saveState(section);
    appendU32(blob, static_cast<uint32_t>(section.size()));
    blob.insert(blob.end(), section.begin(), section.end());

    section.clear();
    bus.saveState(section);
    appendU32(blob, static_cast<uint32_t>(section.size()));
    blob.insert(blob.end(), section.begin(), section.end());

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        fail(error, "could not open state file for writing");
        return false;
    }
    out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    if (!out) {
        fail(error, "failed to write state file");
        return false;
    }
    return true;
}

bool load(const std::string& path,
          cpu_r5a22::CPU& cpu,
          bus::SnesBus& bus,
          ppu::Ppu& ppu,
          std::string* error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail(error, "could not open state file");
        return false;
    }
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const uint8_t* pos = blob.data();
    const uint8_t* end = pos + blob.size();

    uint32_t magic = 0;
    uint32_t version = 0;
    if (!readU32(pos, end, magic) || magic != kMagic) {
        fail(error, "not a snesquik state file");
        return false;
    }
    if (!readU32(pos, end, version) || version != kVersion) {
        fail(error, "state file version mismatch");
        return false;
    }

    uint32_t cpuSize = 0;
    if (!readU32(pos, end, cpuSize) || cpuSize != sizeof(cpu_r5a22::CPU::SaveState)
        || static_cast<size_t>(end - pos) < cpuSize) {
        fail(error, "state file CPU section mismatch (different build?)");
        return false;
    }
    cpu_r5a22::CPU::SaveState cpuState;
    std::memcpy(&cpuState, pos, sizeof(cpuState));
    pos += cpuSize;

    uint32_t ppuSize = 0;
    if (!readU32(pos, end, ppuSize) || static_cast<size_t>(end - pos) < ppuSize) {
        fail(error, "state file truncated in PPU section");
        return false;
    }
    const uint8_t* ppuData = pos;
    pos += ppuSize;

    uint32_t busSize = 0;
    if (!readU32(pos, end, busSize) || static_cast<size_t>(end - pos) < busSize) {
        fail(error, "state file truncated in bus section");
        return false;
    }
    const uint8_t* busData = pos;
    pos += busSize;

    if (!ppu.loadState(ppuData, ppuSize)) {
        fail(error, "state file PPU section mismatch (different build?)");
        return false;
    }
    if (!bus.loadState(busData, busSize)) {
        fail(error, "state file bus section mismatch (different build?)");
        return false;
    }
    cpu.loadState(cpuState);
    return true;
}

} // namespace snesquik::state
