#!/usr/bin/env python3
import math
import os

# Grid layout matching the coordinates specified in sc_ui_atlas_drake.h
GRID = {
    # Buttons (140 x 56)
    "btn_momentary_idle":    (0, 0, 140, 56),
    "btn_momentary_armed":   (150, 0, 140, 56),
    "btn_momentary_active":  (300, 0, 140, 56),
    "btn_latching_off":      (450, 0, 140, 56),
    "btn_latching_on":       (600, 0, 140, 56),
    "btn_inactive":          (750, 0, 140, 56),
    "btn_danger":            (900, 0, 140, 56),

    # Sliders
    "slider_track_h":        (0, 70, 120, 24),
    "slider_thumb":          (130, 70, 40, 24),
    "slider_track_v":        (180, 70, 24, 120),
    "axis_rudder_track":     (220, 70, 256, 32),
    "axis_rudder_pedal":     (490, 70, 56, 40),
    "axis_throttle_track":   (560, 70, 44, 120),
    "axis_throttle_grip":    (620, 70, 60, 20),
    "axis_yaw_needle":       (700, 70, 10, 56),

    # Joystick/2D Pads
    "axis_joystick_base":    (0, 200, 120, 120),
    "axis_joystick_thumb":   (130, 200, 40, 40),
    "axis_haat_base":        (180, 200, 120, 120),
    "axis_haat_cursor":      (310, 200, 24, 24),
    "axis_yaw_ring":         (350, 200, 120, 120),
    "knob_ring":             (480, 200, 64, 64),
    "knob_cap":              (560, 200, 64, 64),

    # D-Pad
    "axis_dpad_base":        (0, 330, 120, 120),
    "axis_dpad_up":          (130, 330, 40, 36),
    "axis_dpad_down":        (180, 330, 40, 36),
    "axis_dpad_left":        (230, 330, 36, 40),
    "axis_dpad_right":       (280, 330, 36, 40),

    # Jog Wheel (96 x 96)
    "jog_wheel_f0":          (0, 460, 96, 96),
    "jog_wheel_f1":          (110, 460, 96, 96),
    "jog_wheel_f2":          (220, 460, 96, 96),
    "jog_wheel_f3":          (330, 460, 96, 96),
    "jog_wheel_f4":          (440, 460, 96, 96),
    "jog_wheel_f5":          (550, 460, 96, 96),
    "jog_wheel_f6":          (660, 460, 96, 96),
    "jog_wheel_f7":          (770, 460, 96, 96),

    # 9-Slice Panel
    "panel_tl":              (0, 570, 16, 16),
    "panel_tr":              (20, 570, 16, 16),
    "panel_bl":              (40, 570, 16, 16),
    "panel_br":              (60, 570, 16, 16),
    "panel_edge_t":          (80, 570, 64, 8),
    "panel_edge_b":          (150, 570, 64, 8),
    "panel_edge_l":          (220, 570, 8, 64),
    "panel_edge_r":          (230, 570, 8, 64),
    "panel_center":          (240, 570, 64, 64),
}


def make_chamfer_path(x, y, w, h, ch):
    """Generate SVG path data for a chamfered rectangle."""
    return f"M {x+ch},{y} H {x+w-ch} L {x+w},{y+ch} V {y+h-ch} L {x+w-ch},{y+h} H {x+ch} L {x},{y+h-ch} V {y+ch} Z"


def make_rivet(cx, cy):
    """Draw a Drake Military metal rivet."""
    return f'<circle cx="{cx}" cy="{cy}" r="3" fill="url(#rivet-grad)" stroke="#000000" stroke-width="0.5"/>' \
           f'<circle cx="{cx-0.8}" cy="{cy-0.8}" r="0.8" fill="#C8B89C" opacity="0.4"/>'


