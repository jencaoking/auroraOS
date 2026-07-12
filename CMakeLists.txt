cmake_minimum_required(VERSION 3.20)
project(auroraOS C CXX ASM)

# =====================================================================
# 0. Kconfig Integration
# =====================================================================
execute_process(
    COMMAND python ${CMAKE_SOURCE_DIR}/scripts/genconfig.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE GENCONFIG_RESULT
)
if(NOT GENCONFIG_RESULT EQUAL 0)
    message(FATAL_ERROR "genconfig.py failed! Please ensure kconfiglib is installed and Kconfig exists.")
endif()

include(${CMAKE_SOURCE_DIR}/config/autoconf.cmake)

# =====================================================================
# 1. 动态板级与架构自动映射
# =====================================================================
if(DEFINED CONFIG_BOARD)
    set(BOARD ${CONFIG_BOARD})
else()
    set(BOARD "lm3s6965-qb" CACHE STRING "Target Hardware Board")
endif()

if(BOARD STREQUAL "lm3s6965-qb")
    set(BOARD_DIR "ti/lm3s6965-qb")
    set(ARCH_DIR  "arm/cortex-m/cm4")
    set(QEMU_MACH "lm3s6965evb")
else()
    message(FATAL_ERROR "Unsupported BOARD: ${BOARD}! Please check boards/ directory.")
endif()

message(STATUS "---------------------------------------------")
message(STATUS "  auroraOS Build Configuration")
message(STATUS "  Target Board : ${BOARD} (${BOARD_DIR})")
message(STATUS "  Target Arch  : ${ARCH_DIR}")
message(STATUS "---------------------------------------------")

set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -ffreestanding -fno-builtin -fno-common -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS} -O2 -g -include autoconf.h -DLUA_32BITS=1")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CPU_FLAGS} -fno-exceptions -fno-rtti -fno-threadsafe-statics -O2 -g -include autoconf.h -DLUA_32BITS=1")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${CPU_FLAGS}")

set(SOURCES
    boot/boot.S
    boot/uart.c
    boot/interrupts.cpp
    kernel/memory.cpp
    kernel/libc.cpp
    kernel/posix.cpp
    apps/shell.cpp
    apps/elf_loader.cpp
    apps/kernel.cpp
    apps/net_app.cpp
)

if(CONFIG_VFS)
    list(APPEND SOURCES vfs/vfs.cpp)
    if(CONFIG_FS_RAMFS)
        list(APPEND SOURCES vfs/ramfs.cpp)
    endif()
    # Assume LittleFS is used if VFS is enabled for now, or just unconditionally add
    list(APPEND SOURCES 3rdparty/littlefs/lfs.c 3rdparty/littlefs/lfs_util.c)
endif()

if(CONFIG_NETWORKING AND CONFIG_NET_LWIP)
    file(GLOB LWIP_SOURCES 
        "3rdparty/lwip/src/api/*.c"
        "3rdparty/lwip/src/core/*.c"
        "3rdparty/lwip/src/core/ipv4/*.c"
        "3rdparty/lwip/src/netif/ethernet.c"
    )
    list(APPEND SOURCES
        net/eth_driver.cpp
        adapter/net/sys_arch.cpp
        adapter/net/ethernetif.cpp
        ${LWIP_SOURCES}
    )
endif()

file(GLOB LUA_SOURCES "3rdparty/lua/*.c")
list(REMOVE_ITEM LUA_SOURCES 
    "${CMAKE_SOURCE_DIR}/3rdparty/lua/lua.c" 
    "${CMAKE_SOURCE_DIR}/3rdparty/lua/luac.c"
    "${CMAKE_SOURCE_DIR}/3rdparty/lua/loslib.c"
    "${CMAKE_SOURCE_DIR}/3rdparty/lua/liolib.c"
    "${CMAKE_SOURCE_DIR}/3rdparty/lua/lmathlib.c"
    "${CMAKE_SOURCE_DIR}/3rdparty/lua/loadlib.c"
    "${CMAKE_SOURCE_DIR}/3rdparty/lua/lstrlib.c"
    "${CMAKE_SOURCE_DIR}/3rdparty/lua/linit.c"
)
list(APPEND SOURCES ${LUA_SOURCES})

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
    ${CMAKE_SOURCE_DIR}/3rdparty/littlefs
    ${CMAKE_SOURCE_DIR}/arch/${ARCH_DIR}
    ${CMAKE_SOURCE_DIR}/boards/${BOARD_DIR}
)

target_link_options(auroraOS.elf PRIVATE
    -T ${CMAKE_SOURCE_DIR}/config/linker.ld
    -nostartfiles
    -Wl,--gc-sections
    --specs=nano.specs
    --specs=nosys.specs
)

target_compile_definitions(auroraOS.elf PRIVATE LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR LFS_NO_ASSERT)