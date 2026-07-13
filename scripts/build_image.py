import sys
import struct
import zlib

FIRMWARE_MAGIC = 0x41555241
VALID_STATUS = 0x12345678

def build_image(bootloader_path, app_path, output_path):
    with open(app_path, 'rb') as f:
        app_data = f.read()

    with open(bootloader_path, 'rb') as f:
        bootloader_data = f.read()

    # Pad bootloader to 32KB
    if len(bootloader_data) > 32768:
        print("ERROR: Bootloader is larger than 32KB!")
        sys.exit(1)
    
    padded_bootloader = bootloader_data.ljust(32768, b'\xFF')

    # Our C++ implementation computes standard CRC32.
    crc = zlib.crc32(app_data) & 0xFFFFFFFF

    version = 1
    image_size = len(app_data)
    
    header = struct.pack('<IIIIIIII', 
        FIRMWARE_MAGIC, 
        version, 
        image_size, 
        crc, 
        VALID_STATUS, 
        0, 0, 0
    )

    # Pad header to 256 bytes for VTOR alignment
    padded_header = header.ljust(256, b'\xFF')

    # Concatenate: Bootloader (32K) + Header (256B) + App
    flash_data = padded_bootloader + padded_header + app_data

    with open(output_path, 'wb') as f:
        f.write(flash_data)
    
    print(f"Success! Generated {output_path} (Size: {len(flash_data)} bytes)")
    print(f"App size: {image_size} bytes, CRC32: 0x{crc:08X}")

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: python build_image.py <bootloader.bin> <auroraOS.bin> <flash.bin>")
        sys.exit(1)
    build_image(sys.argv[1], sys.argv[2], sys.argv[3])
