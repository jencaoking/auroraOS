FROM ubuntu:22.04
RUN apt-get update && apt-get install -y \
    gcc-arm-none-eabi \
    cmake \
    qemu-system-arm \
    python3 \
    make \
    && rm -rf /var/lib/apt/lists/*
