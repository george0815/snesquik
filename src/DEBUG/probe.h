#pragma once

#include <cstdint>
#include <string>

namespace snesquik::debug {

struct ProbeOptions {
    std::string romPath;
    std::string outputDirectory = "probe-out";
    uint32_t frames = 60;
    uint32_t traceSteps = 4096;
    uint32_t snapshotEvery = 1;
    uint32_t pressFrame = UINT32_MAX;
    uint32_t releaseFrame = UINT32_MAX;
    uint16_t injectedButtons = 0;
};

struct ProbeResult {
    bool ok = false;
    std::string message;
    uint16_t resetPc = 0;
    uint64_t cpuCycles = 0;
};

ProbeResult runProbe(const ProbeOptions& options);

} // namespace snesquik::debug
