import os, struct, zlib

W, H = 800, 1280
OUT = r"C:\Development\ESP32\ESPSCar\tools\mock_ui\hifi"
os.makedirs(OUT, exist_ok=True)

# 5x7 bitmap font (subset needed for UI labels)
FONT = {
    'A':[0x0E,0x11,0x11,0x1F,0x11,0x11,0x11], 'B':[0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E],
    'C':[0x0E,0x11,0x10,0x10,0x10,0x11,0x0E], 'D':[0x1E,0x11,0x11,0x11,0x11,0x11,0x1E],
    'E':[0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F], 'F':[0x1F,0x10,0x10,0x1E,0x10,0x10,0x10],
    'G':[0x0E,0x11,0x10,0x17,0x11,0x11,0x0E], 'H':[0x11,0x11,0x11,0x1F,0x11,0x11,0x11],
    'I':[0x1F,0x04,0x04,0x04,0x04,0x04,0x1F], 'J':[0x1F,0x02,0x02,0x02,0x12,0x12,0x0C],
    'K':[0x11,0x12,0x14,0x18,0x14,0x12,0x11], 'L':[0x10,0x10,0x10,0x10,0x10,0x10,0x1F],
    'M':[0x11,0x1B,0x15,0x15,0x11,0x11,0x11], 'N':[0x11,0x11,0x19,0x15,0x13,0x11,0x11],
    'O':[0x0E,0x11,0x11,0x11,0x11,0x11,0x0E], 'P':[0x1E,0x11,0x11,0x1E,0x10,0x10,0x10],
    'Q':[0x0E,0x11,0x11,0x11,0x15,0x12,0x0D], 'R':[0x1E,0x11,0x11,0x1E,0x14,0x12,0x11],
    'S':[0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E], 'T':[0x1F,0x04,0x04,0x04,0x04,0x04,0x04],
    'U':[0x11,0x11,0x11,0x11,0x11,0x11,0x0E], 'V':[0x11,0x11,0x11,0x11,0x11,0x0A,0x04],
    'W':[0x11,0x11,0x11,0x15,0x15,0x15,0x0A], 'X':[0x11,0x11,0x0A,0x04,0x0A,0x11,0x11],
    'Y':[0x11,0x11,0x0A,0x04,0x04,0x04,0x04], 'Z':[0x1F,0x01,0x02,0x04,0x08,0x10,0x1F],
    '0':[0x0E,0x11,0x13,0x15,0x19,0x11,0x0E], '1':[0x04,0x0C,0x04,0x04,0x04,0x04,0x0E],
    '2':[0x0E,0x11,0x01,0x02,0x04,0x08,0x1F], '3':[0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E],
    '4':[0x02,0x06,0x0A,0x12,0x1F,0x02,0x02], '5':[0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E],
    '6':[0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E], '7':[0x1F,0x01,0x02,0x04,0x08,0x08,0x08],
    '8':[0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E], '9':[0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E],
    ':':[0x00,0x04,0x00,0x00,0x04,0x00,0x00], '-':[0x00,0x00,0x00,0x1F,0x00,0x00,0x00],
    '.':[0x00,0x00,0x00,0x00,0x00,0x0C,0x0C], '/':[0x01,0x02,0x04,0x08,0x10,0x00,0x00],
    '%':[0x18,0x19,0x02,0x04,0x08,0x13,0x03], '(': [0x02,0x04,0x08,0x08,0x08,0x04,0x02],
    ')': [0x08,0x04,0x02,0x02,0x02,0x04,0x08], ' ': [0,0,0,0,0,0,0], ',':[0,0,0,0,0,4,8],
}

def png_chunk(tag, data):
    return struct.pack('!I', len(data)) + tag + data + struct.pack('!I', zlib.crc32(tag + data) & 0xffffffff)

def save_png(img, name):
    raw = bytearray()
    for row in img:
        raw.append(0)
        for r,g,b in row:
            raw.extend((r,g,b))
    comp = zlib.compress(bytes(raw), 6)
    png = bytearray(b'\x89PNG\r\n\x1a\n')
    png.extend(png_chunk(b'IHDR', struct.pack('!IIBBBBB', W, H, 8, 2, 0, 0, 0)))
    png.extend(png_chunk(b'IDAT', comp))
    png.extend(png_chunk(b'IEND', b''))
    with open(os.path.join(OUT, name), 'wb') as f:
        f.write(png)

