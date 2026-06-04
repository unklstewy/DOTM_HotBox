#!/usr/bin/env python3
"""
pack_sprite_atlas.py — SC-Term sprite atlas packer
===================================================
Rasterizes named sprite groups from a theme SVG and packs them into a single
512×512 RGB565 .bin atlas, plus a C descriptor header.

Usage:
    python3 tools/pack_sprite_atlas.py --theme drake --svg art/style_drake_military/sprite_sheet.svg --out-dir sdcard/assets/themes/drake/

Requirements:
    pip install -r tools/requirements_atlas.txt
    (cairosvg, Pillow, lxml)

SVG convention:
    Each sprite group must have   id="sprite-<name>"
    and a data-rect="x,y,w,h"    attribute giving its bounding box
    inside the SVG canvas (in SVG user units = px at 96 dpi).

    Example:
        <g id="sprite-btn_momentary_idle" data-rect="0,440,140,56">
          ...
        </g>

Output:
    <out-dir>/atlas.bin     — raw RGB565 little-endian, 512×512
    <out-dir>/atlas_debug.png  — visual reference (not shipped to device)

Descriptor header (printed to stdout, redirect to include/):
    tools/pack_sprite_atlas.py ... > components/sc_ui/include/sc_ui_atlas_drake_gen.h

The sprite names are mapped to SC_SPRITE_* enum values defined in
sc_ui_sprites.h — the mapping table is at the bottom of this file.
"""

import argparse
import struct
import sys
import os
from pathlib import Path

try:
    import cairosvg
    from PIL import Image
    import io
except ImportError:
    sys.exit(
        "Missing dependencies. Run:  pip install -r tools/requirements_atlas.txt"
    )

# ── Atlas constants ────────────────────────────────────────────────────────── #
ATLAS_W    = 576
ATLAS_H    = 576
CHROMA_KEY = (0, 0, 1)   # RGB — the transparent sentinel colour (near-black)
SVG_DPI    = 96           # cairosvg rasterization DPI (1 SVG px = 1 CSS px)

# ── Sprite name → SC_SPRITE_* index mapping ───────────────────────────────── #
# Must match sc_ui_sprite_id_t enum ORDER in sc_ui_sprites.h
SPRITE_NAMES = [
    "btn_momentary_idle",
    "btn_momentary_armed",
    "btn_momentary_active",
    "btn_latching_off",
    "btn_latching_on",
    "btn_inactive",
    "btn_danger",
    "slider_track_h",
    "slider_track_v",
    "slider_thumb",
    "axis_joystick_base",
    "axis_joystick_thumb",
    "axis_dpad_base",
    "axis_dpad_up",
    "axis_dpad_down",
    "axis_dpad_left",
    "axis_dpad_right",
    "axis_haat_base",
    "axis_haat_cursor",
    "axis_throttle_track",
    "axis_throttle_grip",
    "axis_yaw_ring",
    "axis_yaw_needle",
    "axis_rudder_track",
    "axis_rudder_pedal",
    "knob_ring",
    "knob_cap",
    "jog_wheel_f0",
    "jog_wheel_f1",
    "jog_wheel_f2",
    "jog_wheel_f3",
    "jog_wheel_f4",
    "jog_wheel_f5",
    "jog_wheel_f6",
    "jog_wheel_f7",
    "panel_tl",
    "panel_tr",
    "panel_bl",
    "panel_br",
    "panel_edge_t",
    "panel_edge_b",
    "panel_edge_l",
    "panel_edge_r",
    "panel_center",
]

