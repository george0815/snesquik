#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace snesquik::debug {

struct JoypadEvent {
    uint32_t frame = 0;
    uint16_t buttons = 0;
    bool press = true;
};

struct WramPoke {
    uint32_t frame = 0;
    uint32_t address = 0;
    uint8_t value = 0;
};

struct ProbeOptions {
    std::string romPath;
    std::string outputDirectory = "probe-out";
    uint32_t frames = 60;
    uint32_t traceSteps = 4096;
    uint32_t snapshotEvery = 1;
    uint32_t pressFrame = UINT32_MAX;
    uint32_t releaseFrame = UINT32_MAX;
    uint16_t injectedButtons = 0;
    std::vector<JoypadEvent> joypadEvents;
    std::vector<WramPoke> wramPokes;
    uint32_t dumpStateFrame = UINT32_MAX;
    uint32_t gsuTraceFrame = UINT32_MAX;
    uint32_t gsuTraceCount = 0;
    std::string loadStatePath;
    std::string saveStatePath;
    uint32_t saveStateFrame = UINT32_MAX;
    std::string dumpAudioPath;
};

struct ProbeResult {
    bool ok = false;
    std::string message;
    uint16_t resetPc = 0;
    uint64_t cpuCycles = 0;
};

ProbeResult runProbe(const ProbeOptions& options);

} // namespace snesquik::debug
