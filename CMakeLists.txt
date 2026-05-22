cmake_minimum_required(VERSION 3.12)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(FRANKLIN_TARGET_RP2350 "Build Franklin for RP2350" OFF)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk" CACHE PATH "Path to the Pico SDK")
set(PICO_SDK_PATH_FALLBACK "/usr/share/pico-sdk")

if(FRANKLIN_TARGET_RP2350)
    set(PICO_PLATFORM rp2350-arm-s)
    set(PICO_BOARD pico2)
    if(EXISTS "${PICO_SDK_PATH}/cmake/preload/platforms/rp2350-arm-s.cmake" OR EXISTS "${PICO_SDK_PATH}/cmake/preload/platforms/rp2350.cmake")
        message(STATUS "Using local RP2350 Pico SDK: ${PICO_SDK_PATH}")
    elseif(EXISTS "${PICO_SDK_PATH_FALLBACK}/cmake/preload/platforms/rp2350-arm-s.cmake" OR EXISTS "${PICO_SDK_PATH_FALLBACK}/cmake/preload/platforms/rp2350.cmake")
        set(PICO_SDK_PATH "${PICO_SDK_PATH_FALLBACK}")
        message(STATUS "Using system RP2350 Pico SDK: ${PICO_SDK_PATH}")
    else()
        message(FATAL_ERROR "No RP2350 Pico SDK found. Expected ${PICO_SDK_PATH}/cmake/preload/platforms/rp2350-arm-s.cmake or rp2350.cmake, or ${PICO_SDK_PATH_FALLBACK}/cmake/preload/platforms/rp2350-arm-s.cmake or rp2350.cmake")
    endif()
else()
    if(NOT EXISTS "${PICO_SDK_PATH}/cmake/pico_pre_load_platform.cmake")
        if(EXISTS "${PICO_SDK_PATH_FALLBACK}/cmake/pico_pre_load_platform.cmake")
            set(PICO_SDK_PATH "${PICO_SDK_PATH_FALLBACK}")
            message(STATUS "Using system Pico SDK: ${PICO_SDK_PATH}")
        else()
            message(FATAL_ERROR "No Pico SDK found. Expected ${PICO_SDK_PATH} or ${PICO_SDK_PATH_FALLBACK}")
        endif()
    else()
        message(STATUS "Using local Pico SDK: ${PICO_SDK_PATH}")
    endif()
    set(PICO_BOARD olimex_rp2040pc)
endif()

set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)
message(STATUS "PICO_SDK_PATH=${PICO_SDK_PATH}")

include(pico_sdk_import.cmake)
project(rp2040 C CXX ASM)

pico_sdk_init()

add_compile_options(-Wall)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/PicoDVI/software/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_subdirectory(lib/PicoDVI/software/libdvi)
add_subdirectory(systems)
