cmake_minimum_required(VERSION 3.20)
project(auroraOS C CXX ASM)

set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -ffreestanding -fno-builtin -fno-common -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS} -O2 -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPU_FLAGS} -fno-exceptions -fno-rtti -fno-threadsafe-statics -O2 -g")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CPU_FLAGS}")

file(GLOB LWIP_SOURCES 
    "3rdparty/lwip/src/api/*.c"
    "3rdparty/lwip/src/core/*.c"
    "3rdparty/lwip/src/core/ipv4/*.c"
    "3rdparty/lwip/src/netif/ethernet.c"
)

set(SOURCES
    boot/boot.S
    boot/uart.c
    boot/interrupts.cpp
    kernel/memory.cpp
    kernel/libc.cpp
    vfs/vfs.cpp
    vfs/ramfs.cpp
    apps/shell.cpp
    apps/elf_loader.cpp
    apps/kernel.cpp
    net/eth_driver.cpp
    adapter/net/sys_arch.cpp
    adapter/net/ethernetif.cpp
    ${LWIP_SOURCES}
)

add_executable(auroraOS.elf ${SOURCES}
    kernel/task.hpp)

target_include_directories(auroraOS.elf PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/config
    ${CMAKE_SOURCE_DIR}/boot
    ${CMAKE_SOURCE_DIR}/kernel
    ${CMAKE_SOURCE_DIR}/syscall
    ${CMAKE_SOURCE_DIR}/vfs
    ${CMAKE_SOURCE_DIR}/apps
    ${CMAKE_SOURCE_DIR}/net
    ${CMAKE_SOURCE_DIR}/adapter/net
    ${CMAKE_SOURCE_DIR}/3rdparty/lwip/src/include
)

target_link_options(auroraOS.elf PRIVATE
    -T ${CMAKE_SOURCE_DIR}/config/linker.ld
    -nostartfiles
    -nostdlib
    --specs=nosys.specs
)