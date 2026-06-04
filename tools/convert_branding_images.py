#!/usr/bin/env python3
import sys
import os
import struct
from pathlib import Path
from PIL import Image

def convert_to_lvgl_bin(png_path: Path, bin_path: Path, format_type: str):
    print(f"Converting {png_path.name} to {bin_path.name} ({format_type})...")
    img = Image.open(png_path)
    w, h = img.size
    
    magic = 0x19  # LV_IMAGE_HEADER_MAGIC
    flags = 0
    reserved_2 = 0
    
    if format_type == 'RGB565':
        cf = 0x12  # LV_COLOR_FORMAT_RGB565
        stride = w * 2
        header = struct.pack('<BBHHHHH', magic, cf, flags, w, h, stride, reserved_2)
        img = img.convert('RGB')
        pixels = bytearray()
        for y in range(h):
            for x in range(w):
                r, g, b = img.getpixel((x, y))
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                val = (r5 << 11) | (g6 << 5) | b5
                pixels.extend(struct.pack('<H', val))
    elif format_type == 'ARGB8888':
        cf = 0x10  # LV_COLOR_FORMAT_ARGB8888
        stride = w * 4
        header = struct.pack('<BBHHHHH', magic, cf, flags, w, h, stride, reserved_2)
        img = img.convert('RGBA')
        pixels = bytearray()
        for y in range(h):
            for x in range(w):
                r, g, b, a = img.getpixel((x, y))
                # Blue, Green, Red, Alpha (BGRA) order for LVGL ARGB8888 format
                pixels.extend(struct.pack('<BBBB', b, g, r, a))
    else:
        raise ValueError(f"Unknown format type: {format_type}")
        
    with open(bin_path, 'wb') as f:
        f.write(header)
        f.write(pixels)
    print(f"  Done. Size: {len(header) + len(pixels)} bytes. Dimensions: {w}x{h}")

def main():
    script_dir = Path(__file__).parent.resolve()
    images_dir = script_dir.parent / "data" / "assets" / "images"
    
    conversions = [
        ("splash_base_landscape.png", "splash_base_landscape.bin", "RGB565"),
        ("splash_base_portait.png", "splash_base_portait.bin", "RGB565"),
        ("DanksideLogo.png", "DanksideLogo.bin", "ARGB8888"),
        ("DanksideLogo_50_pct.png", "DanksideLogo_50_pct.bin", "ARGB8888"),
        ("logo_HotBox.png", "logo_HotBox.bin", "ARGB8888"),
        ("favicon.png", "favicon.bin", "ARGB8888"),
    ]
    
    for png_name, bin_name, fmt in conversions:
        png_path = images_dir / png_name
        bin_path = images_dir / bin_name
        if png_path.exists():
            convert_to_lvgl_bin(png_path, bin_path, fmt)
        else:
            print(f"Warning: {png_path} does not exist. Skipping.")

if __name__ == '__main__':
    main()