def generate_drake():
    svg = []
    # Header
    svg.append('<?xml version="1.0" encoding="UTF-8"?>')
    svg.append('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1050 700" width="1050" height="700">')
    
    # Styles and Definitions
    svg.append('<defs>')
    # Grunge pattern for rustic worn look
    svg.append('  <pattern id="hazard" patternUnits="userSpaceOnUse" width="24" height="24" patternTransform="rotate(45)">')
    svg.append('    <rect width="12" height="24" fill="#FFB000"/>')
    svg.append('    <rect x="12" width="12" height="24" fill="#000000"/>')
    svg.append('  </pattern>')
    # Rivet metal gradient
    svg.append('  <radialGradient id="rivet-grad" cx="35%" cy="35%" r="65%">')
    svg.append('    <stop offset="0%" stop-color="#7A6B58"/>')
    svg.append('    <stop offset="60%" stop-color="#2A1F18"/>')
    svg.append('    <stop offset="100%" stop-color="#0B0807"/>')
    svg.append('  </radialGradient>')
    svg.append('  <style>')
    svg.append('    .st-font { font-family: "Courier New", Courier, monospace; font-weight: bold; font-size: 11px; fill: #C8B89C; text-anchor: middle; letter-spacing: 1px; }')
    svg.append('    .st-font-active { font-family: "Courier New", Courier, monospace; font-weight: bold; font-size: 12px; fill: #000000; text-anchor: middle; letter-spacing: 1.5px; }')
    svg.append('    .st-font-danger { font-family: "Courier New", Courier, monospace; font-weight: bold; font-size: 12px; fill: #FF1F1F; text-anchor: middle; letter-spacing: 1.5px; }')
    svg.append('    .st-font-inactive { font-family: "Courier New", Courier, monospace; font-weight: bold; font-size: 10px; fill: #3A2C20; text-anchor: middle; letter-spacing: 1px; }')
    svg.append('  </style>')
    svg.append('</defs>')

    # Background canvas fill
    svg.append('<rect width="1050" height="700" fill="#0B0807"/>')

    # Draw all sprites
    for name, (x, y, w, h) in GRID.items():
        svg.append(f'<!-- sprite-{name} ({w}x{h}) -->')
        svg.append(f'<g id="sprite-{name}" data-rect="{x},{y},{w},{h}">')

        if name == "btn_momentary_idle":
            # Dark steel plate, rust-brown border, 4 corner rivets
            path = make_chamfer_path(x, y, w, h, 6)
            svg.append(f'  <path d="{path}" fill="#140E0A" stroke="#5A4636" stroke-width="1.5"/>')
            svg.append(make_rivet(x + 10, y + 10))
            svg.append(make_rivet(x + w - 10, y + 10))
            svg.append(make_rivet(x + 10, y + h - 10))
            svg.append(make_rivet(x + w - 10, y + h - 10))

        elif name == "btn_momentary_armed":
            # Dark steel plate, glowing amber border, rivets, armed label
            path = make_chamfer_path(x, y, w, h, 6)
            svg.append(f'  <path d="{path}" fill="#1C140E" stroke="#FFB000" stroke-width="2"/>')
            svg.append(make_rivet(x + 10, y + 10))
            svg.append(make_rivet(x + w - 10, y + 10))
            svg.append(make_rivet(x + 10, y + h - 10))
            svg.append(make_rivet(x + w - 10, y + h - 10))
            # Glowing indicator dots
            svg.append(f'  <circle cx="{x + w/2}" cy="{y + 14}" r="2" fill="#FFB000"/>')

        elif name == "btn_momentary_active":
            # Solid glowing amber, black text, hazard stripes on side
            path = make_chamfer_path(x, y, w, h, 6)
            svg.append(f'  <path d="{path}" fill="#FFB000" stroke="#FFB000" stroke-width="1"/>')
            # Hazard zones on sides
            svg.append(f'  <rect x="{x}" y="{y}" width="14" height="{h}" fill="url(#hazard)"/>')
            svg.append(f'  <rect x="{x + w - 14}" y="{y}" width="14" height="{h}" fill="url(#hazard)"/>')
            svg.append(make_rivet(x + 22, y + 10))
            svg.append(make_rivet(x + w - 22, y + 10))
            svg.append(make_rivet(x + 22, y + h - 10))
            svg.append(make_rivet(x + w - 22, y + h - 10))

        elif name == "btn_latching_off":
            # Toggle switch Off
            path = make_chamfer_path(x, y, w, h, 6)
            svg.append(f'  <path d="{path}" fill="#140E0A" stroke="#5A4636" stroke-width="1.5"/>')
            # Toggle housing
            svg.append(f'  <rect x="{x + 20}" y="{y + 18}" width="20" height="20" rx="2" fill="#0B0807" stroke="#3A2C20"/>')
            # Toggle handle pointing left
            svg.append(f'  <line x1="{x + 30}" y1="{y + 28}" x2="{x + 12}" y2="{y + 28}" stroke="#C8B89C" stroke-width="4" stroke-linecap="round"/>')
            svg.append(f'  <circle cx="{x + 12}" cy="{y + 28}" r="4" fill="#C8B89C"/>')

        elif name == "btn_latching_on":
            # Toggle switch On
            path = make_chamfer_path(x, y, w, h, 6)
            svg.append(f'  <path d="{path}" fill="#1C140E" stroke="#FFB000" stroke-width="2"/>')
            # Toggle housing
            svg.append(f'  <rect x="{x + 20}" y="{y + 18}" width="20" height="20" rx="2" fill="#0B0807" stroke="#FFB000"/>')
            # Toggle handle pointing right
            svg.append(f'  <line x1="{x + 30}" y1="{y + 28}" x2="{x + 48}" y2="{y + 28}" stroke="#FFB000" stroke-width="4" stroke-linecap="round"/>')
            svg.append(f'  <circle cx="{x + 48}" cy="{y + 28}" r="4" fill="#FFD96B"/>')

        elif name == "btn_inactive":
            # Worn out locked look
            path = make_chamfer_path(x, y, w, h, 6)
            svg.append(f'  <path d="{path}" fill="#080605" stroke="#2A1F18" stroke-width="1.5"/>')

        elif name == "btn_danger":
            # Alarm red eject button with hazard zones
            path = make_chamfer_path(x, y, w, h, 6)
            svg.append(f'  <path d="{path}" fill="#3A0808" stroke="#FF1F1F" stroke-width="2"/>')
            svg.append(f'  <rect x="{x}" y="{y}" width="16" height="{h}" fill="url(#hazard)"/>')
            svg.append(f'  <rect x="{x + w - 16}" y="{y}" width="16" height="{h}" fill="url(#hazard)"/>')
            svg.append(make_rivet(x + 22, y + 10))
            svg.append(make_rivet(x + w - 22, y + 10))
            svg.append(make_rivet(x + 22, y + h - 10))
            svg.append(make_rivet(x + w - 22, y + h - 10))

        elif name == "slider_track_h":
            # Horizontal slider track line (120 x 24)
            svg.append(f'  <rect x="{x}" y="{y + 8}" width="{w}" height="8" rx="2" fill="#000000" stroke="#3A2C20" stroke-width="1.5"/>')
            # Tick marks
            for tx in range(0, w + 1, 20):
                svg.append(f'  <line x1="{x + tx}" y1="{y}" x2="{x + tx}" y2="{y + 5}" stroke="#C8B89C" stroke-width="1"/>')
                svg.append(f'  <line x1="{x + tx}" y1="{y + 19}" x2="{x + tx}" y2="{y + 24}" stroke="#C8B89C" stroke-width="1"/>')

        elif name == "slider_thumb":
            # Slider thumb (40 x 24)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="2" fill="#2A1F18" stroke="#FFB000" stroke-width="1.5"/>')
            # Grate ridges
            svg.append(f'  <line x1="{x + 15}" y1="{y + 4}" x2="{x + 15}" y2="{y + 20}" stroke="#FFB000" stroke-width="2"/>')
            svg.append(f'  <line x1="{x + 20}" y1="{y + 4}" x2="{x + 20}" y2="{y + 20}" stroke="#FFB000" stroke-width="2"/>')
            svg.append(f'  <line x1="{x + 25}" y1="{y + 4}" x2="{x + 25}" y2="{y + 20}" stroke="#FFB000" stroke-width="2"/>')

        elif name == "slider_track_v":
            # Vertical slider track line (24 x 120)
            svg.append(f'  <rect x="{x + 8}" y="{y}" width="8" height="{h}" rx="2" fill="#000000" stroke="#3A2C20" stroke-width="1.5"/>')
            # Tick marks
            for ty in range(0, h + 1, 20):
                svg.append(f'  <line x1="{x}" y1="{y + ty}" x2="{x + 5}" y2="{y + ty}" stroke="#C8B89C" stroke-width="1"/>')
                svg.append(f'  <line x1="{x + 19}" y1="{y + ty}" x2="{x + 24}" y2="{y + ty}" stroke="#C8B89C" stroke-width="1"/>')

        elif name == "axis_rudder_track":
            # Rudder slot track (256 x 32)
            svg.append(f'  <rect x="{x}" y="{y + 8}" width="{w}" height="16" fill="#000000" stroke="#5A4636" stroke-width="1.5"/>')
            for tx in range(0, w + 1, 32):
                svg.append(f'  <line x1="{x + tx}" y1="{y}" x2="{x + tx}" y2="{y + 6}" stroke="#C8B89C" stroke-width="1"/>')
                svg.append(f'  <line x1="{x + tx}" y1="{y + 26}" x2="{x + tx}" y2="{y + 32}" stroke="#C8B89C" stroke-width="1"/>')
            svg.append(make_rivet(x + 8, y + 16))
            svg.append(make_rivet(x + w - 8, y + 16))

        elif name == "axis_rudder_pedal":
            # Rudder pedal block (56 x 40)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#140E0A" stroke="#FFB000" stroke-width="2"/>')
            # Diagonal rubber pad ridges
            for offset in range(6, w - 10, 10):
                svg.append(f'  <line x1="{x + offset}" y1="{y + 8}" x2="{x + offset + 8}" y2="{y + 32}" stroke="#FFB000" stroke-width="3"/>')

        elif name == "axis_throttle_track":
            # Slide axis track (44 x 120)
            svg.append(f'  <rect x="{x + 16}" y="{y}" width="12" height="{h}" fill="#000000" stroke="#3A2C20" stroke-width="1.5"/>')
            # Major percent marks (100, 50, 0)
            svg.append(f'  <text x="{x + 8}" y="{y + 12}" font-family="monospace" font-size="8" fill="#C8B89C" text-anchor="end">100</text>')
            svg.append(f'  <text x="{x + 8}" y="{y + 64}" font-family="monospace" font-size="8" fill="#C8B89C" text-anchor="end">50</text>')
            svg.append(f'  <text x="{x + 8}" y="{y + 116}" font-family="monospace" font-size="8" fill="#C8B89C" text-anchor="end">0</text>')
            for ty in range(0, h + 1, 15):
                svg.append(f'  <line x1="{x + 32}" y1="{y + ty}" x2="{x + 40}" y2="{y + ty}" stroke="#C8B89C" stroke-width="1"/>')

        elif name == "axis_throttle_grip":
            # Throttle grip block (60 x 20)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#2A1F18" stroke="#8A3B12" stroke-width="1.5"/>')
            # Left and right side warning stripes
            svg.append(f'  <rect x="{x}" y="{y}" width="6" height="{h}" fill="url(#hazard)"/>')
            svg.append(f'  <rect x="{x + w - 6}" y="{y}" width="6" height="{h}" fill="url(#hazard)"/>')
            svg.append(f'  <circle cx="{x + w/2}" cy="{y + h/2}" r="3" fill="#FFB000"/>')

        elif name == "axis_yaw_needle":
            # Yaw dial needle (10 x 56)
            # Sharp red needle pointing up, centered at x+5, y+28
            svg.append(f'  <polygon points="{x+5},{y} {x+9},{y+42} {x+6},{y+42} {x+6},{y+56} {x+4},{y+56} {x+4},{y+42} {x+1},{y+42}" fill="#FF1F1F"/>')
            svg.append(f'  <circle cx="{x+5}" cy="{y+48}" r="2" fill="#FFFFFF"/>')

        elif name == "axis_joystick_base":
            # Floating thumb pad base (120 x 120)
            cx, cy = x + 60, y + 60
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="50" fill="#000000" stroke="#2A1F18" stroke-width="2"/>')
            # Inner circle rings
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="35" fill="none" stroke="#5A4636" stroke-dasharray="2 3"/>')
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="20" fill="none" stroke="#5A4636" stroke-dasharray="2 3"/>')
            # Crosshairs
            svg.append(f'  <line x1="{cx - 50}" y1="{cy}" x2="{cx + 50}" y2="{cy}" stroke="#7A5400" stroke-dasharray="3 3"/>')
            svg.append(f'  <line x1="{cx}" y1="{cy - 50}" x2="{cx}" y2="{cy + 50}" stroke="#7A5400" stroke-dasharray="3 3"/>')
            # Rivets in corners
            svg.append(make_rivet(x + 12, y + 12))
            svg.append(make_rivet(x + w - 12, y + 12))
            svg.append(make_rivet(x + 12, y + h - 12))
            svg.append(make_rivet(x + w - 12, y + h - 12))

        elif name == "axis_joystick_thumb":
            # Inner thumb controller button (40 x 40)
            cx, cy = x + 20, y + 20
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="18" fill="#140E0A" stroke="#FFB000" stroke-width="2"/>')
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="8" fill="none" stroke="#5A4636"/>')
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="3" fill="#FF1F1F"/>')

        elif name == "axis_haat_base":
            # Squared boundaries frame (120 x 120)
            path = make_chamfer_path(x + 8, y + 8, w - 16, h - 16, 12)
            svg.append(f'  <path d="{path}" fill="#000000" stroke="#2A1F18" stroke-width="2"/>')
            # Rivets at corners
            svg.append(make_rivet(x + 14, y + 14))
            svg.append(make_rivet(x + w - 14, y + 14))
            svg.append(make_rivet(x + 14, y + h - 14))
            svg.append(make_rivet(x + w - 14, y + h - 14))
            # Grid ticks
            svg.append(f'  <line x1="{x + 20}" y1="{y + 60}" x2="{x + 100}" y2="{y + 60}" stroke="#7A5400" stroke-dasharray="2 4"/>')
            svg.append(f'  <line x1="{x + 60}" y1="{y + 20}" x2="{x + 60}" y2="{y + 100}" stroke="#7A5400" stroke-dasharray="2 4"/>')

        elif name == "axis_haat_cursor":
            # Positional crosshair dot (24 x 24)
            cx, cy = x + 12, y + 12
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="3" fill="#FF1F1F"/>')
            svg.append(f'  <line x1="{cx - 10}" y1="{cy}" x2="{cx + 10}" y2="{cy}" stroke="#FF1F1F" stroke-width="1.2"/>')
            svg.append(f'  <line x1="{cx}" y1="{cy - 10}" x2="{cx}" y2="{cy + 10}" stroke="#FF1F1F" stroke-width="1.2"/>')

        elif name == "axis_yaw_ring":
            # Static dial background (120 x 120)
            cx, cy = x + 60, y + 60
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="52" fill="none" stroke="#2A1F18" stroke-width="2"/>')
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="45" fill="none" stroke="#7A5400" stroke-dasharray="2 3"/>')
            # Degree marks
            for angle in range(0, 360, 30):
                rad = math.radians(angle)
                x1 = cx + 45 * math.sin(rad)
                y1 = cy - 45 * math.cos(rad)
                x2 = cx + 52 * math.sin(rad)
                y2 = cy - 52 * math.cos(rad)
                svg.append(f'  <line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#7A5400" stroke-width="1"/>')
            svg.append(f'  <text x="{cx}" y="{cy - 34}" font-family="monospace" font-size="8" fill="#FFB000" text-anchor="middle">N</text>')
            svg.append(f'  <text x="{cx + 38}" y="{cy + 3}" font-family="monospace" font-size="8" fill="#FFB000" text-anchor="middle">E</text>')
            svg.append(f'  <text x="{cx}" y="{cy + 40}" font-family="monospace" font-size="8" fill="#FFB000" text-anchor="middle">S</text>')
            svg.append(f'  <text x="{cx - 38}" y="{cy + 3}" font-family="monospace" font-size="8" fill="#FFB000" text-anchor="middle">W</text>')

        elif name == "knob_ring":
            # Outer dial marker shell (64 x 64)
            cx, cy = x + 32, y + 32
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="26" fill="none" stroke="#2A1F18" stroke-width="2"/>')
            # Sweep tick marks
            for angle in range(-135, 136, 30):
                rad = math.radians(angle)
                x1 = cx + 22 * math.sin(rad)
                y1 = cy + 22 * math.cos(rad)
                x2 = cx + 27 * math.sin(rad)
                y2 = cy + 27 * math.cos(rad)
                svg.append(f'  <line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#7A5400" stroke-width="1.2"/>')

        elif name == "knob_cap":
            # Rotary dial cap (64 x 64)
            cx, cy = x + 32, y + 32
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="22" fill="#140E0A" stroke="#FFB000" stroke-width="1.5"/>')
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="12" fill="none" stroke="#2A1F18"/>')
            svg.append(f'  <line x1="{cx}" y1="{cy}" x2="{cx}" y2="{cy - 20}" stroke="#FFB000" stroke-width="2"/>')

        elif name == "axis_dpad_base":
            # Stationary cross (120 x 120)
            cross_path = f"M {x+42},{y} H {x+78} V {y+42} H {x+120} V {y+78} H {x+78} V {y+120} H {x+42} V {y+78} H {x} V {y+42} H {x+42} Z"
            svg.append(f'  <path d="{cross_path}" fill="#140E0A" stroke="#2A1F18" stroke-width="2"/>')
            svg.append(f'  <circle cx="{x+60}" cy="{y+60}" r="10" fill="none" stroke="#7A5400"/>')

        elif name == "axis_dpad_up":
            # Highlighted up arrow (40 x 36)
            path = f"M {x+20},{y+2} L {x+38},{y+26} H {x+28} V {y+34} H {x+12} V {y+26} H {x+2} Z"
            svg.append(f'  <path d="{path}" fill="#FFB000" stroke="#7A5400" stroke-width="1"/>')

        elif name == "axis_dpad_down":
            # Highlighted down arrow (40 x 36)
            path = f"M {x+20},{y+34} L {x+2},{y+10} H {x+12} V {y+2} H {x+28} V {y+10} H {x+38} Z"
            svg.append(f'  <path d="{path}" fill="#FFB000" stroke="#7A5400" stroke-width="1"/>')

        elif name == "axis_dpad_left":
            # Highlighted left arrow (36 x 40)
            path = f"M {x+2},{y+20} L {x+26},{y+2} V {x-x+12+x} H {x+34} V {y+28} H {x+26} V {y+38} Z"
            # Correct path computation:
            path = f"M {x+2},{y+20} L {x+26},{y+2} V {y+12} H {x+34} V {y+28} H {x+26} V {y+38} Z"
            svg.append(f'  <path d="{path}" fill="#FFB000" stroke="#7A5400" stroke-width="1"/>')

        elif name == "axis_dpad_right":
            # Highlighted right arrow (36 x 40)
            path = f"M {x+34},{y+20} L {x+10},{y+38} V {y+28} H {x+2} V {y+12} H {x+10} V {y+2} Z"
            svg.append(f'  <path d="{path}" fill="#FFB000" stroke="#7A5400" stroke-width="1"/>')

        elif name.startswith("jog_wheel_f"):
            # Jog Wheel frames (96 x 96)
            frame_num = int(name[11])
            angle = frame_num * 45
            cx, cy = x + 48, y + 48
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="40" fill="#000000" stroke="#2A1F18" stroke-width="2"/>')
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="32" fill="none" stroke="#7A5400" stroke-dasharray="4 4"/>')
            # Rotating dot indicator
            rad = math.radians(angle)
            dot_x = cx + 28 * math.sin(rad)
            dot_y = cy - 28 * math.cos(rad)
            svg.append(f'  <circle cx="{dot_x}" cy="{dot_y}" r="6" fill="#FFB000" stroke="#000000" stroke-width="1"/>')
            svg.append(f'  <circle cx="{dot_x}" cy="{dot_y}" r="2" fill="#FFFFFF"/>')

        elif name == "panel_tl":
            # Top-Left corner (16 x 16)
            path = f"M {x+4},{y} H {x+16} V {y+16} H {x} V {y+4} Z"
            svg.append(f'  <path d="{path}" fill="#0B0807"/>')
            svg.append(f'  <line x1="{x+4}" y1="{y}" x2="{x}" y2="{y+4}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x}" y1="{y+4}" x2="{x}" y2="{y+16}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x+4}" y1="{y}" x2="{x+16}" y2="{y}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <circle cx="{x+9}" cy="{y+9}" r="1.5" fill="url(#rivet-grad)"/>')

        elif name == "panel_tr":
            # Top-Right corner (16 x 16)
            path = f"M {x},{y} H {x+12} L {x+16},{y+4} V {y+16} H {x} Z"
            svg.append(f'  <path d="{path}" fill="#0B0807"/>')
            svg.append(f'  <line x1="{x+12}" y1="{y}" x2="{x+16}" y2="{y+4}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x}" y1="{y}" x2="{x+12}" y2="{y}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x+16}" y1="{y+4}" x2="{x+16}" y2="{y+16}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <circle cx="{x+7}" cy="{y+9}" r="1.5" fill="url(#rivet-grad)"/>')

        elif name == "panel_bl":
            # Bottom-Left corner (16 x 16)
            path = f"M {x},{y} H {x+16} V {y+16} H {x+4} L {x},{y+12} Z"
            svg.append(f'  <path d="{path}" fill="#0B0807"/>')
            svg.append(f'  <line x1="{x}" y1="{y}" x2="{x}" y2="{y+12}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x}" y1="{y+12}" x2="{x+4}" y2="{y+16}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x+4}" y1="{y+16}" x2="{x+16}" y2="{y+16}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <circle cx="{x+9}" cy="{y+7}" r="1.5" fill="url(#rivet-grad)"/>')

        elif name == "panel_br":
            # Bottom-Right corner (16 x 16)
            path = f"M {x},{y} H {x+16} V {y+12} L {x+12},{y+16} H {x} Z"
            svg.append(f'  <path d="{path}" fill="#0B0807"/>')
            svg.append(f'  <line x1="{x+16}" y1="{y}" x2="{x+16}" y2="{y+12}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x+16}" y1="{y+12}" x2="{x+12}" y2="{y+16}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x}" y1="{y+16}" x2="{x+12}" y2="{y+16}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <circle cx="{x+7}" cy="{y+7}" r="1.5" fill="url(#rivet-grad)"/>')

        elif name == "panel_edge_t":
            # Top horizontal edge (64 x 8)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#0B0807"/>')
            svg.append(f'  <line x1="{x}" y1="{y}" x2="{x+w}" y2="{y}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x}" y1="{y+h}" x2="{x+w}" y2="{y+h}" stroke="#140E0A" stroke-width="0.8"/>')

        elif name == "panel_edge_b":
            # Bottom horizontal edge (64 x 8)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#0B0807"/>')
            svg.append(f'  <line x1="{x}" y1="{y+h}" x2="{x+w}" y2="{y+h}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x}" y1="{y}" x2="{x+w}" y2="{y}" stroke="#000000" stroke-width="0.8"/>')

        elif name == "panel_edge_l":
            # Left vertical edge (8 x 64)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#0B0807"/>')
            svg.append(f'  <line x1="{x}" y1="{y}" x2="{x}" y2="{y+h}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x+w}" y1="{y}" x2="{x+w}" y2="{y+h}" stroke="#140E0A" stroke-width="0.8"/>')

        elif name == "panel_edge_r":
            # Right vertical edge (8 x 64)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#0B0807"/>')
            svg.append(f'  <line x1="{x+w}" y1="{y}" x2="{x+w}" y2="{y+h}" stroke="#5A4636" stroke-width="1"/>')
            svg.append(f'  <line x1="{x}" y1="{y}" x2="{x}" y2="{y+h}" stroke="#000000" stroke-width="0.8"/>')

        elif name == "panel_center":
            # Panel Center (64 x 64)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#000000"/>')

        svg.append('</g>')

    svg.append('</svg>')
    return '\n'.join(svg)


