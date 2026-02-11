# CMakeLists.txt for LibreSD RP2040 Example
#
# Build instructions:
#   1. Create build directory: mkdir build && cd build
#   2. Configure: cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..
#   3. Build: make
#   4. Flash: copy libresd_demo.uf2 to Pico (hold BOOTSEL while connecting)

cmake_minimum_required(VERSION 3.13)

# Include Pico SDK (set PICO_SDK_PATH env var or pass -DPICO_SDK_PATH=...)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(libresd_demo C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# LibreSD source files
set(LIBRESD_SOURCES
    ../../src/libresd_sd.c
    ../../src/libresd_fat.c
    ../../src/libresd_file.c
    ../../src/libresd_shell.c
)

# LibreSD include directories
set(LIBRESD_INCLUDES
    ../../include
)

# Create the executable
add_executable(libresd_demo
    main.c
    libresd_hal_rp2040.c
    ${LIBRESD_SOURCES}
)

# Include directories
target_include_directories(libresd_demo PRIVATE
    ${LIBRESD_INCLUDES}
)

# Link against Pico SDK libraries
target_link_libraries(libresd_demo
    pico_stdlib
    hardware_spi
    hardware_gpio
    hardware_rtc
)

# Enable USB serial output (disable UART)
pico_enable_stdio_usb(libresd_demo 1)
pico_enable_stdio_uart(libresd_demo 0)

# Generate UF2 file for easy flashing
pico_add_extra_outputs(libresd_demo)

# Optional: Enable debug output (uncomment for verbose logging)
# target_compile_definitions(libresd_demo PRIVATE LIBRESD_DEBUG=1)

# Optimization level
target_compile_options(libresd_demo PRIVATE -O2)
