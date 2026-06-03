import os
import cairosvg
from PIL import Image
import struct

def convert_svg_to_rgb565_bin(svg_path, bin_path, width, height):
    print(f"Rasterizing {svg_path}...")
    png_data = cairosvg.svg2png(url=svg_path, output_width=width, output_height=height)
    
    import io
    img = Image.open(io.BytesIO(png_data)).convert('RGB')
    img = img.resize((width, height), Image.LANCZOS)
    
    print(f"Converting to RGB565 {bin_path}...")
    with open(bin_path, 'wb') as f:
        for y in range(height):
            for x in range(width):
                r, g, b = img.getpixel((x, y))
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)
                # Pack as little-endian unsigned short
                f.write(struct.pack('<H', rgb565))
    print(f"Saved {bin_path}")

def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out_dir = os.path.join(base_dir, 'components', 'sc_ui', 'assets')
    os.makedirs(out_dir, exist_ok=True)
    
    # Drake
    drake_svg = os.path.join(base_dir, 'data', 'art', 'style_drake_military', 'mockup_pilot_mfd.svg')
    drake_bin = os.path.join(out_dir, 'mockup_drake.bin')
    convert_svg_to_rgb565_bin(drake_svg, drake_bin, 800, 1280)
    
    # Origin
    origin_svg = os.path.join(base_dir, 'data', 'art', 'style_origin_lux', 'mockup_nav_cruise.svg')
    origin_bin = os.path.join(out_dir, 'mockup_origin.bin')
    convert_svg_to_rgb565_bin(origin_svg, origin_bin, 800, 1280)

if __name__ == '__main__':
    main()