# ── Atlas grid layout (x, y, w, h) — mirrors sc_ui_atlas_drake.h ─────────── #
# Sprites are placed at fixed positions; the packer blits each rasterized crop
# to the correct position rather than using a dynamic shelf packer.
ATLAS_LAYOUT = {
    # Row 0: Buttons Row 1 (y: 0..56)
    "btn_momentary_idle":    (  0,   0, 140,  56),
    "btn_momentary_armed":   (140,   0, 140,  56),
    "btn_momentary_active":  (280,   0, 140,  56),
    "btn_danger":            (420,   0, 140,  56),

    # Row 1: Buttons Row 2 (y: 56..112)
    "btn_latching_off":      (  0,  56, 140,  56),
    "btn_latching_on":       (140,  56, 140,  56),
    "btn_inactive":          (280,  56, 140,  56),

    # Row 2: Sliders & Throttle (y: 112..232)
    "slider_track_v":        (  0, 112,  24, 120),
    "axis_throttle_track":   ( 24, 112,  44, 120),
    "slider_track_h":        ( 68, 112, 120,  24),
    "slider_thumb":          (188, 112,  40,  24),
    "axis_throttle_grip":    (228, 112,  60,  20),

    # Row 3: Jog Wheels (y: 232..328)
    "jog_wheel_f0":          (  0, 232,  96,  96),
    "jog_wheel_f1":          ( 96, 232,  96,  96),
    "jog_wheel_f2":          (192, 232,  96,  96),
    "jog_wheel_f3":          (288, 232,  96,  96),
    "jog_wheel_f4":          (384, 232,  96,  96),
    "jog_wheel_f5":          (  0,   0,   0,   0),  # Placeholder
    "jog_wheel_f6":          (  0,   0,   0,   0),  # Placeholder
    "jog_wheel_f7":          (  0,   0,   0,   0),  # Placeholder

    # Row 4: Joystick, D-Pad, HAAT (y: 328..448)
    "axis_joystick_base":    (  0, 328, 120, 120),
    "axis_joystick_thumb":   (120, 328,  40,  40),
    "axis_dpad_base":        (160, 328, 120, 120),
    "axis_dpad_up":          (280, 328,  40,  36),
    "axis_dpad_down":        (320, 328,  40,  36),
    "axis_dpad_left":        (360, 328,  36,  40),
    "axis_dpad_right":       (396, 328,  36,  40),
    "axis_haat_base":        (432, 328, 120, 120),
    "axis_haat_cursor":      (552, 328,  24,  24),

    # Row 5: Yaw, Rudder, Knobs, Panels (y: 448..576)
    "axis_yaw_ring":         (  0, 448, 120, 120),
    "axis_yaw_needle":       (120, 448,  10,  56),
    "axis_rudder_track":     (130, 448, 256,  32),
    "axis_rudder_pedal":     (386, 448,  56,  40),
    "knob_ring":             (442, 448,  64,  64),
    "knob_cap":              (506, 448,  64,  64),

    "panel_tl":              (130, 512,  16,  16),
    "panel_tr":              (146, 512,  16,  16),
    "panel_bl":              (160, 512,  16,  16),
    "panel_br":              (176, 512,  16,  16),
    "panel_edge_t":          (194, 512,  64,   8),
    "panel_edge_b":          (258, 512,  64,   8),
    "panel_edge_l":          (322, 512,   8,  64),
    "panel_edge_r":          (330, 512,   8,  64),
    "panel_center":          (338, 512,  64,  64),
}


