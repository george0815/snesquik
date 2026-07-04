# snesquik Emulator Architecture Guide

This document is a detailed onboarding guide for snesquik, a Super Nintendo /
Super Famicom emulator written in C++20. It explains the original hardware, the
memory map, the major code modules, ownership relationships, runtime flow, the
timing model, save-state format, SRAM behavior, rendering path, audio path, the
enhancement-chip (coprocessor) implementations, and the important fields in the
main component classes.

The intent is practical: a new programmer should be able to read this document,
open the referenced source files, and understand how the emulator is wired
together well enough to debug or extend it.

## Mental Model

At the highest level, snesquik emulates a small computer with an unusually
crowded cartridge slot:

```text
Frontend (SDL/OpenGL)  or  Probe (headless)
  |
  v
main loop (owns and drives everything)
  |
  +-- CPU (65816 / Ricoh 5A22)
  +-- SnesBus
  |    +-- Cartridge ROM / SRAM (CartridgeRom)
  |    +-- 128 KiB WRAM
  |    +-- CPU MMIO ($2100-$5FFF): PPU ports, APU ports, WRAM port,
  |    |     NMI/IRQ timers, mul/div, joypads, DMA/HDMA registers
  |    +-- Apu (SPC700 + S-DSP, blargg snes_spc)
  |    +-- Gsu (Super FX)          [if cartridge has one]
  |    +-- Sa1 (SA-1, second 65816)[if cartridge has one]
  |    +-- NecDsp (DSP-1, uPD7725) [if cartridge has one]
  |    +-- Sdd1 (S-DD1 decompressor)[if cartridge has one]
  |
  +-- Ppu (S-PPU software renderer)
  +-- savestate module
```

There is no top-level `Emulator` class. The composition root is
`main()` in `src/MAIN/snesquik.cpp` (and, in parallel, `runProbe()` in
`src/DEBUG/probe.cpp`): each constructs a `Ppu`, a `SnesBus`, a `CPU`, wires
them together, and runs the frame loop inline. **These two frame loops are
intentionally duplicated and must be kept in sync** — see
[Dual Frame Loops](#dual-frame-loops).

The central rule is:

```text
Only emulated devices understand emulated hardware.
Frontends only present video/audio/input.
```

The SDL frontend uses OpenGL strictly to display the CPU-rendered framebuffer.
The PPU renders on the CPU, per scanline, into a 32-bit RGBA buffer.

## The Super Nintendo Hardware

The SNES is built around a Ricoh 5A22 (a 65C816 core plus system glue), a
two-chip picture processor (exposed to software as one "S-PPU"), an audio
subsystem consisting of a Sony SPC700 CPU plus S-DSP with its own 64 KiB of
RAM, 128 KiB of work RAM, controller I/O, and a cartridge port that famously
accepts *additional processors* inside the cartridge.

### Ricoh 5A22 (65816 core)

The main CPU is a WDC 65C816 derivative. On real hardware it runs from a
21.477 MHz master clock, with each bus cycle taking 6, 8, or 12 master clocks
depending on the region accessed (FastROM/SlowROM/joypad registers). The 5A22
package also contains the DMA/HDMA engines, the multiply/divide unit, the
NMI/IRQ timers, and the joypad auto-read logic — in snesquik those "glue"
features live in `SnesBus`, while the 65816 core itself is `CPU`.

Important 65816 features for SNES emulation:

- It is little-endian.
- 24-bit address space (16 MiB), addressed as 256 banks of 64 KiB.
- 16-bit accumulator and index registers that can each be switched to 8-bit
  width via the `M` and `X` status flags.
- An *emulation mode* (6502-compatible) that the CPU boots into; games switch
  to native mode with `XCE`. Emulation mode forces `M`/`X` to 8-bit, pins the
  stack to page 1, and uses different interrupt vectors.
- Direct page (`D` register) replaces the 6502 zero page and can be relocated.
- `PB` (program bank) and `DB` (data bank) registers extend 16-bit addresses.
- Block-move instructions (`MVN`/`MVP`), stack-relative addressing, and
  24-bit "long" addressing modes.
- The reset vector is fetched from `$00:FFFC`.

In snesquik the CPU is `CPU` in `src/CPU_R5A22/core.h` / `core.cpp`, with the
opcode table and operations in `operations.cpp` and the addressing-mode
resolvers in `addressing_modes.cpp`.

### S-PPU

The PPU owns its own memories, none of which are CPU-addressable directly —
everything goes through ports at `$2100-$213F`:

- `VRAM`: 64 KiB (32 K 16-bit words) of tile/tilemap memory.
- `CGRAM`: 256 15-bit BGR colors.
- `OAM`: 544 bytes of sprite attributes (512-byte low table + 32-byte high
  table).

The PPU renders per scanline:

- Up to 4 background layers (mode-dependent), 2/4/8 bits per pixel.
- A sprite (OBJ) layer: 128 sprites, sizes 8x8 through 64x64, with per-line
  limits of 32 sprites and 34 sprite tiles.
- 8 background modes, including Mode 7 (affine-transformed 256-color layer)
  and offset-per-tile modes (2/4/6).
- Two clip windows combinable with AND/OR/XOR/XNOR logic per layer.
- Color math (add/subtract, half) between the main screen and either the sub
  screen or a fixed color, gated by windows.
- Per-scanline register changes via HDMA (gradients, letterboxes, wavy
  effects — HDMA is the SNES's signature raster trick).

In snesquik the PPU is `Ppu` in `src/S-PPU/ppu.h` / `ppu.cpp`.

### APU: SPC700 + S-DSP

The audio subsystem is a self-contained computer: a Sony SPC700 CPU
(~1.024 MHz) with 64 KiB of private RAM and a 16-channel-BRR-sample S-DSP that
produces 32 kHz stereo output. The main CPU cannot touch APU RAM; it talks to
the SPC700 exclusively through four byte-wide ports at `$2140-$2143`. On boot
the SPC700 runs a 64-byte IPL ROM implementing the upload handshake every game
uses to install its sound driver.

snesquik embeds blargg's `snes_spc` library (git submodule at
`src/APU_SPC700/snes_spc/`) for the SPC700+DSP core, wrapped by `Apu` in
`src/APU_SPC700/apu.h` / `apu.cpp`. The wrapper owns the CPU↔SPC time
synchronization, the per-frame sample extraction, and a `SPC_Filter` stage that
models the SNES DAC's high-pass behavior (without it, voice key-ons pop
audibly).

### Controllers

The standard SNES pad has a D-pad, B/A/Y/X, L/R shoulders, Start, and Select —
12 buttons shifted out serially. Software can read them two ways:

- Serially via `$4016` (strobe + one bit per read), or
- Via *auto-read*: setting NMITIMEN bit 0 makes the 5A22 latch all buttons
  into `$4218/$4219` during vblank (taking ~4224 master clocks, during which
  `$4212` bit 0 reads busy).

In snesquik the bus owns all joypad state; frontends translate their raw
events into the `ControllerButton` enum and call `SnesBus::setButton()`.

### Enhancement Chips (Coprocessors)

The SNES cartridge bus exposes enough address space and control lines that
many cartridges shipped with additional processors. snesquik implements four,
each written from hardware documentation rather than ported from another
emulator (a deliberate project rule):

| Chip | What it is | Example games | snesquik module |
| --- | --- | --- | --- |
| Super FX (GSU) | Custom RISC CPU rendering 3D/sprites into a shared framebuffer | Star Fox, Star Fox 2, DOOM, Yoshi's Island | `src/GSU/` |
| SA-1 | Second 65816 at 10.74 MHz with its own MMC, I-RAM, BW-RAM, DMA, arithmetic unit | Kirby's Dream Land 3, Super Mario RPG | `src/SA1/` |
| DSP-1 | NEC uPD7725 fixed-point DSP (matrix/trig math) | Super Mario Kart, Pilotwings, Suzuka 8 Hours | `src/DSP/` |
| S-DD1 | Entropy-coded graphics decompressor feeding DMA | Street Fighter Alpha 2, Star Ocean | `src/SDD1/` |

