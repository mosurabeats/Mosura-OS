#!/usr/bin/env python3
"""Tile docs/img/*.png screenshots into one contact sheet.
    python3 tools/sheet.py OUT.png name1 name2 ..."""
import sys, zlib, struct, os
IMG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'docs', 'img')

def readpng(p):
    d = open(p, 'rb').read(); i = 8; idat = b''
    while i < len(d):
        l = struct.unpack('>I', d[i:i+4])[0]; t = d[i+4:i+8]; c = d[i+8:i+8+l]
        if t == b'IHDR': w, h = struct.unpack('>II', c[:8])
        if t == b'IDAT': idat += c
        i += 12 + l
    raw = zlib.decompress(idat)
    return w, h, [raw[y*(w*3+1)+1:(y+1)*(w*3+1)] for y in range(h)]

def main():
    out, names = sys.argv[1], sys.argv[2:]
    imgs = [readpng(os.path.join(IMG, n + '.png')) for n in names]
    w, h = imgs[0][0], imgs[0][1]; cols = 2 if len(imgs) > 1 else 1; gap = 8
    rn = (len(imgs) + cols - 1) // cols
    W = cols*w + (cols+1)*gap; H = rn*h + (rn+1)*gap
    cv = [bytearray(b'\x30\x30\x34' * W) for _ in range(H)]
    for k, (iw, ih, rows) in enumerate(imgs):
        ox = gap + (k % cols)*(w+gap); oy = gap + (k // cols)*(h+gap)
        for y in range(ih): cv[oy+y][ox*3:(ox+iw)*3] = rows[y]
    raw = b''.join(b'\x00' + bytes(r) for r in cv)
    ch = lambda t, c: struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t+c) & 0xffffffff)
    open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))

main()