def new_img(bg):
    return [[list(bg) for _ in range(W)] for _ in range(H)]

def rect(img, x1,y1,x2,y2,c):
    x1=max(0,x1); y1=max(0,y1); x2=min(W,x2); y2=min(H,y2)
    for y in range(y1,y2):
        row = img[y]
        for x in range(x1,x2): row[x] = list(c)

def frame(img, x1,y1,x2,y2,c,t=2):
    rect(img,x1,y1,x2,y1+t,c); rect(img,x1,y2-t,x2,y2,c)
    rect(img,x1,y1,x1+t,y2,c); rect(img,x2-t,y1,x2,y2,c)

def grad(img, top, bot):
    for y in range(H):
        a = y/(H-1)
        c = [int(top[i]*(1-a)+bot[i]*a) for i in range(3)]
        row = img[y]
        for x in range(W): row[x] = c[:]

def text(img, x, y, s, c, scale=2):
    cx = x
    for ch in s.upper():
        g = FONT.get(ch, FONT[' '])
        for ry, bits in enumerate(g):
            for rx in range(5):
                if bits & (1 << (4-rx)):
                    rect(img, cx+rx*scale, y+ry*scale, cx+(rx+1)*scale, y+(ry+1)*scale, c)
        cx += 6*scale

BG0=(7,12,18); BG1=(13,26,34); PANEL=(16,28,38); CARD=(24,44,58)
ACC=(0,153,255); GOOD=(0,255,136); WARN=(255,170,0); BAD=(255,68,68)
TXT=(224,238,246); SUB=(133,163,182); DIM=(64,92,110)

# 1 Pairing
img = new_img(BG0); grad(img,BG0,BG1)
rect(img,0,0,W,126,PANEL); frame(img,0,0,W,126,DIM,3)
text(img,32,28,'SC TERMINAL',TXT,3); text(img,32,74,'FIRST BOOT PAIRING',SUB,2)
for i,l in enumerate(['WIFI SSID','WIFI PASSWORD','BRIDGE HOST','BRIDGE PORT']):
    y = 210 + i*170
    rect(img,70,y,730,y+115,CARD); frame(img,70,y,730,y+115,DIM,3)
    text(img,94,y+18,l,SUB,2)
    text(img,94,y+62,'....................................',TXT,2)
rect(img,70,950,380,1060,ACC); frame(img,70,950,380,1060,TXT,2); text(img,132,992,'TEST CONNECTION',TXT,2)
rect(img,420,950,730,1060,GOOD); frame(img,420,950,730,1060,TXT,2); text(img,503,992,'SAVE + NEXT',BG0,2)
rect(img,0,1148,W,H,PANEL); text(img,40,1188,'TOUCH: GT911 READY  /  USB HID: ONLINE',SUB,2)
save_png(img,'hifi_pairing.png')

# 2 Console
img = new_img(BG0); grad(img,(6,14,20),(10,22,28))
rect(img,0,0,W,110,PANEL); text(img,28,28,'CUTLASS BLACK - PILOT MFD LEFT',TXT,2); text(img,28,70,'LINK ONLINE  PING 21MS  FPS 60',SUB,2)
rect(img,36,140,388,510,CARD); frame(img,36,140,388,510,DIM,3)
text(img,58,166,'SHIELDS',TXT,2)
for i,(n,v,c) in enumerate([('FORE',84,GOOD),('LEFT',78,WARN),('RIGHT',76,WARN),('REAR',81,GOOD)]):
    y=210+i*68
    text(img,58,y,n,SUB,2); rect(img,160,y-2,350,y+28,(18,30,40)); frame(img,160,y-2,350,y+28,DIM,2)
    rect(img,164,y+2,164+int(1.82*v),y+24,c); text(img,360,y,f'{v}%',TXT,2)
rect(img,412,140,764,510,CARD); frame(img,412,140,764,510,DIM,3)
text(img,434,166,'WEAPONS',TXT,2)
for i,(lbl,c) in enumerate([('GROUP A READY',GOOD),('GROUP B STBY',WARN),('MISSILES SAFE',SUB)]):
    y=230+i*88; rect(img,434,y,742,y+62,(18,30,40)); frame(img,434,y,742,y+62,DIM,2); text(img,456,y+20,lbl,c,2)
