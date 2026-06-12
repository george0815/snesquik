#pragma once

#include <string>

namespace snesquik::cpu_r5a22 {
class CPU;
}
namespace snesquik::bus {
class SnesBus;
}
namespace snesquik::ppu {
class Ppu;
}

namespace snesquik::state {

// Save/load the full machine state (CPU, PPU, bus/WRAM, APU, SRAM).
// States are tied to the exact build that wrote them; on mismatch load()
// fails and reports via *error.
bool save(const std::string& path,
          const cpu_r5a22::CPU& cpu,
          bus::SnesBus& bus,
          const ppu::Ppu& ppu,
          std::string* error = nullptr);

bool load(const std::string& path,
          cpu_r5a22::CPU& cpu,
          bus::SnesBus& bus,
          ppu::Ppu& ppu,
          std::string* error = nullptr);

} // namespace snesquik::state
