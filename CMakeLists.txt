cmake_minimum_required(VERSION 3.20)

project(snesquik LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SNESQUIK_BUILD_RENDERER "Build SDL2/OpenGL renderer target" ON)
option(SNESQUIK_BUILD_TESTS "Build test executables" ON)

add_library(snesquik_cpu
    src/CPU_R5A22/addressing_modes.cpp
    src/CPU_R5A22/core.cpp
    src/CPU_R5A22/operations.cpp
)
target_include_directories(snesquik_cpu PUBLIC src)

add_library(snesquik_ppu
    src/S-PPU/ppu.cpp
)
target_include_directories(snesquik_ppu PUBLIC src)

add_library(snesquik_apu
    src/APU_SPC700/apu.cpp
    src/APU_SPC700/snes_spc/snes_spc/SNES_SPC.cpp
    src/APU_SPC700/snes_spc/snes_spc/SNES_SPC_misc.cpp
    src/APU_SPC700/snes_spc/snes_spc/SNES_SPC_state.cpp
    src/APU_SPC700/snes_spc/snes_spc/SPC_DSP.cpp
    src/APU_SPC700/snes_spc/snes_spc/SPC_Filter.cpp
    src/APU_SPC700/snes_spc/snes_spc/spc.cpp
    src/APU_SPC700/snes_spc/snes_spc/dsp.cpp
)
target_include_directories(snesquik_apu PUBLIC src src/APU_SPC700/snes_spc)
target_compile_definitions(snesquik_apu PRIVATE HAVE_STDINT_H)

add_library(snesquik_gsu
    src/GSU/gsu.cpp
    src/GSU/gsu_operations.cpp
    src/GSU/gsu_table.cpp
)
target_include_directories(snesquik_gsu PUBLIC src)

add_library(snesquik_sa1
    src/SA1/sa1.cpp
)
target_include_directories(snesquik_sa1 PUBLIC src)
target_link_libraries(snesquik_sa1 PUBLIC snesquik_cpu)

add_library(snesquik_dsp
    src/DSP/necdsp.cpp
)
target_include_directories(snesquik_dsp PUBLIC src)

add_library(snesquik_sdd1
    src/SDD1/sdd1.cpp
)
target_include_directories(snesquik_sdd1 PUBLIC src)

add_library(snesquik_bus
    src/BUS/bus.cpp
    src/STATE/savestate.cpp
)
target_include_directories(snesquik_bus PUBLIC src)
target_link_libraries(snesquik_bus PUBLIC snesquik_cpu snesquik_ppu snesquik_apu snesquik_gsu snesquik_sa1 snesquik_dsp snesquik_sdd1)

add_library(snesquik_cartridge
    src/CART/rom_parser.cpp
)
target_include_directories(snesquik_cartridge PUBLIC src)
target_link_libraries(snesquik_cartridge PUBLIC snesquik_bus)

add_library(snesquik_debug
    src/DEBUG/probe.cpp
)
target_include_directories(snesquik_debug PUBLIC src)
target_link_libraries(snesquik_debug PUBLIC snesquik_cartridge snesquik_cpu snesquik_ppu)

if (SNESQUIK_BUILD_RENDERER)
    find_package(SDL2)
    find_package(OpenGL)
    if (SDL2_FOUND AND OpenGL_FOUND)
        add_library(snesquik_renderer
            src/MAIN/sdl_gl_renderer.cpp
        )
        target_include_directories(snesquik_renderer PUBLIC src ${SDL2_INCLUDE_DIRS})
        target_link_libraries(snesquik_renderer PUBLIC ${SDL2_LIBRARIES} OpenGL::GL)

        add_executable(snesquik src/MAIN/snesquik.cpp)
        target_link_libraries(snesquik PRIVATE snesquik_renderer snesquik_debug snesquik_cartridge snesquik_cpu)
    else()
        message(STATUS "SDL2 or OpenGL not found; skipping snesquik_renderer")
    endif()
endif()

if (SNESQUIK_BUILD_TESTS)
    enable_testing()

    add_executable(cpu_r5a22_instruction_tests tests/cpu_r5a22_instruction_tests.cpp)
    target_link_libraries(cpu_r5a22_instruction_tests PRIVATE snesquik_cpu)
    add_test(NAME cpu_r5a22_instruction_tests COMMAND cpu_r5a22_instruction_tests)
    set_tests_properties(cpu_r5a22_instruction_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(snes_bus_tests tests/snes_bus_tests.cpp)
    target_link_libraries(snes_bus_tests PRIVATE snesquik_bus)
    add_test(NAME snes_bus_tests COMMAND snes_bus_tests)
    set_tests_properties(snes_bus_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(rom_parser_tests tests/rom_parser_tests.cpp)
    target_link_libraries(rom_parser_tests PRIVATE snesquik_cartridge snesquik_cpu)
    add_test(NAME rom_parser_tests COMMAND rom_parser_tests)
    set_tests_properties(rom_parser_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(gsu_tests tests/gsu_tests.cpp)
    target_link_libraries(gsu_tests PRIVATE snesquik_gsu)
    add_test(NAME gsu_tests COMMAND gsu_tests)
    set_tests_properties(gsu_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(sa1_tests tests/sa1_tests.cpp)
    target_link_libraries(sa1_tests PRIVATE snesquik_sa1)
    add_test(NAME sa1_tests COMMAND sa1_tests)
    set_tests_properties(sa1_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(dsp_tests tests/dsp_tests.cpp)
    target_link_libraries(dsp_tests PRIVATE snesquik_dsp)
    add_test(NAME dsp_tests COMMAND dsp_tests)
    set_tests_properties(dsp_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(ppu_tests tests/ppu_tests.cpp)
    target_link_libraries(ppu_tests PRIVATE snesquik_bus snesquik_ppu)
    add_test(NAME ppu_tests COMMAND ppu_tests)
    set_tests_properties(ppu_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(sdd1_tests tests/sdd1_tests.cpp)
    target_link_libraries(sdd1_tests PRIVATE snesquik_sdd1)
    add_test(NAME sdd1_tests COMMAND sdd1_tests)
    set_tests_properties(sdd1_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