text(img,434,504,'ALERTS: NONE',GOOD,2)
text(img,38,560,'SYSTEMS',TXT,2)
btn=[('POWER',ACC),('ENGINES',ACC),('COMMS',ACC),('NAV',ACC),('SCAN',ACC),('GEAR',WARN)]
for i,(n,c) in enumerate(btn):
    x=42+(i%3)*246; y=600+(i//3)*132; rect(img,x,y,x+220,y+94,(22,36,48)); frame(img,x,y,x+220,y+94,c,3); text(img,x+64,y+36,n,TXT,2)
rect(img,0,1128,W,H,PANEL)
for i,n in enumerate(['HOME','COMBAT','NAV','MFD','SETTINGS']):
    x=20+i*156; c=ACC if n=='MFD' else CARD
    rect(img,x,1158,x+140,1246,c); frame(img,x,1158,x+140,1246,TXT if n=='MFD' else DIM,2); text(img,x+38,1190,n,TXT if n!='MFD' else BG0,2)
save_png(img,'hifi_console.png')

# 3 Settings
img = new_img(BG0); grad(img,(10,18,24),(14,26,34))
rect(img,0,0,W,118,PANEL); text(img,30,30,'SETTINGS',TXT,3); text(img,30,78,'TERMINAL + NETWORK + HID',SUB,2)
rows=['BRIGHTNESS','HAPTICS','SOUND FX','HID ENABLED','WIFI AUTO-RETRY','BRIDGE AUTO-CONNECT','DEVELOPER LOGS']
for i,r in enumerate(rows):
    y=160+i*132; rect(img,36,y,764,y+96,CARD); frame(img,36,y,764,y+96,DIM,2); text(img,64,y+34,r,TXT,2)
    rect(img,618,y+22,738,y+74,GOOD if i in (0,3,4,5) else DIM); frame(img,618,y+22,738,y+74,TXT,2)
    text(img,644,y+40,'ON' if i in (0,3,4,5) else 'OFF',BG0 if i in (0,3,4,5) else SUB,2)
rect(img,70,1118,370,1230,ACC); frame(img,70,1118,370,1230,TXT,2); text(img,152,1162,'SAVE',TXT,3)
rect(img,430,1118,730,1230,WARN); frame(img,430,1118,730,1230,TXT,2); text(img,500,1162,'FACTORY RESET',BG0,2)
save_png(img,'hifi_settings.png')

# 4 OTA
img = new_img(BG0); grad(img,(8,16,22),(12,24,30))
rect(img,0,0,W,118,PANEL); text(img,30,30,'OTA UPDATE',TXT,3); text(img,30,78,'FIRMWARE + UI ASSETS',SUB,2)
rect(img,70,180,730,790,CARD); frame(img,70,180,730,790,DIM,3)
text(img,108,232,'CURRENT VERSION: 1.0.0',SUB,2)
text(img,108,286,'TARGET VERSION : 1.1.0',TXT,2)
text(img,108,350,'STEP 1/4 DOWNLOAD PACKAGE',TXT,2)
text(img,108,406,'STEP 2/4 VERIFY SIGNATURE',TXT,2)
text(img,108,462,'STEP 3/4 FLASH OTA SLOT',GOOD,2)
text(img,108,518,'STEP 4/4 REBOOT + HEALTHCHECK',SUB,2)
rect(img,108,626,692,700,(14,24,32)); frame(img,108,626,692,700,DIM,3)
p=0.68; rect(img,114,632,114+int((692-114)*p),694,ACC)
text(img,360,654,'68%',TXT,2)
rect(img,70,840,730,1030,CARD); frame(img,70,840,730,1030,DIM,2)
text(img,100,890,'DO NOT POWER OFF DEVICE',WARN,2)
text(img,100,940,'ETA: 00:47',TXT,2)
rect(img,210,1118,590,1230,GOOD); frame(img,210,1118,590,1230,TXT,2); text(img,300,1162,'RESTART NOW',BG0,3)
save_png(img,'hifi_ota.png')

print('Generated high-fidelity PNG files in', OUT)
for n in ['hifi_pairing.png','hifi_console.png','hifi_settings.png','hifi_ota.png']:
    print(os.path.join(OUT,n))