def generate_origin():
    svg = []
    # Header
    svg.append('<?xml version="1.0" encoding="UTF-8"?>')
    svg.append('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1050 700" width="1050" height="700">')
    
    # Styles and Definitions
    svg.append('<defs>')
    # Glass vertical gradient
    svg.append('  <linearGradient id="glass-grad" x1="0" y1="0" x2="0" y2="1">')
    svg.append('    <stop offset="0%" stop-color="#142540"/>')
    svg.append('    <stop offset="25%" stop-color="#0A1320"/>')
    svg.append('    <stop offset="100%" stop-color="#04070D"/>')
    svg.append('  </linearGradient>')
    # Gold vertical gradient
    svg.append('  <linearGradient id="gold-grad" x1="0" y1="0" x2="0" y2="1">')
    svg.append('    <stop offset="0%" stop-color="#F3DC9C"/>')
    svg.append('    <stop offset="50%" stop-color="#D4B26A"/>')
    svg.append('    <stop offset="100%" stop-color="#7A5E2B"/>')
    svg.append('  </linearGradient>')
    # Style
    svg.append('  <style>')
    svg.append('    .or-font { font-family: "Segoe UI", "Helvetica Neue", Arial, sans-serif; font-size: 10px; fill: #E8EEF6; text-anchor: middle; letter-spacing: 2px; }')
    svg.append('    .or-font-active { font-family: "Segoe UI", "Helvetica Neue", Arial, sans-serif; font-weight: bold; font-size: 11px; fill: #04070D; text-anchor: middle; letter-spacing: 2.5px; }')
    svg.append('    .or-font-danger { font-family: "Segoe UI", "Helvetica Neue", Arial, sans-serif; font-size: 11px; fill: #FF5A6A; text-anchor: middle; letter-spacing: 2px; }')
    svg.append('    .or-font-inactive { font-family: "Segoe UI", "Helvetica Neue", Arial, sans-serif; font-size: 10px; fill: #54627A; text-anchor: middle; letter-spacing: 2px; }')
    svg.append('  </style>')
    svg.append('</defs>')

    # Background canvas fill
    svg.append('<rect width="1050" height="700" fill="#04070D"/>')

    # Draw all sprites
    for name, (x, y, w, h) in GRID.items():
        svg.append(f'<!-- sprite-{name} ({w}x{h}) -->')
        svg.append(f'<g id="sprite-{name}" data-rect="{x},{y},{w},{h}">')

        if name == "btn_momentary_idle":
            # Smoked glass panel, bezel grey border, paper white text
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="url(#glass-grad)" stroke="#243349" stroke-width="1.2"/>')
            # Thin top light highlight
            svg.append(f'  <path d="M {x+6},{y+1.5} H {x+w-6}" stroke="#6EC4FF" stroke-width="0.8" opacity="0.4"/>')

        elif name == "btn_momentary_armed":
            # Glass panel, ice-blue glowing border, gold inlay, armed text
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="url(#glass-grad)" stroke="#6EC4FF" stroke-width="1.8"/>')
            svg.append(f'  <path d="M {x+6},{y+1.5} H {x+w-6}" stroke="#E8EEF6" stroke-width="1" opacity="0.6"/>')
            # Gold inlay line on left
            svg.append(f'  <line x1="{x + 8}" y1="{y + 12}" x2="{x + 8}" y2="{y + h - 12}" stroke="url(#gold-grad)" stroke-width="2"/>')

        elif name == "btn_momentary_active":
            # Solid Ice-blue filled button with Navy-night active text
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="#6EC4FF" stroke="#6EC4FF" stroke-width="1"/>')
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="2" rx="1" fill="#FFFFFF" opacity="0.6"/>')

        elif name == "btn_latching_off":
            # Toggle Off
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="url(#glass-grad)" stroke="#243349" stroke-width="1.2"/>')
            # Toggle channel
            svg.append(f'  <rect x="{x + 20}" y="{y + 20}" width="40" height="16" rx="8" fill="#04070D" stroke="#243349" stroke-width="1"/>')
            # Toggle knob off position
            svg.append(f'  <circle cx="{x + 28}" cy="{y + 28}" r="8" fill="#54627A"/>')

        elif name == "btn_latching_on":
            # Toggle On
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="url(#glass-grad)" stroke="#6EC4FF" stroke-width="1.5"/>')
            # Toggle channel
            svg.append(f'  <rect x="{x + 20}" y="{y + 20}" width="40" height="16" rx="8" fill="#04070D" stroke="#6EC4FF" stroke-width="1"/>')
            # Toggle knob on position
            svg.append(f'  <circle cx="{x + 52}" cy="{y + 28}" r="8" fill="#6EC4FF"/>')
            svg.append(f'  <circle cx="{x + 52}" cy="{y + 28}" r="3" fill="#FFFFFF"/>')

        elif name == "btn_inactive":
            # Disabled button
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="#04070D" stroke="#243349" stroke-width="1" stroke-dasharray="3 3"/>')

        elif name == "btn_danger":
            # Danger abort eject button
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="#1C090D" stroke="#FF5A6A" stroke-width="1.8"/>')
            # Gold inlay trim on left
            svg.append(f'  <line x1="{x + 8}" y1="{y + 12}" x2="{x + 8}" y2="{y + h - 12}" stroke="url(#gold-grad)" stroke-width="2"/>')

        elif name == "slider_track_h":
            # Horizontal slider track line (120 x 24)
            svg.append(f'  <rect x="{x}" y="{y + 10}" width="{w}" height="4" rx="2" fill="#04070D" stroke="#243349" stroke-width="1"/>')
            # Minimal ticks
            for tx in range(0, w + 1, 30):
                svg.append(f'  <line x1="{x + tx}" y1="{y + 18}" x2="{x + tx}" y2="{y + 22}" stroke="#54627A" stroke-width="0.8"/>')

        elif name == "slider_thumb":
            # Slider thumb (40 x 24) - Glass pill shape
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="12" fill="url(#glass-grad)" stroke="#6EC4FF" stroke-width="1.8"/>')
            svg.append(f'  <circle cx="{x + w/2}" cy="{y + h/2}" r="3" fill="#E8EEF6"/>')

        elif name == "slider_track_v":
            # Vertical slider track line (24 x 120)
            svg.append(f'  <rect x="{x + 10}" y="{y}" width="4" height="{h}" rx="2" fill="#04070D" stroke="#243349" stroke-width="1"/>')
            # Ticks
            for ty in range(0, h + 1, 30):
                svg.append(f'  <line x1="{x + 18}" y1="{y + ty}" x2="{x + 22}" y2="{y + ty}" stroke="#54627A" stroke-width="0.8"/>')

        elif name == "axis_rudder_track":
            # Rudder slot track (256 x 32)
            svg.append(f'  <rect x="{x}" y="{y + 14}" width="{w}" height="4" rx="2" fill="#04070D" stroke="#243349" stroke-width="1"/>')
            # Ticks
            for tx in range(0, w + 1, 32):
                svg.append(f'  <line x1="{x + tx}" y1="{y + 4}" x2="{x + tx}" y2="{y + 10}" stroke="#6EC4FF" stroke-width="0.8" opacity="0.6"/>')

        elif name == "axis_rudder_pedal":
            # Rudder pedal block (56 x 40)
            svg.append(f'  <rect x="{x + 4}" y="{y + 4}" width="{w - 8}" height="{h - 8}" rx="6" fill="url(#glass-grad)" stroke="url(#gold-grad)" stroke-width="1.8"/>')
            svg.append(f'  <line x1="{x + 14}" y1="{y + 16}" x2="{x + w - 14}" y2="{y + 16}" stroke="#6EC4FF" stroke-width="1"/>')
            svg.append(f'  <line x1="{x + 14}" y1="{y + 24}" x2="{x + w - 14}" y2="{y + 24}" stroke="#6EC4FF" stroke-width="1"/>')

        elif name == "axis_throttle_track":
            # Slide axis track (44 x 120)
            svg.append(f'  <rect x="{x + 20}" y="{y}" width="4" height="{h}" rx="2" fill="#04070D" stroke="#243349" stroke-width="1"/>')
            # Major percent marks
            svg.append(f'  <text x="{x + 10}" y="{y + 12}" font-family="sans-serif" font-size="8" fill="#6EC4FF" text-anchor="end">100</text>')
            svg.append(f'  <text x="{x + 10}" y="{y + 64}" font-family="sans-serif" font-size="8" fill="#6EC4FF" text-anchor="end">50</text>')
            svg.append(f'  <text x="{x + 10}" y="{y + 116}" font-family="sans-serif" font-size="8" fill="#6EC4FF" text-anchor="end">0</text>')

        elif name == "axis_throttle_grip":
            # Throttle grip block (60 x 20)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="url(#glass-grad)" stroke="#6EC4FF" stroke-width="1.5"/>')
            svg.append(f'  <circle cx="{x + w/2}" cy="{y + h/2}" r="5" fill="none" stroke="url(#gold-grad)" stroke-width="1.5"/>')

        elif name == "axis_yaw_needle":
            # Yaw dial needle (10 x 56)
            # Thin gold needle line with central circle
            svg.append(f'  <line x1="{x + 5}" y1="{y}" x2="{x + 5}" y2="{y + h}" stroke="url(#gold-grad)" stroke-width="1.5"/>')
            svg.append(f'  <circle cx="{x + 5}" cy="{y + 28}" r="3.5" fill="url(#gold-grad)"/>')
            svg.append(f'  <circle cx="{x + 5}" cy="{y + 28}" r="1.2" fill="#04070D"/>')

        elif name == "axis_joystick_base":
            # Floating thumb pad base (120 x 120)
            cx, cy = x + 60, y + 60
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="50" fill="none" stroke="#243349" stroke-width="1.5"/>')
            # Gold concentric circle
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="36" fill="none" stroke="url(#gold-grad)" stroke-width="0.8" opacity="0.6"/>')
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="22" fill="none" stroke="#243349" stroke-dasharray="1 3"/>')
            # Grid hair crosshairs
            svg.append(f'  <line x1="{cx - 52}" y1="{cy}" x2="{cx + 52}" y2="{cy}" stroke="#243349" stroke-width="0.8"/>')
            svg.append(f'  <line x1="{cx}" y1="{cy - 52}" x2="{cx}" y2="{cy + 52}" stroke="#243349" stroke-width="0.8"/>')

        elif name == "axis_joystick_thumb":
            # Inner thumb controller button (40 x 40)
            cx, cy = x + 20, y + 20
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="18" fill="url(#glass-grad)" stroke="#6EC4FF" stroke-width="1.8"/>')
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="3" fill="#E8EEF6"/>')

        elif name == "axis_haat_base":
            # Squared boundaries frame (120 x 120)
            svg.append(f'  <rect x="{x + 8}" y="{y + 8}" width="{w - 16}" height="{h - 16}" rx="10" fill="url(#glass-grad)" stroke="#243349" stroke-width="1.5"/>')
            svg.append(f'  <rect x="{x + 14}" y="{y + 14}" width="{w - 28}" height="{h - 28}" rx="6" fill="none" stroke="url(#gold-grad)" stroke-width="0.8" opacity="0.6"/>')

        elif name == "axis_haat_cursor":
            # Positional crosshair dot (24 x 24)
            cx, cy = x + 12, y + 12
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="4" fill="#6EC4FF"/>')
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="9" fill="none" stroke="#6EC4FF" stroke-width="0.8" stroke-dasharray="1 2"/>')

        elif name == "axis_yaw_ring":
            # Static dial background (120 x 120)
            cx, cy = x + 60, y + 60
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="50" fill="none" stroke="#243349" stroke-width="1.2"/>')
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="44" fill="none" stroke="url(#gold-grad)" stroke-width="0.8" opacity="0.6"/>')
            for angle in range(0, 360, 45):
                rad = math.radians(angle)
                x1 = cx + 44 * math.sin(rad)
                y1 = cy - 44 * math.cos(rad)
                x2 = cx + 50 * math.sin(rad)
                y2 = cy - 50 * math.cos(rad)
                svg.append(f'  <line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#243349" stroke-width="1"/>')
            # Sleek tags
            svg.append(f'  <text x="{cx}" y="{cy - 32}" font-family="sans-serif" font-size="8" fill="#D4B26A" text-anchor="middle">N</text>')
            svg.append(f'  <text x="{cx + 36}" y="{cy + 3}" font-family="sans-serif" font-size="8" fill="#D4B26A" text-anchor="middle">E</text>')

        elif name == "knob_ring":
            # Outer dial marker shell (64 x 64)
            cx, cy = x + 32, y + 32
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="26" fill="none" stroke="#243349" stroke-width="1.2"/>')
            # Gold sweep line
            svg.append(f'  <path d="M {cx - 18.38},{cy + 18.38} A 26 26 0 1 1 {cx + 18.38},{cy + 18.38}" fill="none" stroke="url(#gold-grad)" stroke-width="1" opacity="0.7"/>')

        elif name == "knob_cap":
            # Rotary dial cap (64 x 64)
            cx, cy = x + 32, y + 32
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="22" fill="url(#glass-grad)" stroke="#6EC4FF" stroke-width="1.5"/>')
            # Small gold indicator dot on the edge
            svg.append(f'  <circle cx="{cx}" cy="{cy - 16}" r="2.5" fill="url(#gold-grad)"/>')

        elif name == "axis_dpad_base":
            # Stationary base cross (120 x 120)
            cross_path = f"M {x+44},{y+10} H {x+76} V {y+44} H {x+110} V {y+76} H {x+76} V {y+110} H {x+44} V {y+76} H {x+10} V {y+44} H {x+44} Z"
            svg.append(f'  <path d="{cross_path}" fill="url(#glass-grad)" stroke="#243349" stroke-width="1.5"/>')
            svg.append(f'  <circle cx="{x+60}" cy="{y+60}" r="8" fill="none" stroke="url(#gold-grad)" stroke-width="0.8" opacity="0.6"/>')

        elif name == "axis_dpad_up":
            # Highlighted up arrow (40 x 36)
            path = f"M {x+20},{y+6} L {x+34},{y+22} H {x+26} V {y+30} H {x+14} V {y+22} H {x+6} Z"
            svg.append(f'  <path d="{path}" fill="#6EC4FF" stroke="#2A6B96" stroke-width="1"/>')

        elif name == "axis_dpad_down":
            # Highlighted down arrow (40 x 36)
            path = f"M {x+20},{y+30} L {x+6},{y+14} H {x+14} V {y+6} H {x+26} V {y+14} H {x+34} Z"
            svg.append(f'  <path d="{path}" fill="#6EC4FF" stroke="#2A6B96" stroke-width="1"/>')

        elif name == "axis_dpad_left":
            # Highlighted left arrow (36 x 40)
            path = f"M {x+6},{y+20} L {x+22},{y+6} V {y+14} H {x+30} V {y+26} H {x+22} V {y+34} Z"
            svg.append(f'  <path d="{path}" fill="#6EC4FF" stroke="#2A6B96" stroke-width="1"/>')

        elif name == "axis_dpad_right":
            # Highlighted right arrow (36 x 40)
            path = f"M {x+30},{y+20} L {x+14},{y+34} V {y+26} H {x+6} V {y+14} H {x+14} V {y+6} Z"
            svg.append(f'  <path d="{path}" fill="#6EC4FF" stroke="#2A6B96" stroke-width="1"/>')

        elif name.startswith("jog_wheel_f"):
            # Jog Wheel frames (96 x 96)
            frame_num = int(name[11])
            angle = frame_num * 45
            cx, cy = x + 48, y + 48
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="38" fill="#04070D" stroke="#243349" stroke-width="1.5"/>')
            svg.append(f'  <circle cx="{cx}" cy="{cy}" r="32" fill="none" stroke="url(#gold-grad)" stroke-width="0.8" opacity="0.6"/>')
            # Rotating dot indicator
            rad = math.radians(angle)
            dot_x = cx + 32 * math.sin(rad)
            dot_y = cy - 32 * math.cos(rad)
            svg.append(f'  <circle cx="{dot_x}" cy="{dot_y}" r="5" fill="#6EC4FF"/>')
            svg.append(f'  <circle cx="{dot_x}" cy="{dot_y}" r="2" fill="#FFFFFF"/>')

        elif name == "panel_tl":
            # Top-Left corner (16 x 16)
            # Rounded corner path with rx=6
            path = f"M {x+16},{y} H {x+6} A 6 6 0 0 0 {x},{y+6} V {y+16} H {x+8} V {y+8} H {x+16} Z"
            svg.append(f'  <path d="{path}" fill="url(#glass-grad)" stroke="#243349" stroke-width="1"/>')
            # Top highlight line in ice-blue
            svg.append(f'  <path d="M {x+6},{y+1} H {x+16}" stroke="#6EC4FF" stroke-width="0.8" opacity="0.5"/>')

        elif name == "panel_tr":
            # Top-Right corner (16 x 16)
            path = f"M {x},{y} H {x+10} A 6 6 0 0 1 {x+16},{y+6} V {y+16} H {x+8} V {y+8} H {x} Z"
            svg.append(f'  <path d="{path}" fill="url(#glass-grad)" stroke="#243349" stroke-width="1"/>')
            # Top highlight line in ice-blue
            svg.append(f'  <path d="M {x},{y+1} H {x+10}" stroke="#6EC4FF" stroke-width="0.8" opacity="0.5"/>')

        elif name == "panel_bl":
            # Bottom-Left corner (16 x 16)
            path = f"M {x},{y} H {x+8} V {y+8} H {x+16} V {y+10} A 6 6 0 0 1 {x+10},{y+16} H {x} Z"
            # Actually, standard bottom-left corner with outer radius 6:
            path = f"M {x},{y} H {x+8} V {y+8} H {x+16} V {y+16} H {x+6} A 6 6 0 0 1 {x},{y+10} Z"
            svg.append(f'  <path d="{path}" fill="url(#glass-grad)" stroke="#243349" stroke-width="1"/>')

        elif name == "panel_br":
            # Bottom-Right corner (16 x 16)
            path = f"M {x},{y} H {x+16} V {y+10} A 6 6 0 0 1 {x+10},{y+16} H {x} V {y+8} H {x+8} Z"
            svg.append(f'  <path d="{path}" fill="url(#glass-grad)" stroke="#243349" stroke-width="1"/>')

        elif name == "panel_edge_t":
            # Top horizontal edge (64 x 8)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="url(#glass-grad)"/>')
            svg.append(f'  <line x1="{x}" y1="{y}" x2="{x+w}" y2="{y}" stroke="#243349" stroke-width="1"/>')
            svg.append(f'  <line x1="{x}" y1="{y}" x2="{x+w}" y2="{y}" stroke="#6EC4FF" stroke-width="0.8" opacity="0.5"/>')

        elif name == "panel_edge_b":
            # Bottom horizontal edge (64 x 8)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="url(#glass-grad)"/>')
            svg.append(f'  <line x1="{x}" y1="{y+h}" x2="{x+w}" y2="{y+h}" stroke="#243349" stroke-width="1"/>')

        elif name == "panel_edge_l":
            # Left vertical edge (8 x 64)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="url(#glass-grad)"/>')
            svg.append(f'  <line x1="{x}" y1="{y}" x2="{x}" y2="{y+h}" stroke="#243349" stroke-width="1"/>')

        elif name == "panel_edge_r":
            # Right vertical edge (8 x 64)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="url(#glass-grad)"/>')
            svg.append(f'  <line x1="{x+w}" y1="{y}" x2="{x+w}" y2="{y+h}" stroke="#243349" stroke-width="1"/>')

        elif name == "panel_center":
            # Panel Center (64 x 64)
            svg.append(f'  <rect x="{x}" y="{y}" width="{w}" height="{h}" fill="#0A1320"/>')
            # Subtle luxury background grid lines (optional / minimal)
            svg.append(f'  <line x1="{x}" y1="{y + 32}" x2="{x + 64}" y2="{y + 32}" stroke="#243349" stroke-width="0.5" opacity="0.3"/>')
            svg.append(f'  <line x1="{x + 32}" y1="{y}" x2="{x + 32}" y2="{y + 64}" stroke="#243349" stroke-width="0.5" opacity="0.3"/>')

        svg.append('</g>')

    svg.append('</svg>')
    return '\n'.join(svg)


def main():
    drake_svg = generate_drake()
    drake_dir = "art/style_drake_military"
    os.makedirs(drake_dir, exist_ok=True)
    drake_path = os.path.join(drake_dir, "sprite_sheet.svg")
    with open(drake_path, "w") as f:
        f.write(drake_svg)
    print(f"Generated {drake_path}")

    origin_svg = generate_origin()
    origin_dir = "art/style_origin_lux"
    os.makedirs(origin_dir, exist_ok=True)
    origin_path = os.path.join(origin_dir, "sprite_sheet.svg")
    with open(origin_path, "w") as f:
        f.write(origin_svg)
    print(f"Generated {origin_path}")


if __name__ == "__main__":
    main()
