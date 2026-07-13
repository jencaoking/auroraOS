#!/bin/bash
qemu-system-arm -M lm3s6965evb -cpu cortex-m4 -nographic -icount shift=3,align=on -rtc clock=vm "$@"
