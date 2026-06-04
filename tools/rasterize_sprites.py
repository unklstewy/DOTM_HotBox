#!/usr/bin/env python3
"""
rasterize_sprites.py — SC-Terminal host-side sprite rasterizer
===============================================================
Crops each named sprite from a theme SVG sheet and saves it as a raw
RGB565 little-endian binary file.  Run this on the host PC; the
resulting .bin files are deployed to the SD card by prep_sdcard.sh and
loaded directly by the firmware without any on-device SVG rendering.

Usage:
    python3 tools/rasterize_sprites.py [--theme drake] [--dry-run] [--all]

Output (written to sdcard/assets/themes/<theme>/sprites/):
    <sprite_name>.bin   — raw RGB565 little-endian, w×h×2 bytes
    sprites_meta.json   — {name: {index, w, h}} lookup used by firmware

Requirements:
    pip install -r tools/requirements_atlas.txt   (cairosvg, Pillow, lxml)

SVG convention (same as pack_sprite_atlas.py):
    Each sprite group must have   id="sprite-<name>"
    and a data-rect="x,y,w,h"    attribute giving its bounding box
    inside the SVG canvas (in SVG user units = px at 96 dpi).
"""

import argparse
import json
import struct
import sys
import io
from pathlib import Path

try:
    import cairosvg
    from PIL import Image
    from lxml import etree
except ImportError:
    sys.exit(
        "Missing dependencies.  Run:  pip install -r tools/requirements_atlas.txt\n"
        "  (needs cairosvg, Pillow, lxml)"
    )

# ── Configuration ──────────────────────────────────────────────────────────── #

SCRIPT_DIR   = Path(__file__).parent.resolve()
PROJECT_ROOT = SCRIPT_DIR.parent

# Sprite name list — ORDER must match sc_ui_sprite_id_t enum in sc_ui_sprites.h
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

# ── Helpers ────────────────────────────────────────────────────────────────── #

def rgba_to_rgb565(r: int, g: int, b: int, a: int) -> int:
    """Convert RGBA8888 to RGB565 with chroma key sentinel."""
    if a < 128:
        return 0x0001  # transparent sentinel
    rv = (r >> 3) & 0x1F
    gv = (g >> 2) & 0x3F
    bv = (b >> 3) & 0x1F
    rgb565 = (rv << 11) | (gv << 5) | bv
    return 0x0002 if rgb565 == 0x0001 else rgb565


def parse_sprite_rects(svg_path: Path) -> dict:
    """
    Parse data-rect attributes from SVG elements with id="sprite-<name>".
    Returns {name: (x, y, w, h)} in SVG user-unit (px) coordinates.
    These are the SOURCE crop coordinates in the SVG canvas — NOT atlas placement coords.
    """
    rects = {}
    try:
        tree = etree.parse(str(svg_path))
        for elem in tree.iter():
            elem_id = elem.get("id") or ""          # guard against None
            if not elem_id.startswith("sprite-"):
                continue
            name = elem_id[7:]                       # strip "sprite-" prefix
            data_rect = elem.get("data-rect") or ""
            if data_rect:
                try:
                    x, y, w, h = [int(v.strip()) for v in data_rect.split(",")]
                    rects[name] = (x, y, w, h)
                except ValueError:
                    print(f"  [!] Malformed data-rect for '{name}': {data_rect}")
    except Exception as e:
        print(f"  [!] SVG parse error: {e}")
    return rects


def rasterize_sprite(svg_bytes: bytes, sx: int, sy: int, sw: int, sh: int) -> tuple | None:
    """
    Render region (sx, sy, sw, sh) of the SVG source canvas to RGB565 binary.
    Returns (rgb565_bytes, w, h) or None on failure.
    """
    if sw == 0 or sh == 0:
        return None

    svg_str = svg_bytes.decode("utf-8", errors="replace")

    # Extract inner SVG body and re-wrap with a cropping viewBox
    start = svg_str.find(">", svg_str.find("<svg")) + 1
    end   = svg_str.rfind("</svg>")
    body  = svg_str[start:end]

    cropped_svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{sw}" height="{sh}" '
        f'viewBox="{sx} {sy} {sw} {sh}">'
        f'{body}</svg>'
    )

    png_bytes = cairosvg.svg2png(
        bytestring=cropped_svg.encode(),
        output_width=sw,
        output_height=sh,
    )
    img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")

    buf = bytearray(sw * sh * 2)
    offset = 0
    for py in range(sh):
        for px in range(sw):
            r, g, b, a = img.getpixel((px, py))
            struct.pack_into("<H", buf, offset, rgba_to_rgb565(r, g, b, a))
            offset += 2

    return bytes(buf), sw, sh


