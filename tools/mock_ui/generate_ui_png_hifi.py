import os
import struct
import zlib

W, H = 800, 1280
OUT = r"C:\Development\ESP32\ESPSCar\tools\mock_ui"
os.makedirs(OUT, exist_ok=True)


def png_chunk(tag, data):
    return struct.pack("!I", len(data)) + tag + data + struct.pack("!I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def save_png(img, name):
    raw = bytearray()
    for row in img:
        raw.append(0)
        for r, g, b in row:
            raw.extend((r, g, b))
    comp = zlib.compress(bytes(raw), 6)

    blob = bytearray(b"\x89PNG\r\n\x1a\n")
    ihdr = struct.pack("!IIBBBBB", W, H, 8, 2, 0, 0, 0)
    blob.extend(png_chunk(b"IHDR", ihdr))
    blob.extend(png_chunk(b"IDAT", comp))
    blob.extend(png_chunk(b"IEND", b""))

    with open(os.path.join(OUT, name), "wb") as f:
        f.write(blob)


# 5x7 font (uppercase + digits + punctuation used in labels)
F = {
    " ": ["00000", "00000", "00000", "00000", "00000", "00000", "00000"],
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11100", "10010", "10001", "10001", "10001", "10010", "11100"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01110"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    "J": ["00111", "00010", "00010", "00010", "10010", "10010", "01100"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "10001", "11001", "10101", "10011", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "10000", "11110", "00001", "00001", "11110"],
    "6": ["01110", "10000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00001", "01110"],
    ":": ["00000", "00100", "00100", "00000", "00100", "00100", "00000"],
    ".": ["00000", "00000", "00000", "00000", "00000", "00110", "00110"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    "/": ["00001", "00010", "00100", "01000", "10000", "00000", "00000"],
    "%": ["11001", "11010", "00100", "01000", "10110", "00110", "00000"],
    "[": ["01110", "01000", "01000", "01000", "01000", "01000", "01110"],
    "]": ["01110", "00010", "00010", "00010", "00010", "00010", "01110"],
}


BG = (7, 12, 17)
PANEL = (16, 28, 38)
CARD = (26, 44, 58)
CARD2 = (22, 36, 48)
ACCENT = (0, 153, 255)
GOOD = (0, 255, 136)
WARN = (255, 170, 0)
DANGER = (255, 68, 68)
TEXT = (225, 238, 245)
MUTED = (116, 145, 163)


def new_img(bg):
    return [[list(bg) for _ in range(W)] for _ in range(H)]


def put_px(img, x, y, c):
    if 0 <= x < W and 0 <= y < H:
        img[y][x] = [c[0], c[1], c[2]]


def rect(img, x1, y1, x2, y2, c):
    x1 = max(0, x1)
    y1 = max(0, y1)
    x2 = min(W, x2)
    y2 = min(H, y2)
    for y in range(y1, y2):
        row = img[y]
        for x in range(x1, x2):
            row[x] = [c[0], c[1], c[2]]


def gradient_v(img, x1, y1, x2, y2, c_top, c_bot):
    x1 = max(0, x1)
    y1 = max(0, y1)
    x2 = min(W, x2)
    y2 = min(H, y2)
    h = max(1, y2 - y1)
    for y in range(y1, y2):
        t = (y - y1) / (h - 1 if h > 1 else 1)
        c = (
            int(c_top[0] * (1 - t) + c_bot[0] * t),
            int(c_top[1] * (1 - t) + c_bot[1] * t),
            int(c_top[2] * (1 - t) + c_bot[2] * t),
        )
        row = img[y]
        for x in range(x1, x2):
            row[x] = [c[0], c[1], c[2]]


def frame(img, x1, y1, x2, y2, c, t=2):
    rect(img, x1, y1, x2, y1 + t, c)
    rect(img, x1, y2 - t, x2, y2, c)
    rect(img, x1, y1, x1 + t, y2, c)
    rect(img, x2 - t, y1, x2, y2, c)


def bar(img, x1, y1, x2, y2, pct, c_fill):
    rect(img, x1, y1, x2, y2, CARD2)
    frame(img, x1, y1, x2, y2, MUTED, 2)
    w = max(0, min(x2 - x1 - 4, int((x2 - x1 - 4) * pct)))
    rect(img, x1 + 2, y1 + 2, x1 + 2 + w, y2 - 2, c_fill)


def draw_char(img, ch, x, y, c, s=3):
    glyph = F.get(ch, F[" "])
    for gy, row in enumerate(glyph):
        for gx, bit in enumerate(row):
            if bit == "1":
                rect(img, x + gx * s, y + gy * s, x + (gx + 1) * s, y + (gy + 1) * s, c)


def draw_text(img, txt, x, y, c, s=3, sp=1):
    cx = x
    for ch in txt.upper():
        draw_char(img, ch, cx, y, c, s)
        cx += (5 * s) + sp * s


# Pairing screen (high-fidelity mock)
img = new_img(BG)
gradient_v(img, 0, 0, W, H, (6, 10, 14), (14, 22, 30))
gradient_v(img, 0, 0, W, 160, (18, 36, 48), (13, 24, 33))
frame(img, 16, 16, 784, 1264, (80, 110, 130), 3)

# header
draw_text(img, "SC TERMINAL", 44, 44, TEXT, 4)
draw_text(img, "INITIAL SETUP", 46, 96, MUTED, 2)
bar(img, 520, 46, 744, 88, 0.20, WARN)
draw_text(img, "20%", 650, 56, TEXT, 2)

# form cards
labels = ["WIFI STATUS: NOT CONNECTED", "SSID", "PASSWORD", "BRIDGE HOST", "BRIDGE PORT"]
ys = [200, 330, 470, 610, 750]
for i, y in enumerate(ys):
    rect(img, 56, y, 744, y + 96, CARD)
    frame(img, 56, y, 744, y + 96, MUTED, 2)
    draw_text(img, labels[i], 76, y + 30, TEXT if i == 0 else MUTED, 2)

# buttons
rect(img, 56, 920, 360, 1020, CARD2)
frame(img, 56, 920, 360, 1020, ACCENT, 3)
draw_text(img, "TEST CONNECTION", 90, 958, ACCENT, 2)
rect(img, 440, 920, 744, 1020, GOOD)
frame(img, 440, 920, 744, 1020, TEXT, 3)
draw_text(img, "SAVE CONTINUE", 484, 958, (8, 22, 14), 2)

draw_text(img, "TOUCH: GT911 ONLINE", 60, 1090, GOOD, 2)
rect(img, 0, 1160, W, H, PANEL)
for i, n in enumerate(["HOME", "PAIR", "SHIP", "NET", "SET"]):
    x = 16 + i * 156
    rect(img, x, 1178, x + 140, 1252, ACCENT if i == 1 else CARD)
    draw_text(img, n, x + 36, 1206, TEXT, 2)

save_png(img, "screen_pairing_hifi.png")


# Console screen (high-fidelity mock)
img = new_img(BG)
gradient_v(img, 0, 0, W, H, (8, 13, 18), (16, 24, 32))
frame(img, 16, 16, 784, 1264, (80, 110, 130), 3)
rect(img, 0, 0, W, 122, PANEL)
draw_text(img, "CUTLASS BLACK", 38, 38, TEXT, 3)
draw_text(img, "PILOT MFD LEFT", 42, 78, MUTED, 2)
rect(img, 500, 28, 760, 94, CARD)
frame(img, 500, 28, 760, 94, GOOD, 2)
draw_text(img, "LINK ONLINE", 536, 52, GOOD, 2)

# status cards
rect(img, 40, 160, 380, 500, CARD)
frame(img, 40, 160, 380, 500, MUTED, 2)
draw_text(img, "SHIELDS", 74, 190, TEXT, 3)
bar(img, 80, 250, 340, 292, 0.84, GOOD)
draw_text(img, "FORE 84%", 90, 260, TEXT, 2)
bar(img, 80, 314, 340, 356, 0.79, GOOD)
draw_text(img, "LEFT 79%", 90, 324, TEXT, 2)
bar(img, 80, 378, 340, 420, 0.76, WARN)
draw_text(img, "RIGHT 76%", 90, 388, TEXT, 2)
bar(img, 80, 442, 340, 484, 0.82, GOOD)
draw_text(img, "REAR 82%", 90, 452, TEXT, 2)

rect(img, 420, 160, 760, 500, CARD)
frame(img, 420, 160, 760, 500, MUTED, 2)
draw_text(img, "WEAPONS", 468, 190, TEXT, 3)
rect(img, 470, 252, 710, 330, DANGER)
frame(img, 470, 252, 710, 330, TEXT, 2)
draw_text(img, "GROUP A ARMED", 500, 283, (28, 8, 8), 2)
rect(img, 470, 350, 710, 428, WARN)
frame(img, 470, 350, 710, 428, TEXT, 2)
draw_text(img, "MISSILES READY", 504, 381, (40, 30, 0), 2)

# action grid
draw_text(img, "SYSTEM ACTIONS", 46, 546, MUTED, 2)
actions = ["POWER", "ENG", "COMMS", "NAV", "SCAN", "GEAR"]
for i, a in enumerate(actions):
    x = 44 + (i % 3) * 248
    y = 584 + (i // 3) * 134
    rect(img, x, y, x + 220, y + 108, CARD2)
    frame(img, x, y, x + 220, y + 108, ACCENT if a == "NAV" else MUTED, 2)
    draw_text(img, a, x + 72, y + 42, TEXT, 2)

rect(img, 40, 876, 760, 1050, CARD)
frame(img, 40, 876, 760, 1050, MUTED, 2)
draw_text(img, "ALERTS", 64, 906, TEXT, 2)
draw_text(img, "NO ACTIVE ALERTS", 68, 958, GOOD, 2)
draw_text(img, "PING 21MS FPS 60", 522, 958, MUTED, 2)

rect(img, 0, 1144, W, H, PANEL)
for i, n in enumerate(["HOME", "COMBAT", "NAV", "MFD", "SET"]):
    x = 16 + i * 156
    rect(img, x, 1178, x + 140, 1252, ACCENT if n == "NAV" else CARD)
    draw_text(img, n, x + 34, 1206, TEXT, 2)

save_png(img, "screen_console_hifi.png")


# Settings screen (high-fidelity mock)
img = new_img(BG)
gradient_v(img, 0, 0, W, H, (8, 13, 18), (14, 22, 30))
frame(img, 16, 16, 784, 1264, (80, 110, 130), 3)
rect(img, 0, 0, W, 120, PANEL)
draw_text(img, "SETTINGS", 44, 42, TEXT, 4)
draw_text(img, "TERMINAL D1001", 48, 92, MUTED, 2)

rows = [
    ("DISPLAY BRIGHTNESS", "78%", True),
    ("HAPTIC FEEDBACK", "ON", True),
    ("HID OUTPUT", "ON", True),
    ("WIFI AUTORECONNECT", "ON", True),
    ("BRIDGE KEEPALIVE", "ON", True),
    ("DEBUG LOG LEVEL", "INFO", False),
    ("FACTORY RESET", "ARMED", False),
]
for i, (name, val, ok) in enumerate(rows):
    y = 180 + i * 132
    rect(img, 40, y, 760, y + 98, CARD)
    frame(img, 40, y, 760, y + 98, MUTED, 2)
    draw_text(img, name, 62, y + 30, TEXT, 2)
    rect(img, 600, y + 22, 730, y + 74, GOOD if ok else WARN)
    draw_text(img, val, 620, y + 40, (10, 25, 12) if ok else (45, 35, 0), 2)

rect(img, 80, 1126, 360, 1228, ACCENT)
frame(img, 80, 1126, 360, 1228, TEXT, 2)
draw_text(img, "SAVE", 178, 1166, TEXT, 3)
rect(img, 440, 1126, 720, 1228, CARD2)
frame(img, 440, 1126, 720, 1228, MUTED, 2)
draw_text(img, "CANCEL", 520, 1166, MUTED, 2)

save_png(img, "screen_settings_hifi.png")


# OTA screen (high-fidelity mock)
img = new_img(BG)
gradient_v(img, 0, 0, W, H, (7, 12, 17), (13, 22, 30))
frame(img, 16, 16, 784, 1264, (80, 110, 130), 3)
rect(img, 0, 0, W, 120, PANEL)
draw_text(img, "OTA UPDATE", 44, 44, TEXT, 4)
draw_text(img, "PACKAGE SC-TERMINAL 1.0.1", 46, 96, MUTED, 2)

rect(img, 60, 190, 740, 760, CARD)
frame(img, 60, 190, 740, 760, MUTED, 2)
steps = [
    ("DOWNLOAD", True),
    ("VERIFY", True),
    ("FLASH", True),
    ("REBOOT", False),
]
for i, (s, done) in enumerate(steps):
    y = 250 + i * 110
    rect(img, 100, y, 700, y + 84, CARD2)
    frame(img, 100, y, 700, y + 84, GOOD if done else MUTED, 2)
    draw_text(img, s, 140, y + 30, TEXT, 2)
    draw_text(img, "DONE" if done else "WAIT", 560, y + 30, GOOD if done else WARN, 2)

bar(img, 100, 760, 700, 840, 0.68, ACCENT)
draw_text(img, "68%", 380, 790, TEXT, 3)

draw_text(img, "EST REMAINING 00:32", 230, 900, MUTED, 2)
rect(img, 220, 1080, 580, 1200, GOOD)
frame(img, 220, 1080, 580, 1200, TEXT, 3)
draw_text(img, "CONTINUE", 304, 1130, (8, 22, 14), 3)

save_png(img, "screen_ota_hifi.png")

print("Generated high-fidelity PNGs:")
for fn in [
    "screen_pairing_hifi.png",
    "screen_console_hifi.png",
    "screen_settings_hifi.png",
    "screen_ota_hifi.png",
]:
    print(os.path.join(OUT, fn))
