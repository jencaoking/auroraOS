@echo off
set MY_CMAKE_PATH=J:\CODE\CMAKE\bin
set MY_MINGW_PATH=J:\CODE\MINGW64\bin
set MY_QEMU_PATH=J:\CODE\QEMU
set MY_ARM_PATH=J:\CODE\arm-gnu-toolchain\bin

set PATH=%MY_ARM_PATH%;%MY_CMAKE_PATH%;%MY_MINGW_PATH%;%MY_QEMU_PATH%;%PATH%

echo ====================================================
echo  auroraOS Local Dev Environment Activated
echo  CMake:       %MY_CMAKE_PATH%
echo  MinGW64:     %MY_MINGW_PATH%
echo  QEMU:        %MY_QEMU_PATH%
echo  ARM Tool:    %MY_ARM_PATH%
echo ====================================================
echo.

cmd /k