def rgb888_to_rgb565(r, g, b):
    """Convert 8-bit RGB to 16-bit RGB565 (little-endian)."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rasterize_svg_crop(svg_bytes, src_x, src_y, src_w, src_h, dst_w, dst_h):
    """
    Rasterize a rectangular crop from an SVG, scaling to dst_w × dst_h.
    Returns a PIL Image in RGBA mode.
    """
    # Rasterize the full SVG first
    png_data = cairosvg.svg2png(bytestring=svg_bytes, dpi=SVG_DPI)
    full_img = Image.open(io.BytesIO(png_data)).convert("RGBA")

    # Crop to sprite region
    crop = full_img.crop((src_x, src_y, src_x + src_w, src_y + src_h))

    # Scale to destination size if different
    if (src_w, src_h) != (dst_w, dst_h) and dst_w > 0 and dst_h > 0:
        crop = crop.resize((dst_w, dst_h), Image.LANCZOS)

    return crop


def build_atlas(svg_path, out_dir, theme_name):
    svg_path = Path(svg_path)
    out_dir  = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    svg_bytes = svg_path.read_bytes()

    # Create blank atlas (filled with chroma-key colour)
    atlas = Image.new("RGBA", (ATLAS_W, ATLAS_H),
                      (CHROMA_KEY[0], CHROMA_KEY[1], CHROMA_KEY[2], 255))

    # ── Parse SVG for sprite data-rect attributes ─────────────────────────── #
    try:
        from lxml import etree
        tree = etree.fromstring(svg_bytes)
        ns   = {"svg": "http://www.w3.org/2000/svg"}
        sprite_rects = {}
        for g in tree.iter():
            gid = g.get("id") or ""
            if gid.startswith("sprite-"):
                name = gid[len("sprite-"):]
                rect_str = g.get("data-rect", "")
                if rect_str:
                    parts = [int(v) for v in rect_str.split(",")]
                    sprite_rects[name] = tuple(parts)  # (x, y, w, h) in SVG px
    except Exception as e:
        print(f"Warning: SVG parse failed ({e}), using layout table only.", file=sys.stderr)
        sprite_rects = {}

    # ── Rasterize and blit each sprite ───────────────────────────────────── #
    placed = {}
    for name, (ax, ay, aw, ah) in ATLAS_LAYOUT.items():
        if aw == 0 or ah == 0:
            placed[name] = (ax, ay, aw, ah)
            continue  # skip placeholder (zero-size) entries

        src_rect = sprite_rects.get(name)
        if src_rect:
            sx, sy, sw, sh = src_rect
            sprite_img = rasterize_svg_crop(svg_bytes, sx, sy, sw, sh, aw, ah)
        else:
            # No id-tagged group found: fill with chroma key (visible as placeholder)
            print(f"  [WARN] No sprite group 'sprite-{name}' in SVG — using placeholder",
                  file=sys.stderr)
            sprite_img = Image.new("RGBA", (aw, ah),
                                   (CHROMA_KEY[0], CHROMA_KEY[1], CHROMA_KEY[2], 255))

        atlas.paste(sprite_img, (ax, ay))
        placed[name] = (ax, ay, aw, ah)
        print(f"  [{name}] → atlas ({ax},{ay}) {aw}×{ah}")

    # ── Save debug PNG ────────────────────────────────────────────────────── #
    debug_path = out_dir / "atlas_debug.png"
    atlas.save(str(debug_path))
    print(f"\nDebug PNG: {debug_path}")

    # ── Convert to RGB565 binary ──────────────────────────────────────────── #
    atlas_rgb = atlas.convert("RGB")
    atlas_data = bytearray()
    for py in range(ATLAS_H):
        for px in range(ATLAS_W):
            r, g, b = atlas_rgb.getpixel((px, py))
            # Clamp chroma key to avoid collision: (0,0,0) → (0,0,1)
            if (r, g, b) == (0, 0, 0):
                b = 1
            word = rgb888_to_rgb565(r, g, b)
            atlas_data += struct.pack("<H", word)

    bin_path = out_dir / "atlas.bin"
    bin_path.write_bytes(atlas_data)
    print(f"Atlas BIN: {bin_path}  ({len(atlas_data)} bytes)")

    return placed


def emit_header(placed, theme_name):
    """Print a C header with the atlas descriptor array to stdout."""
    enum_prefix = "SC_SPRITE_"
    lines = [
        f"/* AUTO-GENERATED by pack_sprite_atlas.py — DO NOT EDIT */",
        f"/* Theme: {theme_name} */",
        f"#pragma once",
        f'#include "sc_ui_sprites.h"',
        f"",
        f"static const sc_ui_sprite_rect_t SC_ATLAS_{theme_name.upper()}_GEN_DESC[SC_SPRITE_COUNT] = {{",
    ]
    for name in SPRITE_NAMES:
        if name in placed:
            x, y, w, h = placed[name]
        else:
            x, y, w, h = 0, 0, 0, 0
        enum_name = f"{enum_prefix}{name.upper()}"
        lines.append(f"    [{enum_name}] = {{ {x:3}, {y:3}, {w:3}, {h:3} }},")
    lines.append("};")
    print("\n".join(lines))


def main():
    ap = argparse.ArgumentParser(description="SC-Term sprite atlas packer")
    ap.add_argument("--svg",     required=True, help="Source SVG file")
    ap.add_argument("--theme",   required=True, help="Theme name (drake|origin)")
    ap.add_argument("--out-dir", required=True, help="Output directory for atlas.bin")
    ap.add_argument("--gen-header", action="store_true",
                    help="Print generated C header to stdout")
    args = ap.parse_args()

    print(f"=== SC-Term Atlas Packer — theme: {args.theme} ===")
    placed = build_atlas(args.svg, args.out_dir, args.theme)

    if args.gen_header:
        emit_header(placed, args.theme)

    print("\nDone.")


if __name__ == "__main__":
    main()
