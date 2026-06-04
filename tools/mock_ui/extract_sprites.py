import xml.etree.ElementTree as ET
import re
import os
import subprocess
import struct
from PIL import Image

SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace('', SVG_NS)

def png_to_bin(png_file, bin_file, w, h):
    img = Image.open(png_file).convert("RGB")
    pixels = img.load()
    
    with open(bin_file, "wb") as f:
        # lv_image_header_t for LVGL v9
        # magic (0x19), cf (0x12 = RGB565), flags (0)
        # w, h, stride (w*2), reserved (0)
        magic = 0x19
        cf = 0x12
        flags = 0
        stride = w * 2
        header = struct.pack("<BBHHH HH", magic, cf, flags, w, h, stride, 0)
        f.write(header)
        
        for y in range(h):
            for x in range(w):
                r, g, b = pixels[x, y]
                # RGB565 format
                color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                # write little-endian uint16
                f.write(struct.pack("<H", color))

# Dimensions for the 9-slice panel
PANEL_W = 240
PANEL_H = 184
INSET_X = 16
INSET_Y = 16

SLICE_MAP = {
    "TL": (0, 0, INSET_X, INSET_Y),
    "TR": (PANEL_W - INSET_X, 0, INSET_X, INSET_Y),
    "BL": (0, PANEL_H - INSET_Y, INSET_X, INSET_Y),
    "BR": (PANEL_W - INSET_X, PANEL_H - INSET_Y, INSET_X, INSET_Y),
    "EDGE_T": (INSET_X, 0, PANEL_W - 2 * INSET_X, INSET_Y),
    "EDGE_B": (INSET_X, PANEL_H - INSET_Y, PANEL_W - 2 * INSET_X, INSET_Y),
    "EDGE_L": (0, INSET_Y, INSET_X, PANEL_H - 2 * INSET_Y),
    "EDGE_R": (PANEL_W - INSET_X, INSET_Y, INSET_X, PANEL_H - 2 * INSET_Y),
    "CENTER": (INSET_X, INSET_Y, PANEL_W - 2 * INSET_X, PANEL_H - 2 * INSET_Y),
}

def extract_sprites(svg_file, out_dir, theme_prefix):
    tree = ET.parse(svg_file)
    root = tree.getroot()
    defs = root.find(f"{{{SVG_NS}}}defs")
    
    os.makedirs(out_dir, exist_ok=True)
    
    # We want to find ALL texts to get chips AND labels like CENTER
    texts = root.findall(f".//{{{SVG_NS}}}text")
    
    for text_node in texts:
        if not text_node.text: continue
        name = text_node.text.strip().replace(" ", "_").replace("·", "").replace("×", "x").replace("/", "_").replace("__", "_").replace("-", "_").replace(".", "_")
        name = name.replace("(", "").replace(")", "")
        
        valid_prefixes = ["btn_", "tab_", "EDGE_", "CENTER", "TL", "TR", "BL", "BR"]
        if not any(name.startswith(p) for p in valid_prefixes):
            continue
            
        # Ignore things that start with these but are too long/complex, unless it's tab
        if name.startswith("tab_") and "active" in name:
            if "inactive" in name:
                # The label says "tab-active / tab-inactive"
                # They are two separate tabs?
                pass
            
        parent = None
        for g in root.findall(f".//{{{SVG_NS}}}g"):
            if text_node in g:
                parent = g
                break
                
        if parent is None: continue
            
        print(f"Extracting {name}...")
        
        # Clone parent so we can strip out garbage
        import copy
        parent_clone = copy.deepcopy(parent)
        
        # Strip out text and dashed lines
        for elem in list(parent_clone.iter()):
            # If element is text or a dashed line, remove it from its parent
            # It's tricky to remove in ElementTree without a parent pointer, so we do it by rebuilding or finding parents
            pass
            
        # Easier way to strip:
        for p in parent_clone.iter():
            for child in list(p):
                tag = child.tag.split('}')[-1]
                if tag == "text":
                    p.remove(child)
                elif tag == "line" and child.get("stroke-dasharray"):
                    p.remove(child)

        new_svg = ET.Element(f"{{{SVG_NS}}}svg", {})
        if defs is not None:
            new_svg.append(copy.deepcopy(defs))
        new_svg.append(parent_clone)
        
        tmp_svg = os.path.join(out_dir, f"{name}.svg")
        tree_out = ET.ElementTree(new_svg)
        
        # Detect dimensions
        w = None
        h = None
        view_x = 0
        view_y = 0
        
        # First check if it's a known slice
        slice_key = None
        for k in SLICE_MAP:
            if name.startswith(k):
                slice_key = k
                break
                
        if slice_key:
            view_x, view_y, w, h = SLICE_MAP[slice_key]
        else:
            rect = parent_clone.find(f".//{{{SVG_NS}}}rect")
            if rect is not None:
                w = int(float(rect.get("width")))
                h = int(float(rect.get("height")))
            else:
                print(f"WARNING: No rect found for {name}, skipping")
                continue
                
        if "transform" in parent_clone.attrib:
            del parent_clone.attrib["transform"]
            
        new_svg.set("viewBox", f"{view_x} {view_y} {w} {h}")
        new_svg.set("width", str(w))
        new_svg.set("height", str(h))
        
        tree_out.write(tmp_svg)
        
        png_file = os.path.join(out_dir, f"{name}.png")
        bin_file = os.path.join(out_dir, f"{name}.bin")
        
        subprocess.run(["/home/sannis/ESPSCar/tools/mock_ui/venv/bin/cairosvg", tmp_svg, "-o", png_file])
        png_to_bin(png_file, bin_file, w, h)
        print(f" -> Generated {bin_file} (size: {w}x{h})")
        
        os.remove(tmp_svg)
        os.remove(png_file)

if __name__ == "__main__":
    print("Extracting Drake sprites...")
    extract_sprites("art/style_drake_military/sprite_sheet.svg", "data/assets/themes/drake", "drake")
    print("Extracting Origin sprites...")
    extract_sprites("art/style_origin_lux/sprite_sheet.svg", "data/assets/themes/origin", "origin")