# ── Discovery ──────────────────────────────────────────────────────────────── #

def discover_themes() -> list:
    """Return theme names that have a sprite_sheet.svg under sdcard/assets/themes/."""
    themes_dir = PROJECT_ROOT / "sdcard" / "assets" / "themes"
    if not themes_dir.exists():
        return []
    return sorted(
        d.name for d in themes_dir.iterdir()
        if d.is_dir() and (d / "sprite_sheet.svg").exists()
    )


# ── Main ───────────────────────────────────────────────────────────────────── #

def main():
    available = discover_themes() or ["drake"]

    parser = argparse.ArgumentParser(description="SC-Terminal host sprite rasterizer")
    parser.add_argument("--theme",   default="drake", choices=available,
                        help="Theme name (default: drake)")
    parser.add_argument("--all",     action="store_true",
                        help="Rasterize all discovered themes")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print what would be written without writing any files")
    parser.add_argument("--out-dir", default=None,
                        help="Override output dir (default: sdcard/assets/themes/<theme>/sprites/)")
    args = parser.parse_args()

    themes = available if args.all else [args.theme]

    for theme in themes:
        svg_path = PROJECT_ROOT / "sdcard" / "assets" / "themes" / theme / "sprite_sheet.svg"
        if not svg_path.exists():
            print(f"[!] SVG not found: {svg_path}  — skipping {theme}")
            continue

        out_dir = Path(args.out_dir) if args.out_dir else \
                  PROJECT_ROOT / "sdcard" / "assets" / "themes" / theme / "sprites"

        prefix = "[DRY RUN] " if args.dry_run else ""
        print(f"\n{prefix}Theme: {theme}")
        print(f"  SVG  : {svg_path}")
        print(f"  Out  : {out_dir}")

        # Read source crop coords from SVG data-rect attributes
        layout    = parse_sprite_rects(svg_path)
        svg_bytes = svg_path.read_bytes()

        if not layout:
            print(f"  [!] No sprite-<name> id elements found in SVG — skipping")
            continue

        if not args.dry_run:
            out_dir.mkdir(parents=True, exist_ok=True)

        meta_out = {}
        written  = 0
        skipped  = 0

        for i, name in enumerate(SPRITE_NAMES):
            rect = layout.get(name)
            if rect is None:
                print(f"  [{i:02d}] {name:30s}  — not in SVG, skipped")
                meta_out[name] = {"index": i, "w": 0, "h": 0}
                skipped += 1
                continue

            sx, sy, sw, sh = rect
            if sw == 0 or sh == 0:
                print(f"  [{i:02d}] {name:30s}  — zero area, skipped")
                meta_out[name] = {"index": i, "w": 0, "h": 0}
                skipped += 1
                continue

            result = rasterize_sprite(svg_bytes, sx, sy, sw, sh)
            if result is None:
                print(f"  [{i:02d}] {name:30s}  — rasterize failed, skipped")
                meta_out[name] = {"index": i, "w": 0, "h": 0}
                skipped += 1
                continue

            rgb_bytes, w, h = result
            bin_path = out_dir / f"{name}.bin"
            size_kb  = len(rgb_bytes) / 1024

            if args.dry_run:
                print(f"  [{i:02d}] {name:30s}  {w:4d}×{h:<4d}  {size_kb:6.1f} KB"
                      f"  (would write {bin_path.name})")
            else:
                bin_path.write_bytes(rgb_bytes)
                print(f"  [{i:02d}] {name:30s}  {w:4d}×{h:<4d}  {size_kb:6.1f} KB"
                      f"  → {bin_path.name}")

            meta_out[name] = {"index": i, "w": w, "h": h}
            written += 1

        meta_path = out_dir / "sprites_meta.json"
        if args.dry_run:
            print(f"\n  Would write {meta_path.name} ({written} sprites, {skipped} skipped)")
        else:
            meta_path.write_text(json.dumps(meta_out, indent=2))
            total_kb = sum(
                (out_dir / f"{n}.bin").stat().st_size / 1024
                for n in SPRITE_NAMES
                if (out_dir / f"{n}.bin").exists()
            )
            print(f"\n  ✓ {written} sprites written, {skipped} skipped")
            print(f"  ✓ sprites_meta.json written")
            print(f"  ✓ Total size: {total_kb:.1f} KB")


if __name__ == "__main__":
    main()
