# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(EstacaoMeteorologica C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Include Directories
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
    ${PICO_SDK_PATH}
    ${PICO_SDK_PATH}/src/common/pico_stdlib/include
    ${PICO_SDK_PATH}/src/rp2_common/hardware_gpio/include
    ${PICO_SDK_PATH}/src/rp2040/hardware_structs/include
    ${PICO_SDK_PATH}/src/common/pico_base/include
    ${PICO_SDK_PATH}/src/rp2_common/pico_platform/include
)

# Add executable. Default name is the project name, version 0.1

add_executable(EstacaoMeteorologica EstacaoMeteorologica.c lib/aht20.c lib/bmp280.c lib/matriz.c)

pico_set_program_name(EstacaoMeteorologica "EstacaoMeteorologica")
pico_set_program_version(EstacaoMeteorologica "0.1")

# Generate PIO header
pico_generate_pio_header(EstacaoMeteorologica ${CMAKE_CURRENT_LIST_DIR}/blink.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(EstacaoMeteorologica 0)
pico_enable_stdio_usb(EstacaoMeteorologica 1)

# Add the standard library to the build
target_link_libraries(EstacaoMeteorologica
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(EstacaoMeteorologica PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${PICO_SDK_PATH}/lib/lwip/src/include
        ${PICO_SDK_PATH}/lib/lwip/src/include/arch
        ${PICO_SDK_PATH}/lib/lwip/src/include/lwip
)

include_directories( ${CMAKE_SOURCE_DIR}/lib ) # Inclui os files .h na pasta lib

# Add any user requested libraries
target_link_libraries(EstacaoMeteorologica 
        hardware_i2c
        hardware_pio
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_mqtt
        pico_mbedtls
        pico_lwip_mbedtls
        )

pico_add_extra_outputs(EstacaoMeteorologica)