The DSP-1 is a *low-level* emulation: it interprets the actual uPD7725
program/data ROM dump (`dsp1b.rom`), so its math is exact. It therefore needs
that dump at runtime — see [DSP-1](#dsp-1-nec-upd7725).

## SNES Memory Map

The 65816 sees a 24-bit space. snesquik masks all bus addresses with
`mask24()` (`address & 0x00ffffff`).

### Hardware-Level Map (no coprocessors)

The SNES map is bank-oriented. "System" banks `$00-$3F` and their mirrors
`$80-$BF` each split into a low half (WRAM mirror + MMIO) and a high half
(cartridge ROM):

| Bank : offset | Meaning |
| --- | --- |
| `$00-$3F,$80-$BF : $0000-$1FFF` | Mirror of the first 8 KiB of WRAM |
| `$00-$3F,$80-$BF : $2100-$213F` | PPU registers |
| `$00-$3F,$80-$BF : $2140-$2143` | APU I/O ports (mirrored through `$217F`) |
| `$00-$3F,$80-$BF : $2180-$2183` | WRAM data port + 17-bit address |
| `$00-$3F,$80-$BF : $4016-$4017` | Joypad serial ports |
| `$00-$3F,$80-$BF : $4200-$421F` | 5A22: NMITIMEN, mul/div, H/V timers, RDNMI/TIMEUP/HVBJOY, auto-read results |
| `$00-$3F,$80-$BF : $4300-$437F` | DMA/HDMA channel registers (8 channels × 16 bytes) |
| `$00-$3F,$80-$BF : $8000-$FFFF` | Cartridge ROM (LoROM view) |
| `$40-$7D` | Cartridge (HiROM ROM / LoROM mirrors / SRAM, mapping-dependent) |
| `$7E-$7F` | 128 KiB WRAM, linear |
| `$C0-$FF` | Cartridge ROM (HiROM/ExHiROM primary view, FastROM region) |

### Cartridge Mappings

`CartridgeRom` (`src/BUS/bus.cpp`) implements three mappings, selected by the
ROM header:

- **LoROM**: 32 KiB pages. ROM offset = `(bank & 0x7f) * 0x8000 + (offset &
  0x7fff)` for `offset >= $8000` (and for the whole bank in `$40-$7D`/`$C0+`).
  SRAM appears at `$70-$7D:0000-7FFF` (and `$F0+`), plus a `$6000-$7FFF`
  window in system banks.
- **HiROM**: 64 KiB pages. ROM offset = `(bank & 0x3f) * 0x10000 + offset` in
  `$C0-$FF` / `$40-$7D`, upper halves of system banks mirror it. SRAM at
  `$20-$3F`/`$A0-$BF:6000-7FFF`.
- **ExHiROM**: like HiROM but `$C0+` maps the *first* 4 MiB and `$40-$7D` /
  system banks map the second 4 MiB (`+0x400000`).

ROM mirroring wraps by **modulo**, not a power-of-two mask — SNES ROM sizes
are frequently not powers of two (Star Fox 2 is `0xFFEC3` bytes). All three
wrap helpers (`CartridgeRom`, `Gsu::readMemory`, `Sa1::readRom`) use a
branch-then-modulo so the division only happens on the rare out-of-range
access:

```cpp
return rom[offset < rom.size() ? offset : offset % rom.size()];
```

### snesquik Bus Decode Order

The SNES map *overlaps by design* once coprocessors are present (the GSU
claims most of the cartridge space when running, the SA-1 remaps everything),
so a non-overlapping device-range table cannot express it.
`SnesBus::read8()` therefore resolves addresses by **priority order**, not by
a device table:

1. Banks `$7E/$7F` → WRAM directly.
2. If a GSU is attached → `gsuMapRead()` (see [Super FX](#super-fx-gsu)).
3. If an SA-1 is attached → `sa1MapRead()`.
4. If a DSP is attached → `dspMapRead()`.
5. If an S-DD1 is attached → `sdd1MapRead()`.
6. System-bank fast paths: WRAM mirror (`offset <= $1FFF`), then ROM
   (`offset >= $8000`).
7. `mapWram()` / `mapMmio()` (MMIO handlers dispatch by register).
8. Cartridge SRAM, then cartridge ROM.
9. Fall through → open bus.

Each coprocessor decode helper (`gsuMapRead/Write`, `sa1MapRead/Write`,
`dspMapRead/Write`, `sdd1MapRead/Write`) returns `true` when the chip owns the
address and is shared by all four access paths, so the mapping logic exists
exactly once.

### Open Bus

`openBusValue` tracks the last byte driven onto the data bus. Reads of
unmapped addresses (and of chip regions that are *decoded* but not *driven*,
such as GSU RAM while the GSU owns it) return the current open-bus value.
Every successful read/write refreshes it. `$4211` (TIMEUP) genuinely mixes
open bus into bits 6-0, as hardware does.

### `read8/write8` vs `readRaw/writeRaw`

The bus has two access tiers:

- `read8()` / `write8()` — the CPU-visible path. Updates open bus, notifies
  the `TraceListener`, triggers APU synchronization on `$2140-$2143`, and has
  read side effects (`$4210` clears the NMI flag, `$4211` clears the IRQ
  flag, `$4016` shifts the joypad register).
- `readRaw()` / `writeRaw()` — the DMA/HDMA path. Same address decode, but
  *without* the `$4210/$4211` acknowledge side effects and without trace
  events, because a DMA sweeping memory must not acknowledge interrupts.
  Stateful data ports that hardware DMA does exercise (`$2180` WRAM port,
  `$2118/$2119` VRAM, APU ports) keep their side effects in the raw path.

## Timing Model and Units

This is the most important section for not breaking things. snesquik runs
three clock domains off one instruction-granular loop, and every `step*` call
has specific units:

| Quantity | Definition |
| --- | --- |
| Master clock | 21.477272 MHz (`21477272` Hz) |
| CPU cycle | 1 cycle = **6 master clocks**, flat (no 8/12-clock slow regions) |
| PPU dot | 1 dot = **4 master clocks** → 1 CPU cycle = 1.5 dots |
| Scanline | 341 dots |
| Frame | 262 scanlines = 89,342 dots = 357,368 master clocks ≈ 60.0988 Hz |
| SPC clock | 1.024 MHz (handled inside `Apu::advance()`) |
| GSU cycle | 2 master clocks at CLSR=0 (10.7 MHz), 1 at CLSR=1 (21.4 MHz) |
| SA-1 cycle | 2 master clocks (10.74 MHz) |

Conversions used in the frame loop, per executed CPU instruction:

```cpp
const uint32_t cpuCycles = cpu.step();          // CPU cycles
dots     = (cpuCycles * 3 + remainder) / 2;     // ×1.5, remainder carried
dmaDots  = bus.consumeDmaDots();                // DMA/HDMA halt time, in dots
bus.stepApu(dmaDots * 2 / 3);                   // extra CPU-cycle units (dots ×4/6)
bus.stepGsu(cpuCycles * 6 + dmaDots * 4);       // master clocks
bus.stepSa1(cpuCycles * 6 + dmaDots * 4);       // master clocks
bus.stepDsp(cpuCycles + dmaDots);               // instruction budget
ppu.tick(dots + dmaDots);                       // dots
```

Two rules that have bitten this project before:

- **DMA time must advance every other clock domain.** The CPU cycle counter
  does not include DMA halt time (the bus tracks it in `pendingDmaDots`, 2
  dots per DMA byte, 16 dots per HDMA scanline). Forgetting to feed
  `dmaDots` into the APU starved audio production by ~6% in DMA-heavy scenes
  and manifested as crackle.
- **GSU step costs are already master clocks.** The cost constants
  (`clsr ? 1 : 2` per internal cycle, `clsr ? 5 : 6` per memory access —
  bsnes's values) have the CLSR clock-select baked in. `Gsu::step()` must
  subtract them from the budget *as-is*; a second `* (clsr ? 1 : 2)`
  conversion once ran every CLSR=0 game (Star Fox) at half speed while
  leaving CLSR=1 games (Star Fox 2, DOOM) untouched.

The CPU's flat 6-clocks-per-cycle model is a deliberate simplification: there
is no FastROM speedup and no per-region 6/8/12 access cost. See
[Caveats](#current-architectural-caveats).

## Build Modules

`CMakeLists.txt` defines the project as static libraries plus executables:

| Target | Purpose |
| --- | --- |
| `snesquik_cpu` | 65816 core, opcode table, addressing modes |
| `snesquik_ppu` | S-PPU software renderer |
| `snesquik_apu` | `Apu` wrapper + vendored blargg `snes_spc` sources |
| `snesquik_gsu` | Super FX core, operations, opcode table |
| `snesquik_sa1` | SA-1 (links `snesquik_cpu` — it instantiates a second `CPU`) |
| `snesquik_dsp` | NEC uPD7725 (DSP-1) |
| `snesquik_sdd1` | S-DD1 decompressor |
| `snesquik_bus` | `SnesBus` + the savestate module; links every chip library |
| `snesquik_cartridge` | ROM image/header parser |
| `snesquik_debug` | Headless probe |
| `snesquik_renderer` | SDL2/OpenGL presenter (built if SDL2+OpenGL found) |
| `snesquik` | The GUI executable (`src/MAIN/snesquik.cpp`) |
| `*_tests` (×8) | Per-subsystem test executables registered with CTest |

Options: `SNESQUIK_BUILD_RENDERER` (default ON, silently skipped without
SDL2/OpenGL) and `SNESQUIK_BUILD_TESTS` (default ON). C++20, no exceptions
policy or special flags. All tests run with the repository root as their
working directory so they can find `tests/roms/`.

**Submodule gotcha:** `src/APU_SPC700/snes_spc` is a git submodule with *no
URL in `.gitmodules`* and local modifications. `git submodule update --init`
fails in fresh clones/worktrees — copy the directory from an existing checkout
instead.

## Composition Root and the Frame Loop

There is no `Emulator` class. `main()` composes the machine:

1. Parse CLI args. If `--probe` is present, hand everything to
   `snesquik::debug::runProbe()` and exit (headless path).
2. Read the ROM file; `cartridge::parseRomImage()` detects the copier header,
   scores LoROM/HiROM/ExHiROM header candidates, and returns the best.
3. Construct `Ppu`, `reset()` it.
4. Construct `SnesBus`; attach PPU, trace listener, cartridge (with the
   header-declared SRAM size).
5. `bus.initApu()` — creates the `SNES_SPC` instance, installs the 64-byte
   IPL ROM, resets it, arms the sample buffer.
6. Attach coprocessors based on the header chipset byte: `attachGsu()`,
   `attachSa1()`, `attachDsp()` (resolving the uPD7725 dump path),
   `attachSdd1()`.
7. Resolve battery SRAM: SA-1 carts persist BW-RAM, everyone else the
   cartridge SRAM. Load `<rom>.srm` if present.
8. Construct `CPU cpu(bus)`, `bus.setCpu(cpu)` (wires the CPU cycle counter
   into APU sync), `cpu.reset()` (fetches the reset vector).
9. Initialize `SdlGlRenderer` and SDL audio, then run the frame loop.

### One Emulated Frame

Each iteration of the outer `while (running)` loop is one video frame:

```text
per frame:
  clearNmiEdge, resetApuPortLog
  ppu.beginFrame()            # counters to 0, invalidate tile cache,
                              # blank the overscan tail rows
  bus.setVblank(false)
  bus.beginFrame()            # (re)initialize all HDMA channels from $43xx
  bus.runHdmaScanline()       # hardware does the first HDMA on line 0

  while dotsThisFrame < 89342:
    cpuCycles = cpu.step()                       # one instruction (or IRQ/NMI entry)
    ppuDots   = cpuCycles*1.5 + consumeDmaDots()
    step APU / GSU / SA-1 / DSP (see timing table)
    ppu.tick(ppuDots)                            # advances H/V counters
    poll SDL events every >=341 dots

    bus.checkIrqCrossing(prevH, prevV, currH, currV)
    cpu.setIrqLine(bus.irqFlag() || bus.gsuIrqPending() || bus.sa1IrqPending())

    # process EVERY scanline crossed by this step (a large DMA can span many):
    while lastLine != ppu.verticalCounter():
      completedLine = lastLine; lastLine = (lastLine+1) % 262
      if completedLine < visibleHeight: ppu.renderScanline(completedLine)
      if lastLine <= visibleHeight:     bus.runHdmaScanline()

    if first crossing into vblank (V >= visibleHeight):
      bus.setVblank(true); bus.beginJoypadAutoRead()
      arm NMI with a 4-instruction delay      # lets LDA $4210 polls see bit 7
    tick the NMI arm countdown; requestNMI when it hits 0
    bus.tickJoypadAutoRead(cpuCycles)
    service nmiEdge (NMITIMEN enable while flag already set)

  bus.endApuFrame()          # extract this frame's 32 kHz samples
  audio dynamic-rate-control + SDL_QueueAudio
  handle F6/F7 save-state requests
  battery SRAM checksum debounce
  renderer.present(ppu.framebuffer())
  frame limiter (pace to 60.0988 Hz)
```

Points worth internalizing:

- **Scanline crossings are processed one line at a time**, even when a single
  step (a multi-kilobyte DMA) jumps many lines. On hardware HDMA pre-empts
  general DMA every scanline; batching crossings once made per-line HDMA
  tables (Star Fox's INIDISP letterbox) fall behind and flicker.
- **IRQs are level-triggered.** `checkIrqCrossing()` computes whether the H/V
  timer target fell inside the `(prev, curr]` dot window (handling wrap), sets
  `irqPending`, and the loop re-asserts `cpu.setIrqLine(...)` every
  instruction. The game acknowledges by reading `$4211` (or the GSU's `$3031`
  / the SA-1's `$2202`).
- **The NMI 4-instruction arm delay is a compatibility heuristic**, not
  hardware modeling: raising vblank and requesting the NMI in the same
  instant broke games (Street Fighter Alpha 2) whose main loop polls `$4210`
  for bit 7 — the NMI handler would always consume the flag first.
- The frame limiter uses `SDL_GetPerformanceCounter`, sleeps in ≥2 ms chunks,
  and resyncs (rather than fast-forwarding) if it falls more than 4 frame
  periods behind.

### Dual Frame Loops

`runProbe()` in `src/DEBUG/probe.cpp` contains a *second copy* of the inner
frame loop — same stepping, same scanline processing, same NMI arm countdown —
without SDL, audio pacing, or wall-clock limiting (it runs as fast as
possible, ~500+ fps). This duplication is deliberate: the probe is the
deterministic debugging instrument, and keeping it free of frontend concerns
is worth the copy. **Any change to the stepping logic must be made in both
files**; both sites carry comments saying so.

## SnesBus

`SnesBus` (`src/BUS/bus.h` / `bus.cpp`) implements the `cpu_r5a22::Bus`
interface (`read8`/`write8`) and owns everything except the PPU (attached by
pointer) and the CPU (referenced for its cycle counter via `setCpu()`).

### Fields

`wram` — `std::array<uint8_t, 128*1024>`, banks `$7E-$7F` plus the low-8 KiB
system-bank mirror.

`mmio` — `std::array<uint8_t, 0x4000>` backing `$2000-$5FFF`. Registers with
behavior are intercepted in `read8`/`write8`; everything else (importantly the
`$43xx` DMA channel registers) reads/writes this array directly. DMA internals
(`dmaAAddress`, `dmaSize`, `hdmaTableAddress`, `hdmaIndirectAddress`) are
accessors over this array, so games observing DMA registers mid-transfer see
live values, and save states capture DMA state for free.

`apuCore`, `gsuCore`/`gsuPresent`, `sa1Core`/`sa1Present`,
`dspCore`/`dspPresent`/`dspHiRomMap`, `sdd1Core`/`sdd1Present`/`sdd1DmaActive`
— the chips; `*Present` flags gate their decode helpers.

`cart` — the `CartridgeRom` (ROM bytes + SRAM + mapping).

`ppuCore` — non-owning `Ppu*`; `$2100-$213F` accesses forward to it.

`traceListener` — optional `TraceListener*` receiving `mmioRead/mmioWrite/
dmaStart/dmaTransfer/hdmaWrite/vblank` callbacks. The GUI logs these when
F5 logging is on; the probe writes them to `mmio.log`.

`cpuTiming` — `const CPU*`; when set, `stepApu()`/APU port accesses sync the
SPC to `cpu->totalCycles()` instead of accumulating deltas manually.

`openBusValue`, `wramPortAddr` (17-bit `$2180-$2183` pointer), multiplier /
divider latches (`wrmpya/wrmpyb/rdmpy`, `wrdiva/wrdivb/rddiv`), NMI/IRQ state
(`nmiEnable`, `nmiFlag`, `nmiEdge`, `vblankActive`, `hvIrqMode`,
`irqHTimeVal`, `irqVTimeVal`, `irqPending`), joypad state (current/latched
16-bit button words, serial read index, strobe, auto-read enable/busy/cycle
countdown), `hdma[8]` channel state, `pendingDmaDots`, and `apuPortLog`
(port-traffic counters used by the probe).

### CPU MMIO Registers Implemented

| Register | Behavior |
| --- | --- |
| `$2140-$2143` | APU ports; each access first syncs the SPC to the exact CPU cycle |
| `$2180-$2183` | WRAM data port with auto-incrementing 17-bit address |
| `$4016` W | Joypad strobe; latches buttons while high |
| `$4016` R | Serial joypad bit (bit 16+ reads 1 = controller present) |
| `$4017` R | Returns `$00` (controller 2 not emulated) |
| `$4200` | NMITIMEN: NMI enable (bit 7), H/V IRQ mode (bits 5-4), auto-read (bit 0). Enabling NMI while the flag is set arms `nmiEdge`; disabling IRQs acks a pending IRQ |
| `$4202/$4203` | 8×8 multiply, result in `$4216/$4217` (instant — no 8-cycle delay modeled) |
| `$4204-$4206` | 16÷8 divide, quotient `$4214/$4215`, remainder `$4216/$4217` (instant; ÷0 → `$FFFF` / dividend) |
| `$4207-$420A` | H/V IRQ timer targets (9-bit) |
| `$420B` | MDMAEN — runs general DMA immediately, whole transfer in one call |
| `$420C` | HDMAEN — sampled at `beginFrame()` and per scanline |
| `$4210` R | RDNMI: NMI flag (cleared on read) + CPU version `2` |
| `$4211` R | TIMEUP: IRQ flag (cleared on read) + open bus bits 6-0 |
| `$4212` R | HVBJOY: vblank, hblank (`hCounter >= 262`), auto-read busy |
| `$4218/$4219` R | Auto-read result, controller 1 |

### DMA

`runDma(channelMask)` executes each enabled channel *synchronously and
completely*:

- Size 0 means 65,536 bytes.
- `transferDmaByte()` handles both directions (`DMAP` bit 7), the B-bus
  address sequence per transfer mode 0-7 (the `sequence[8][4]` table), fixed
  vs incrementing/decrementing A-address (`DMAP` bits 3-4).
- **The A-bus address wraps within its bank**: only the 16-bit offset
  increments; the bank byte `$43x4` is never carried into. DOOM's sound-table
  loader depends on this (a bank carry corrupted its voice tables → the
  historical shotgun crash).
- If the cartridge is an S-DD1 and the channel was armed via `$4801`, A-bus
  reads come from the decompressor instead of memory.
- Timing: `pendingDmaDots += size * 2` (2 dots = 8 master clocks per byte),
  consumed by the frame loop as CPU halt time.

### HDMA

Per-channel state (`HdmaChannel`): `active`, `doTransfer`, and `lineCounter`,
which stores the *full* table count byte — bit 7 (repeat) included. Flow:

- `beginFrame()` reinitializes every enabled channel: table address ←
  `$43x2/3`, then `reloadHdma()` fetches the first count byte (a zero byte
  deactivates the channel; indirect mode also fetches the 16-bit data
  pointer).
- `runHdmaScanline()` (called once per scanline, lines 0-224): if
  `doTransfer`, `transferHdma()` writes 1/2/4 bytes (per the transfer-mode
  byte count table) from the table (direct) or through the indirect pointer,
  to `$21xx` B-bus addresses. Then the line counter decrements; bit 7 of the
  result decides whether the *next* line transfers, and when the low 7 bits
  hit zero a new table entry loads.
- Each scanline with HDMA active adds 16 dots of CPU halt time.

### Interrupt Timing

`checkIrqCrossing(prevH, prevV, currH, currV)` converts both positions to
absolute dot indices in the 89,342-dot frame, computes the wrapped span, and
triggers if the target (H timer: every line at H=HTIME; V timer: line start;
HV: exact dot) falls within `(prev, curr]`. This makes IRQs robust against
multi-line steps.

### Joypad Auto-Read

`beginJoypadAutoRead()` at vblank start sets busy for 704 CPU cycles
(≈4224 master clocks ≈ 3 scanlines); `tickJoypadAutoRead()` counts it down and
`finishJoypadAutoRead()` latches the current buttons into `$4218/$4219`.
Games that poll `$4212` bit 0 before reading the result therefore see a
realistic busy window.

## Cartridge and ROM Parsing

### `CartridgeRom`

Holds `rom` (bytes), `sram` (bytes, `0xff`-filled at power-on), and the
`CartridgeMap`. `mapCpuAddress()` / `mapSramAddress()` implement the
[three mappings](#cartridge-mappings); `read()`/`readSram()`/`writeSram()`
apply modulo mirroring.

### Header Detection (`src/CART/rom_parser.cpp`)

`parseRomImage()`:

1. Strips a 512-byte copier header if `size % 0x8000 == 512`.
2. Builds a header candidate for each mapping (header at `$7FC0` LoROM,
   `$FFC0` HiROM, `$40FFC0` ExHiROM) — rejected outright if the 21-byte title
   isn't printable ASCII.
3. Scores each candidate: printable title (+4), checksum⊕complement ==
   `$FFFF` (+6), map-mode nibble matches the candidate mapping (+5 twice —
   once in scoring, once as a direct confidence bonus), sane speed bits (+2),
   plausible ROM/RAM size codes (+2/+1), declared size within 2× of the file
   (+2), and at least one non-`0000`/`FFFF` interrupt vector (+4).
4. Picks the highest-confidence candidate.

`RomHeader` also decodes the chipset byte into capability queries used by the
composition roots: `hasSuperFx()` (`$13-$1A`; expansion-RAM size from the
extended header byte at `headerOffset - 3`, defaulting to the GSU-2's 64 KiB),
`hasSa1()` (`$3x`), `hasDsp()` (`$0x` with low nibble ≥ 3), `hasSdd1()`
(`$4x`), and `hasBattery()` (low nibble 2/5/6, excluding Super FX whose
chipset encoding differs). `main()` prints a warning for chipset values that
declare a coprocessor snesquik doesn't emulate.

## CPU Implementation

### Architecture

The core is a table-driven interpreter with a clean three-way split:

- `core.cpp` — the `CPU` class: register file, memory access helpers, stack
  helpers, flag helpers, interrupt entry, `step()`.
- `addressing_modes.cpp` — 26 addressing-mode resolvers, each returning an
  `Operand` (`address`/`value`/`immediate`/`accumulator` variants).
- `operations.cpp` — 92 operation implementations plus the 256-entry
  `opcodeTable()`, each entry `{mnemonic, operationId, addressingId,
  operationFn, addressingFn, baseCycles, baseBytes}`.

`CPU::step()`:

1. If `nmiPending` → clear wait/stop, take the NMI (7/8 cycles).
2. If stopped (`STP`) → burn 1 cycle.
3. If `irqLine && !I` → take the IRQ.
4. If waiting (`WAI`) → resume if the IRQ line is asserted even when masked
   (hardware WAI is level-sensitive: with `I` set, execution simply continues
   past the WAI without vectoring); otherwise burn 1 cycle.
5. Fetch opcode, add `baseCycles`, resolve the addressing mode (which may add
   penalty cycles), execute, then `normalizeEmulationRegisters()`.

### Registers and Modes

`Registers`: 16-bit `a/x/y/s/d/pc`, 8-bit `db/pb/p`, and the `emulation`
flag. `normalizeEmulationRegisters()` enforces the invariants after every
instruction: emulation mode forces `M|X` set and `S` into page 1; `X8` masks
the index high bytes to zero.

Width handling is centralized: `regA()/setRegA()` respect `M` (8-bit A
preserves the hidden high byte), `setRegX/Y` mask by index width, and
`loadOperand()/storeOperand()` take an explicit width.

### Cycle Accounting

`baseCycles` from the table, plus penalties added where hardware adds them:

- +1 for 16-bit memory access (`addWidthCycle`), +2 for 16-bit
  read-modify-write.
- +1 when the direct-page register's low byte is non-zero.
- +1 on `abs,X` / `abs,Y` / `(dp),Y` when the index is 16-bit *or* a page
  boundary is crossed.
- +1 for taken branches, +1 more for an emulation-mode page crossing.

### Hardware Quirks Modeled

- **Emulation-mode direct page wrapping**: 16-bit direct pointer fetches wrap
  within the page only when `DL == 0`; otherwise they wrap at the bank-0
  16-bit boundary (`read16BankWrap`).
- **The `(dp,X)` quirk**: with `DL != 0` in emulation mode, the low pointer
  byte is fetched without page wrap but the `+1` for the high byte wraps
  within the page.
- **"New" 65816 instructions and the stack**: `JSL/RTL/PEA/PEI/PER/PHB/PHD/
  PLB/PLD` and `JSR (a,x)` use the `NoWrap` stack helpers — they do not wrap
  within page 1 even in emulation mode; `S` is renormalized afterwards.
- **Interrupt P pushes**: emulation mode forces bit 5 set and pushes bit 4
  (B) set only for `BRK` — clear for IRQ/NMI so handlers can distinguish
  them. Native mode pushes `PB` first and uses the native vector set
  (`$FFE4-$FFEE`); emulation uses `$FFF4-$FFFE` with BRK sharing the IRQ
  vector.
- **Decimal mode**: `ADC`/`SBC` run nibble-serial algorithms in which the
  overflow flag is evaluated from the top nibble's sum *before* the final
  decimal adjustment, matching silicon. SBC is implemented as addition of the
  complement with adjustment on no-carry nibbles.
- **Block moves** (`MVN`/`MVP`) execute one byte per `step()` and rewind `PC`
  by 3 until `A` underflows, so interrupts and per-instruction stepping
  interleave correctly with long moves.

### Interrupt API

External code asserts `setIrqLine(bool)` (level) or `requestNMI()` (edge).
`SaveState` (a plain struct: registers + cycles + irq/nmi/stop/wait flags) is
produced/consumed by `saveState()`/`loadState()`.

## APU Implementation

### The `snes_spc` Core

The submodule provides `SNES_SPC` (SPC700 CPU + S-DSP + 64 KiB RAM, cycle
-based `run_until` execution driven through its port-access API) and
`SPC_Filter` (gain + high-pass). The `Apu` wrapper (`src/APU_SPC700/apu.cpp`)
owns one of each, plus the standard 64-byte IPL ROM image (the `$FFC0-$FFFF`
bootstrap implementing the `$AABB`-ready / upload handshake protocol).

### Time Synchronization

The wrapper keeps the SPC's clock aligned to the CPU's:

- `spcTime` — SPC clocks elapsed this frame (the time argument passed to
  `snes_spc`'s port and frame calls).
- `spcTimeAccum` / `lastCpuCycle` — fixed-point remainder and the CPU cycle
  count at the last sync.
- `advance(cpuCycles)` converts CPU cycles to SPC clocks:
  `spcTimeAccum += cpuCycles * 6144000` (= 6 master clocks × 1,024,000), then
  divides by 21,477,272, carrying the remainder exactly.
- `syncToCpuCycle(cpuCycle, extra)` — the normal path. Computes the delta
  from `lastCpuCycle`, clamps absurd deltas (> `0xffff`, e.g. a stale
  baseline right after a state load) to zero, adds unclamped `extra` (DMA
  halt time), and advances.

Every `$2140-$2143` access syncs the SPC to the *exact current CPU cycle*
before performing the port read/write — this sub-instruction alignment is
what keeps tight upload handshake loops (DOOM's sound driver) stable. A
`resyncCpuBaseline()` call after save-state load re-anchors the baseline.

### Frame Audio Path

`endFrame()` calls `spc->end_frame(spcTime)`, copies out `sample_count()`
stereo int16 samples (~534 pairs per 60 Hz frame at 32 kHz), runs
`SPC_Filter` over them (the DAC high-pass that removes key-on DC pops),
re-arms the buffer, and resets frame-local time. The frontend pulls
`frameSamples()` once per frame.

### Debug Accessors

`debugPc()`, `debugError()` (the SPC core's internal error string — nonnull
means the SPC executed something illegal, the canonical "sound engine
crashed" detector), `debugRam(addr)`, `debugInPort/OutPort(n)`. The probe
prints all of these per snapshot.

## PPU Implementation

### Memories and Register File

`Ppu` owns `vram` (32 K words), `cgram` (256 words), `oam` (544 bytes), the
RGBA `frame` (256×240) and a parallel `priorityFrame`, plus a `registers[0x40]`
shadow of `$2100-$213F` (reads of write-only registers return the last written
byte — a cheap open-bus stand-in).

Register decoding is a single `switch` in `writeRegister()` /
`readRegister()`. Notable port behaviors implemented:

- **VRAM port** (`$2115-$211A`): `VMAIN` increment-on-low/high, step sizes
  1/32/128, and the three address-remap modes. Reads go through the
  **prefetch latch**: setting `$2116/7` preloads it, and an increment-trigger
  read returns the latch *then* reloads from the pre-increment address —
  games rely on the "dummy read" convention, and Super Metroid's
  decompress-to-VRAM broke until this was exact.
- **CGRAM port** (`$2121/22/3B`): write-twice low/high latch with the
  read/write phase flag.
- **OAM port** (`$2102-$2104`, `$2138`): 10-bit address, write-pair latch for
  the low table, single-byte writes to the high table.
- **Mode 7 registers**: write-twice `mode7Latch` pairs; `$211B/$211C` also
  feed the `M7A × M7B(high byte)` signed multiply readable at `$2134-$2136`.
- **Counters/latches** (`$2137`, `$213C-$213F`): reading SLHV latches H/V;
  OPHCT/OPVCT alternate low/high on repeated reads; STAT78 resets the
  high/low toggles and reports the field flag; STAT77 reports sprite
  time/range overflow.
- **COLDATA** (`$2132`): per-channel fixed-color write with the three select
  bits.

### Rendering Model

The PPU renders **per scanline into line buffers**, then composes:

```text
renderScanline(y):
  forceBlank or y >= visibleHeight -> black row, done
  buildVisibleSpriteList(y)         # OAM scan with 32-sprite/34-tile limits
  for each BG enabled on main|sub:  renderBgLine(bg, y)
  if OBJ enabled:                   renderObjLine(y)
  for each x:
    main = composeFromBuffers(x, main screen)
    sub  = composeFromBuffers(x, sub screen)   # only if CGWSEL bit 1
    frame[y][x] = rgba(applyColorMath(main, sub, x))
```

- `renderBgLine()` fills per-BG color/priority/opaque line buffers. It walks
  pixels left to right, memoizes the tilemap entry per tile, handles 16×16
  tiles, mosaic, and (in modes 2/4/6) **offset-per-tile**: the per-column
  H/V scroll overrides are prefetched from BG3's tilemap — H entry at BG3
  tile row `BG3VOFS>>3`, V entry at row `(BG3VOFS+8)>>3`, with the mode-4
  single-entry H/V select bit. Window masking deliberately does *not* happen
  here (the buffers are unmasked; `composeFromBuffers` masks per screen).
- `decodeTilePixel()` decodes planar 2/4/8bpp tiles through a 128-entry
  direct-mapped **row cache** keyed on (chr base, tile, bpp, row). The cache
  key has no VRAM-content component, so it is invalidated per scanline and at
  `beginFrame()` — stale entries once served pause-menu tiles from gameplay
  CHR.
- `composeFromBuffers()` picks the highest-priority opaque pixel among
  enabled, non-window-masked layers (backdrop = CGRAM 0). Priority values
  come from per-mode tables (`bgPriorityValue`, `objPriorityValue`) ported
  from bsnes's mode tables — every layer/priority combination within a mode
  has a distinct rank, including the mode-1 BG3-priority quirk (only BG3's
  *high-priority* tiles are promoted to the front) and EXTBG mode 7.
- **Sprites**: `buildVisibleSpriteList` applies the per-line 32-sprite and
  34-tile hardware limits (setting the STAT77 flags); `renderObjLine` then
  draws in OAM order with **lowest OAM index winning overlaps** regardless of
  priority — OBJ priority only orders sprites against backgrounds. Name
  select/base come from OBJSEL; the sprite Y-wrap rule handles entries
  straddling the 256-line wrap.
- **Mode 7**: full hardware formula per anomie's documentation — 13-bit
  signed center/scroll registers, scroll applied *inside* the matrix
  transform, offsets clipped to ±1024, per-term products truncated to
  multiples of 64, out-of-bounds handling per M7SEL (transparent / tile 0
  fill), screen flip bits, and EXTBG (BG2 as the 7-bit-priority second
  layer). Direct-color mode is supported for 8bpp layers via CGWSEL bit 0.
- **Windows**: two windows with per-layer enable/invert selectors
  (`W12SEL/W34SEL/WOBJSEL`) and per-layer combine logic
  (OR/AND/XOR/XNOR from `WBGLOG/WOBJLOG`), applied per screen through
  `TMW/TSW`. The color window (OBJ selector high nibble) feeds both the
  color-math gate (CGWSEL bits 5-4) and the **force-main-black region**
  (CGWSEL bits 7-6) — the latter is applied before color math in
  `applyColorMath()` (DOOM's 3D-viewport side borders).
- **Color math** (`applyColorMath`/`blendColors`): add/subtract with optional
  halving, per-layer enables from CGADSUB (OBJ math only for palettes 4-7),
  addend = sub screen (CGWSEL bit 1, falling back to the fixed color when the
  sub pixel is backdrop — SMW's title gradient) or the fixed COLDATA color.
- `rgbaFromSnesColor()` expands 5-bit channels and applies the INIDISP
  brightness (0-15) multiplicatively.

Debug flags (F1-F4 in the GUI) can disable color math, disable windows, or
isolate/force BG3 — bring-up tools that remain wired in.

### Timing Interface

`tick(dots)` just advances `hCounter`/`vCounter` (341×262, field flag toggled
per wrap). The *frame loop*, not the PPU, decides when scanlines complete and
when vblank starts; `hblank()` reports `hCounter >= 262`. `visibleHeight()`
is 224 or 239 (SETINI overscan bit).

### Save State

`Ppu::saveState()` is a raw `memcpy` of the whole object (statically asserted
trivially copyable). Fast and complete, but it ties `.state` files to the
exact build — any member change invalidates them. The loader rejects size
mismatches.

## Super FX (GSU)

`src/GSU/gsu.h`/`gsu.cpp` (core), `gsu_operations.cpp` (instruction
implementations), `gsu_table.cpp` (256-entry dispatch table).

### Programming Model

- 16 × 16-bit registers `r0-r15`; `r14` is the ROM-buffer address (writing it
  starts a ROM prefetch), `r15` is the program counter.
- `SFR` status word: Z/CY/S/OV flags, `G` (GO — the chip runs while set),
  `R` (ROM buffer busy), `ALT1/ALT2` instruction prefixes, `B` (the
  `WITH` prefix), and the IRQ flag.
- `sreg`/`dreg` source/destination register pointers, set by `FROM`/`TO`/
  `WITH` prefixes; `regReset()` clears prefixes after non-prefix
  instructions.
- Banking: `PBR` (code), `ROMBR` (ROM buffer), `RAMBR` (RAM buffer), `SCBR`
  (screen base), `SCMR` (screen mode / height + RON/RAN bus grants), `CBR`
  (cache base), `COLR`/`POR` (plot color and options), `CLSR` (clock select),
  `CFGR` (IRQ mask, multiplier speed).

### Execution and the Pipeline

The GSU has a one-byte pipeline: `executeOne()` executes the previously
fetched byte while fetching the next (`pipe()` is how operands consume
immediate bytes). Branches therefore have a delay slot, which falls out of
the pipeline model naturally. `r15Modified` distinguishes jumps from the
default `++r15`.

### Instruction Cache

512-byte cache mapped at `CBR`, 32 × 16-byte lines with valid bits. Fetches
inside the cache window cost 1 GSU cycle on a hit; a miss loads the whole
16-byte line at ROM speed. `CACHE`, `LJMP` and stopping the chip via `$3030`
move/flush the cache base. The S-CPU can read/write cache contents at
`$3100-$32FF`.

### Memory Access and Buffers

`readMemory()` maps the GSU's view: banks `$00-$3F` LoROM-style
(`(bank & 0x3f) << 15 | (offset & 0x7fff)`), `$40-$5F` linear ROM, `$60-$7F`
RAM. ROM mirroring is modulo (non-power-of-two sizes — see
[Cartridge Mappings](#cartridge-mappings)).

ROM and RAM accesses go through hardware-modeled **latency buffers**:
`updateRomBuffer()` sets the R flag and a countdown (`romcl`); `step()`
decrements it and completes the fetch into `romdr` when it expires;
`readRomBuffer()` (`GETB`/`GETC` etc.) syncs first. RAM writes buffer the
same way (`ramcl/ramar/ramdr`), so back-to-back `STW` and RAM reads have
realistic stall behavior.

### PLOT Pipeline

`plot(x, y)` implements the pixel port: transparency per POR/COLR rules
(with the freeze-high 4bpp case), dithering, and a two-slot **pixel cache**
of 8-pixel tiles flushed to screen RAM as bitplanes — a full 8-pixel slot
flushes without the read-modify-write, a partial one merges with memory.
`rpix()` flushes both slots then reads a pixel back (and is what games use to
synchronize on render completion). `screenPixelAddress()` implements the
SCMR height modes (128/160/192/OBJ) and 2/4/8bpp plane layout.

### The S-CPU's View

Registered in the bus decode (`gsuMapRead/Write`):

| S-CPU address | Meaning |
| --- | --- |
| `$00-$3F/$80-$BF : $3000-$34FF` | GSU MMIO: `r0-r15` at `$3000-$301F` (writing `$301F` sets GO), SFR, PBR, ROMBR, VCR, RAMBR, CBR, SCBR, CLSR, SCMR, CFGR, BRAMR, cache window `$3100-$32FF` |
| `$00-$3F : $6000-$7FFF` | Game-pack RAM window (if RAN grants it) |
| `$00-$3F : $8000+`, `$40-$6F` | ROM — but only while RON grants it |
| `$70-$7D` | Game-pack RAM linear |

While the GSU runs with RON/RAN set, the S-CPU reading ROM gets
`cpuRomConflictValue()` — the fixed 16-byte pattern real hardware returns,
whose layout doubles as interrupt vectors pointing at `$0104`. Reading `$3031`
acknowledges the GSU IRQ (raised by `STOP` unless masked by CFGR).

### Stepping and Cost Model

`run(masterClocks)` adds to a signed budget and executes instructions while
the budget is positive and GO is set. All costs are charged in **master
clocks with CLSR baked into the constants** (see
[Timing Model](#timing-model-and-units)); instructions that consumed no
modeled time are charged one minimum cycle. `MULT/UMULT` and `FMULT/LMULT`
charge their CFGR-dependent extra cycles explicitly.

## SA-1

`src/SA1/sa1.h`/`sa1.cpp`. The SA-1 is a second `cpu_r5a22::CPU` — the same
65816 core class — for which `Sa1` itself implements the `Bus` interface.
This reuse is the payoff of keeping the CPU core bus-agnostic.

### Memory Maps

The SA-1 CPU's own map (`Sa1::read8/write8`): I-RAM (2 KiB) at `$0000-$07FF`
and `$3000-$37FF`; MMIO `$2200-$23FF`; BW-RAM block window `$6000-$7FFF`;
BW-RAM linear at `$40-$4F`; ROM elsewhere via the MMC. Reset/NMI/IRQ vectors
are *intercepted*: reads of `$00:FFEA/EE/FC` return the CNV/CIV/CRV registers
the S-CPU programmed.

The S-CPU's view (via `sa1MapRead/Write` in the bus): MMIO `$2200-$23FF`,
I-RAM `$3000-$37FF`, its own BW-RAM window `$6000-$7FFF` (block select in
BMAPS), BW-RAM linear `$40-$4F`, ROM through the same MMC.

The MMC maps four super-bank registers (`$2220-$2223`, 1 MiB each: CXB/DXB/
EXB/FXB) over the LoROM-style system banks and the HiROM-style `$C0-$FF`
quadrants.

### Control and Messaging

`$2200` (CCNT) lets the S-CPU hold the SA-1 in reset/wait, send 4-bit
messages, and raise IRQ/NMI to it; `$2209` (SCNT) is the mirror direction
(with S-CPU vector redirection selects). Enable/clear register pairs
(`$2201/$2202` S-CPU side, `$220A/$220B` SA-1 side) manage the five interrupt
sources; `updateSa1IrqLine()` reduces them to the SA-1 CPU's level-triggered
IRQ line and edge-triggered NMI. The S-CPU-facing IRQ is OR-ed into the main
CPU's IRQ line by the frame loop.

### Arithmetic Unit

`$2251-$2254`: signed 16×16 multiply, 16÷16 divide with floored semantics
(non-negative remainder), or 40-bit multiply-accumulate with overflow flag
(`$2250` selects). Results in `$2306-$230A`. Writing MB high triggers the
operation and clears MB, matching hardware.

### DMA and Character Conversion

Implemented: normal DMA (ROM/BW-RAM/I-RAM → I-RAM or BW-RAM, triggered by the
DDA write matching the destination), **type-2 character conversion** (the
SA-1 CPU fills the BRF register file `$2240-$224F`; each completed half
converts one 8-pixel line to planar bitplanes in I-RAM), and **type-1
character conversion** (while active, S-CPU I-RAM reads are synthesized
on-the-fly from the linear BW-RAM bitmap — `ccDmaRead()` computes the planar
byte from bitmap pixels). Deferred: DMA timing/IRQ granularity, variable-
length bit processing, bitmap-mapped BW-RAM ($2225 bit 7), and the timers.

### Stepping

`stepSa1(masterClocks)` halves master clocks into SA-1 cycles (carrying the
remainder), skips entirely while reset/wait are asserted, and runs whole
instructions against a signed budget like the GSU.

## DSP-1 (NEC uPD7725)

`src/DSP/necdsp.cpp` is a full uPD7725 interpreter:

- 2048 × 24-bit program ROM, 1024 × 16-bit data ROM, 16-bit data RAM
  (masked at `0x7ff`), dual accumulators with independent flag sets, the
  K×L multiplier (recomputed every instruction), a 4-bit stack, and the
  four instruction classes (OP/RT/JP/LD) decoded field-by-field.
- Flag semantics (S0/S1/Z/C/OV0/OV1, including the OV1 sign-latch behavior)
  follow the MAME/uPD7725 references.
- The `loadRom()` dump format is **little-endian**: 2048 × 3 bytes program +
  1024 × 2 bytes data = 8192 bytes (`dsp1b.rom`). A wrong-endian dump
  produces garbage opcodes — this was a real bring-up trap.

### Handshake

The S-CPU talks through DR/SR with the RQM protocol. `readDR()`/`writeDR()`
implement 8-bit and two-step 16-bit transfer modes (SR's DRC/DRS bits);
completing a transfer clears RQM and calls `runToRequest()`, which executes
instructions until the program *blocks at a `JRQM` wait* (bounded at 200 k
instructions). Stopping merely when RQM sets would be wrong — reading an
input word also sets RQM but the program keeps running. `step()` in the frame
loop advances the DSP proportionally but likewise parks at the wait.

### Bus Windows

LoROM carts: banks `$30-$3F`/`$B0-$BF`, `$8000+`, bit `$4000` selects SR.
HiROM carts: banks `$00-$1F`/`$80-$9F`, `$6000-$7FFF`, bit `$1000` selects
SR. SR is read-only from the S-CPU. The dump path is resolved from
`$SNESQUIK_DSP1_ROM` or `tests/roms/dsp1{b,,a}.rom` — **relative to the
working directory**, which matters for the probe (a missing dump leaves the
DSP inert and the game silently misbehaves).

## S-DD1

`src/SDD1/sdd1.cpp`:

- **MMC**: `$4804-$4807` select four 1 MiB ROM blocks into the `$C0-$FF`
  quarters; `readRom()` applies it. Registers `$4800-$4803` are the DMA
  arm/control bytes.
- **DMA interception**: when a general DMA channel armed via `$4801` starts
  an A→B transfer, `runDma()` calls `decompressBegin(srcAddress)` and feeds
  the channel from `decompressReadByte()` instead of memory; the arm bit
  clears after one transfer.
- **Decoder**: the real S-DD1 algorithm — a 33-state probability-evolution
  machine over 32 contexts (selected from per-bitplane history bits per the
  header's context-model select), Golomb run-length bit generators of order
  0-7 (`kRunTable`), and three output layouts (2/4/8bpp planar pairs and the
  chunky mode-7 layout). It decompresses directly from mapped ROM as the DMA
  pulls bytes.

The NMI-latency heuristic in the frame loop was originally exposed by SFA2's
S-DD1 usage (its main loop polls `$4210`).

## Save States

`src/STATE/savestate.cpp` provides whole-machine snapshots.

### File Format

```text
magic:   "SQST" (u32 LE)
version: u32, currently 1
cpuSize: u32   + CPU::SaveState bytes (raw struct)
ppuSize: u32   + Ppu bytes (raw memcpy of the object)
busSize: u32   + SnesBus section
```

The bus section is written with `appendPod` (raw little-endian struct dumps)
in a fixed order: WRAM, the MMIO array, open bus, WRAM port pointer, mul/div
latches, NMI/IRQ/joypad state, HDMA channel state, pending DMA dots — then
length-prefixed sub-blobs for cartridge SRAM, the APU (via `snes_spc`'s
`copy_state` plus the wrapper's `spcTime`/`spcTimeAccum`), and the GSU, SA-1,
DSP, and S-DD1 (each zero-length when the chip is absent; loaders accept
older states that end early, so newly appended sections stay
backward-compatible).

After the bus loads, `Apu::resyncCpuBaseline(cpu.totalCycles())` re-anchors
the APU sync baseline — it is intentionally not serialized.

### Properties and Caveats

- States are **build-tied**: the PPU section is a raw object image and the
  CPU section a raw struct, so any layout change invalidates existing
  `.state` files. Loaders reject size mismatches with a "different build?"
  error rather than misreading.
- Save is `F6`, load is `F7` in the GUI (path = `<rom>.state`); the probe can
  save at a chosen frame (`--save-state N path`) and resume (`--load-state`).
  This save-in-GUI / resume-in-probe round trip is the project's most
  effective debugging tool: a human plays to the bug, then the probe replays
  from that point deterministically.

## Battery SRAM

- Path: `<rom path>.srm`, loaded at startup into either the cartridge SRAM
  or — for SA-1 carts — BW-RAM (`bus.getSa1().bwRam()`).
- Write-back is **checksum-debounced**: every frame `main()` FNV-1a hashes
  the save RAM; a change marks it dirty and resets an idle counter, and the
  file is flushed after ~2 s (120 frames) of no further changes, plus once at
  exit. Hashing was chosen over write-path instrumentation because SA-1
  BW-RAM is written by the S-CPU, the SA-1 CPU, SA-1 DMA, and character
  conversion — one content hash catches all of them.
- SRAM is *also* inside save states (the bus section), which restores save
  files when loading a state. The `.srm` file remains the persistent copy.

## Frontend

### SDL/OpenGL Renderer

`SdlGlRenderer` (`src/MAIN/sdl_gl_renderer.cpp`) opens an SDL2 window with a
GL 3.3 core context, loads the handful of needed GL entry points via
`SDL_GL_GetProcAddress`, and draws the PPU framebuffer as a nearest-neighbor
`GL_BGRA` texture on a fullscreen quad (the PPU writes `0xAARRGGBB` pixels,
which is BGRA byte order on little-endian hosts). Swap interval is adaptive
vsync (`-1`) with a fallback to immediate (`0`) — the frame limiter in
`main()` owns pacing either way.

### Input

`pollEvents()` translates keyboard and SDL GameController events into the key
callback. Controllers are opened on `SDL_CONTROLLERDEVICEADDED` (SDL fires it
for already-attached pads at init) and closed on removal. Gamepad buttons are
mapped by synthesizing the equivalent keyboard keycode, so one callback path
serves both.

| Key | SNES button | | Key | Function |
| --- | --- | --- | --- | --- |
| `Z` / pad B | B | | `F1` | toggle color math |
| `X` / pad A | A | | `F2` | toggle windows |
| `A` / pad Y | Y | | `F3` | BG3-only view |
| `S` / pad X | X | | `F4` | force BG3 |
| `Q`/`W` / shoulders | L / R | | `F5` | toggle logging → `snesquik_log.txt` |
| Enter / Start | Start | | `F6` / `F7` | save / load state |
| Backspace / Back | Select | | Esc | quit |
| Arrows / D-pad | D-pad | | | |

### Audio Output and Dynamic Rate Control

Audio is queued to SDL (`AUDIO_S16SYS`, 32 kHz stereo request; the device may
convert). The queue is primed with ~100 ms of silence, then held near a
100 ms target by a **PI controller**:

- The measured queue depth (converted to seconds using the *actual* device
  format) is EMA-smoothed (α = 0.05) because raw depth jitters by a whole
  audio callback chunk.
- The proportional term is ±1%; the integral (`rateBias`, ±2%, slow
  accumulation) learns the device's true clock error — measured +0.6% on
  PipeWire here, larger than the proportional band.
- The applied ratio is slew-limited (±0.0005/frame) and passes samples
  through **bit-exact** inside a ±0.05% deadband; outside it, a linear
  resampler stretches the frame.
- A queue above 250 ms drops the frame's audio (counted as an overrun);
  an empty queue counts an underrun. Both appear in the window title and
  stderr. `SNESQUIK_AUDIO_STATS=1` prints per-second stats even when clean;
  `SNESQUIK_AUDIO_DUMP=path` tees the exact queued bytes (s16le stereo
  32 kHz) to a file.

The lesson encoded here: when audio crackles, *measure the production rate
first* — the historic crackle was the APU producing ~30 k samples/s instead
of 32 k (missing DMA time), not an output-side problem.

### Frame State Logging

F5 (or `--log-from N`) opens `snesquik_log.txt` and dumps a per-frame
register panorama: CPU registers, INIDISP/BGMODE/TM/TS/window/CGWSEL state,
scroll and tilemap bases, NMI/IRQ timer state, OBJSEL, Mode 7 registers,
VRAM/CGRAM excerpts, and APU port traffic counts. The `MainTraceListener`
additionally logs interesting MMIO writes, DMA transfers to VRAM-ish B-bus
targets, HDMA writes, and vblank edges.

## Headless Probe

`snesquik <rom> --probe <outdir> [options]` runs the machine without SDL —
deterministically and fast. It is the primary debugging instrument; GUI
sessions are **not** deterministic (input timing varies), and several past
"bugs" turned out to be GUI nondeterminism artifacts. Repro claims should
come from probe runs.

| Option | Effect |
| --- | --- |
| `--frames N` | Number of frames to run (default 60) |
| `--snapshot-every K` | Write `frame_XXXX.png` every K frames + a `summary.txt` status line |
| `--trace-steps N` | Log the first N CPU instructions to `cpu.log` |
| `--probe-tap BTN F` | Press button for 10 frames starting at frame F |
| `--probe-hold BTN F1 F2` | Press at F1, release at F2 |
| `--probe-press/-release BTN F` | Raw edge control |
| `--probe-poke F ADDR VAL` | Write a WRAM byte at frame F (hex addr/val) |
| `--load-state PATH` | Resume from a save state before frame 0 |
| `--save-state F PATH` | Save a state at frame F |
| `--dump-state F` | At frame F dump `vram/cgram/oam/wram/spcram/gsuram.bin`, `ppu_regs.txt`, `gsu_regs.txt`; MMIO logging is gated to the 2500 frames before F |
| `--dump-audio PATH` | Capture the APU stream to a WAV (s16 stereo 32 kHz) |
| `--gsu-trace F N` | Trace N GSU instructions starting at frame F to `gsu_trace.log` |

Always-written artifacts: `mmio.log` (trace-listener events, capped at 20 M
lines), `cpu.log`, `summary.txt`. The per-snapshot summary line is dense on
purpose — CPU PC/P, APU port totals and SPC PC/error/ports, joypad word,
INIDISP/BGMODE/TM, NMI/IRQ timer state, GSU GO/PC/SFR/SCMR/instruction count,
SA-1 PC/cycles, DSP PC/SR/DR — one greppable line per snapshot that answers
"what is every processor doing right now."

The PNGs are minimal but standard (stored-deflate), so any image tool — or a
20-line Python row-hasher — can post-process them.

### Regression Workflow

The proven way to validate a change: build a pristine baseline (`git worktree
add`, copy the `snes_spc` submodule directory in, build), run identical
probes on both builds across a ROM matrix that covers every chip —
`smw` (LoROM), `sm` (HiROM), `sf`/`sf2`/`doom` (GSU at both clock selects),
`kdl3` (SA-1), `smk` (DSP-1, set `SNESQUIK_DSP1_ROM` absolutely!), `sfa2`
(S-DD1) — and `cmp` the frame PNGs and summaries. Behavior-preserving changes
come out bit-identical; anything else needs eyeballing.

## Startup-to-Frame Flow

1. User runs `./snesquik game.sfc`.
2. ROM file is read; copier header stripped; LoROM/HiROM/ExHiROM header
   candidates scored; best mapping chosen.
3. `Ppu`, `SnesBus`, coprocessors, and `CPU` are constructed and wired.
4. Battery `.srm` (if any) is loaded into SRAM or SA-1 BW-RAM.
5. `cpu.reset()` fetches `$00:FFFC` through the bus → the ROM's reset vector.
6. Renderer and audio device open; the frame loop starts.
7. The game initializes: uploads its sound driver through `$2140-$2143`
   (the IPL handshake — each port access runs the SPC to that exact cycle),
   uploads graphics through `$2118/$2119` (usually via DMA), configures
   the PPU, enables NMI.
8. Per frame: HDMA reinitializes, instructions execute, DMA halt time is
   accounted to every clock domain, scanlines render into line buffers and
   compose into the framebuffer, HDMA fires per line, vblank raises the NMI
   (with the 4-instruction arm delay), auto-read latches the joypad.
9. `endApuFrame()` extracts ~534 stereo samples; dynamic rate control queues
   them to SDL.
10. The framebuffer is uploaded as a texture and presented; the limiter paces
    to 60.0988 Hz.

## Testing

Eight CTest suites, each a standalone executable over its library:

| Suite | Covers |
| --- | --- |
| `cpu_r5a22_instruction_tests` | 65816 instruction semantics, flags, addressing, modes |
| `snes_bus_tests` | Memory map, MMIO, DMA/HDMA, IRQ timing, joypad, savestate round-trip |
| `rom_parser_tests` | Header scoring, mappings, chipset detection |
| `ppu_tests` | Register semantics, rendering, priorities, windows, color math, Mode 7 |
| `gsu_tests` | GSU instructions, buffers, plot pipeline |
| `sa1_tests` | SA-1 memory map, messaging, arithmetic, conversion DMA |
| `dsp_tests` | uPD7725 execution and the DR/SR handshake |
| `sdd1_tests` | MMC and decompression against known vectors |

All run with the repo root as working directory (some use `tests/roms/`
fixtures; real game ROMs there are gitignored). System-level verification is
the probe: boot matrices, gameplay resumes from committed `.state` files, and
the [worktree diff workflow](#regression-workflow).

## Current Architectural Caveats

Useful to know before extending the emulator:

- **Timing is instruction-granular, not cycle-accurate.** One CPU cycle is a
  flat 6 master clocks: no FastROM, no per-region access costs, DMA at a flat
  8 master clocks/byte. Games depending on exact cycle counts will drift.
- **The PPU is scanline-based.** Mid-scanline register writes affect the
  whole line; sprite evaluation is approximated at line granularity; there is
  no dot renderer. Hires modes (512-wide 5/6 pseudo-hires) and interlace are
  not rendered. `$4212`'s hblank edge is at dot 262, not the hardware's ~274.
- **The NMI 4-instruction arm delay and the 704-cycle auto-read duration are
  heuristics** tuned for compatibility, not measured hardware latencies.
  Mul/div results are instant (hardware takes 8/16 cycles).
- **Save states are build-tied** (raw struct/object dumps, no versioned field
  encoding). Loaders fail cleanly on size mismatch but cannot migrate.
- **The frame loop exists twice** (GUI + probe) and must be edited in both
  places.
- **SA-1 is a foundation**: timers, variable-length bit processing, bitmap
  BW-RAM, and DMA timing are deferred. DSP-2/3/4 share the DSP-1 plumbing but
  need their own dumps and are unwired. Other chips (CX4, SPC7110, ST01x,
  OBC-1) are unimplemented; the loader warns.
- **The DSP-1 needs its ROM dump at a cwd-relative path** unless
  `SNESQUIK_DSP1_ROM` is set — headless runs from other directories silently
  get an inert DSP.
- **The `snes_spc` submodule has no `.gitmodules` URL** and carries local
  modifications (debug accessors); treat it as vendored source.
- Controller 2 is not emulated (`$4017` reads 0).
- The GSU cost model is calibrated against bsnes's constants rather than
  per-instruction hardware measurements; CLSR semantics are correct but the
  absolute figures are approximations.

## Where to Start When Adding a Feature

For a CPU behavior:

1. Semantics live in `src/CPU_R5A22/operations.cpp`; addressing/penalties in
   `addressing_modes.cpp`; the table entry (base cycles/bytes) in
   `operations.cpp`'s `opcodeTable()`.
2. Add cases to `tests/cpu_r5a22_instruction_tests.cpp`.
3. Remember `normalizeEmulationRegisters()` runs after every instruction —
   don't duplicate its invariants.

For a memory-mapped register:

1. Decide the owner: PPU register → `Ppu::read/writeRegister`; 5A22 register
   → the MMIO dispatch in `SnesBus::read8/write8`; chip register → that
   chip's `*MapRead/Write` + its own decode.
2. If DMA can touch it, mirror the behavior in `readRaw/writeRaw`
   (or confirm it must *not* have the CPU-path side effects there).
3. Add bus tests.

For PPU behavior:

1. Add focused tests in `tests/ppu_tests.cpp`; keep rendering CPU-side.
2. For visual verification, use the probe's PNG snapshots and the
   [regression workflow](#regression-workflow) — pixel diffs catch more than
   eyes do.
3. Window masking belongs in `composeFromBuffers`, not in the line
   renderers (the buffers are shared by main and sub screens).

For a new coprocessor:

1. Follow the existing pattern: own module/library, `power()`,
   `attachRom(std::span)`, `step*(units)`, `saveState/loadState`, and a
   `*MapRead/Write` pair in `SnesBus` gated by a `*Present` flag.
2. Add detection to `RomHeader`, attachment to *both* composition roots
   (`main()` and `runProbe()`), stepping to *both* frame loops, and a
   length-prefixed savestate section (append-only for compatibility).
3. Implement from hardware documentation, not by porting another emulator's
   code — that is a standing project rule.

For timing changes:

1. Re-read [Timing Model and Units](#timing-model-and-units) first; every
   `step*` call has specific units.
2. Change both frame loops.
3. Run the probe diff matrix before and after — bit-identical PNGs for games
   the change shouldn't affect is the acceptance bar.

For save-state changes:

1. Append new sections with a `u32` length prefix so older states still load
   (the bus loader treats early EOF as "older state").
2. Struct layout changes to `CPU::SaveState` or `Ppu` invalidate all states —
   acceptable, but say so in the commit message.

## Quick Source Map

| Area | Files |
| --- | --- |
| Composition root / GUI loop | `src/MAIN/snesquik.cpp` |
| SDL/OpenGL presenter + input | `src/MAIN/sdl_gl_renderer.h/.cpp` |
| Bus, DMA/HDMA, MMIO, cartridge mapping | `src/BUS/bus.h`, `src/BUS/bus.cpp` |
| ROM/header parsing | `src/CART/rom_parser.h/.cpp` |
| 65816 core | `src/CPU_R5A22/core.h/.cpp`, `operations.cpp`, `addressing_modes.cpp` |
| PPU | `src/S-PPU/ppu.h/.cpp` |
| APU wrapper | `src/APU_SPC700/apu.h/.cpp` |
| SPC700+DSP core (vendored submodule) | `src/APU_SPC700/snes_spc/snes_spc/` |
| Super FX | `src/GSU/gsu.h/.cpp`, `gsu_operations.cpp`, `gsu_table.cpp` |
| SA-1 | `src/SA1/sa1.h/.cpp` |
| DSP-1 (uPD7725) | `src/DSP/necdsp.h/.cpp` |
| S-DD1 | `src/SDD1/sdd1.h/.cpp` |
| Save states | `src/STATE/savestate.h/.cpp` |
| Headless probe | `src/DEBUG/probe.h/.cpp` |
| Tests | `tests/*.cpp`, fixtures in `tests/roms/` |
