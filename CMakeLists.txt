cmake_minimum_required(VERSION 3.20)
project(auroraOS C CXX ASM)

set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -ffreestanding -fno-builtin -fno-common -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS} -O2 -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPU_FLAGS} -fno-exceptions -fno-rtti -fno-threadsafe-statics -O2 -g")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CPU_FLAGS}")

set(SOURCES
    boot.S
    uart.c
    kernel.cpp
    interrupts.cpp
    memory.cpp
    softbus.cpp
    vfs.cpp
    ramfs.cpp
    shell.cpp
    elf_loader.cpp
)

add_executable(auroraOS.elf ${SOURCES}
    task.hpp)

target_include_directories(auroraOS.elf PRIVATE ${CMAKE_SOURCE_DIR})
