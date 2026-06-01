import os, struct, zlib

W, H = 800, 1280
OUT = r"C:\Development\ESP32\ESPSCar\tools\mock_ui"
os.makedirs(OUT, exist_ok=True)


def png_chunk(tag, data):
    return struct.pack('!I', len(data)) + tag + data + struct.pack('!I', zlib.crc32(tag + data) & 0xffffffff)


def save_png(img, name):
    raw = bytearray()
    for row in img:
        raw.append(0)
        for r, g, b in row:
            raw.extend((r, g, b))
    comp = zlib.compress(bytes(raw), 6)
    png = bytearray(b'\x89PNG\r\n\x1a\n')
    ihdr = struct.pack('!IIBBBBB', W, H, 8, 2, 0, 0, 0)
    png.extend(png_chunk(b'IHDR', ihdr))
    png.extend(png_chunk(b'IDAT', comp))
    png.extend(png_chunk(b'IEND', b''))
    with open(os.path.join(OUT, name), 'wb') as f:
        f.write(png)


def new_img(bg):
    return [[list(bg) for _ in range(W)] for _ in range(H)]


def rect(img, x1, y1, x2, y2, c):
    x1 = max(0, x1); y1 = max(0, y1)
    x2 = min(W, x2); y2 = min(H, y2)
    for y in range(y1, y2):
        row = img[y]
        for x in range(x1, x2):
            row[x] = list(c)


def frame(img, x1, y1, x2, y2, c, t=2):
    rect(img, x1, y1, x2, y1+t, c)
    rect(img, x1, y2-t, x2, y2, c)
    rect(img, x1, y1, x1+t, y2, c)
    rect(img, x2-t, y1, x2, y2, c)

BG = (8, 14, 18)
PANEL = (18, 30, 38)
CARD = (28, 46, 58)
ACCENT = (0, 153, 255)
GOOD = (0, 255, 136)
WARN = (255, 170, 0)
DANGER = (255, 68, 68)
TEXT = (220, 236, 242)
MUTED = (90, 120, 140)

img = new_img(BG)
rect(img, 0, 0, W, 120, PANEL); rect(img, 0, 120, W, 200, CARD)
for i in range(4):
    y = 260 + i * 160; rect(img, 80, y, 720, y + 92, CARD); frame(img, 80, y, 720, y + 92, MUTED, 3)
rect(img, 80, 980, 360, 1080, ACCENT); rect(img, 440, 980, 720, 1080, GOOD)
rect(img, 0, 1160, W, H, PANEL); frame(img, 24, 24, 776, 1256, MUTED, 4)
save_png(img, 'screen_pairing.png')

img = new_img(BG)
rect(img, 0, 0, W, 110, PANEL); rect(img, 0, 110, W, 170, CARD)
rect(img, 40, 220, 380, 520, CARD); rect(img, 420, 220, 760, 520, CARD)
for i, c in enumerate((GOOD, GOOD, WARN, WARN, DANGER, DANGER)):
    x = 70 + (i % 3) * 230; y = 590 + (i // 3) * 130; rect(img, x, y, x + 190, y + 100, c); frame(img, x, y, x + 190, y + 100, TEXT, 2)
rect(img, 40, 900, 760, 1040, CARD); rect(img, 0, 1140, W, H, PANEL)
for i in range(5):
    x = 20 + i * 156; rect(img, x, 1168, x + 140, 1252, ACCENT if i == 2 else CARD)
frame(img, 24, 24, 776, 1256, MUTED, 4)
save_png(img, 'screen_console.png')

img = new_img(BG)
rect(img, 0, 0, W, 120, PANEL)
for i in range(7):
    y = 180 + i * 130; rect(img, 40, y, 760, y + 96, CARD); rect(img, 620, y + 22, 730, y + 74, GOOD if i % 2 == 0 else MUTED)
rect(img, 80, 1130, 360, 1230, ACCENT); rect(img, 440, 1130, 720, 1230, WARN)
frame(img, 24, 24, 776, 1256, MUTED, 4)
save_png(img, 'screen_settings.png')

img = new_img(BG)
rect(img, 0, 0, W, 120, PANEL); rect(img, 80, 260, 720, 760, CARD)
rect(img, 140, 860, 660, 930, PANEL); frame(img, 140, 860, 660, 930, MUTED, 3)
rect(img, 146, 866, 486, 924, ACCENT)
rect(img, 80, 980, 720, 1060, CARD); rect(img, 220, 1120, 580, 1220, GOOD)
frame(img, 24, 24, 776, 1256, MUTED, 4)
save_png(img, 'screen_ota.png')

print('Generated PNG files in', OUT)
