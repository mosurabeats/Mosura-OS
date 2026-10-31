#!/usr/bin/env python3
"""mospatch: the Mosura OS firmware toolchain.

Mosura never ships Roland's firmware. You download the official System
Program from Roland yourself; this tool works on your copy, on your machine.

    mospatch.py inspect  SP404MKII_APP1.bin          check version, list regions
    mospatch.py unpack   SP404MKII_APP1.bin OUTDIR   write each RAM region as .bin
    mospatch.py hooks                                 show hook status (port/sp404/hooks.json)
    mospatch.py build    SP404MKII_APP1.bin OUT.bin   patch (refuses unverified hooks)
    mospatch.py selftest                              decoder round-trip tests

APP1 facts (from the DM-404 project's notes, see docs/HARDWARE.md): it is
flashed at 0x60080000 and starts with Arm Compiler startup code. The words at
file offset 0x80/0x84 bound the __scatterload table (relative to 0x80); each
16-byte row is (load address, exec address, size, handler). Handlers are a
plain copy, armlink's LZ77-style decompressor, or zero-init.
"""
import hashlib, json, os, struct, sys

FLASH_BASE = 0x60080000
KNOWN = {
    '4a3d67711e14dcc97d50249a4eee7dd6df0251a2f37757cbbe2556c233730d80': '5.52',
}
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOOKS = os.path.join(ROOT, 'port', 'sp404', 'hooks.json')


# ---- armlink RW decompression ------------------------------------------
# Token byte: bits 0-2 literal count + 1 (0: count in next byte), bit 3 set:
# back-reference, clear: zero run; bits 4-7 run length (0: in next byte).
# A back-reference copies (run + 2) bytes from `offset` bytes back.
def decompress(src, outlen):
    out = bytearray(); i = 0
    while len(out) < outlen:
        tok = src[i]; i += 1
        lit = tok & 7
        if lit == 0: lit = src[i]; i += 1
        run = tok >> 4
        if run == 0: run = src[i]; i += 1
        n = lit - 1
        if n > 0: out += src[i:i + n]; i += n
        if tok & 8:
            off = src[i]; i += 1
            start = len(out) - off
            for k in range(run + 2): out.append(out[start + k])
        else:
            out += b'\x00' * run
    return bytes(out[:outlen]), i


def compress(data):
    """Tiny matching encoder, only for selftest round trips."""
    out = bytearray(); i = 0; lits = bytearray()
    def flush(kind, run, off=None):
        nonlocal lits
        lit = len(lits) + 1
        tok = (lit if lit < 8 else 0) | (8 if kind == 'ref' else 0) | ((run if run < 16 else 0) << 4)
        out.append(tok)
        if lit >= 8: out.append(lit)
        if run >= 16 or run == 0: out.append(run)
        out.extend(lits); lits = bytearray()
        if kind == 'ref': out.append(off)
    while i < len(data):
        z = 0
        while i + z < len(data) and data[i + z] == 0 and z < 255: z += 1
        best = (0, 0)
        for off in range(1, min(255, i) + 1):
            l = 0
            while i + l < len(data) and data[i - off + l] == data[i + l] and l < 257: l += 1
            if l > best[0]: best = (l, off)
        if z >= 3 and z >= best[0]: flush('zero', z); i += z
        elif best[0] >= 3: flush('ref', best[0] - 2, best[1]); i += best[0]
        else:
            lits.append(data[i]); i += 1
            if len(lits) == 254: flush('zero', 0)      # literal count tops out at 254
    if lits: flush('zero', 0)
    return bytes(out)


# ---- image ---------------------------------------------------------------
def load(path):
    data = open(path, 'rb').read()
    sha = hashlib.sha256(data).hexdigest()
    return data, sha, KNOWN.get(sha)


def regions(data):
    rel0, rel1 = struct.unpack_from('<2I', data, 0x80)
    rows = [struct.unpack_from('<4I', data, off) for off in range(0x80 + rel0, 0x80 + rel1, 16)]
    if not rows: raise ValueError('no scatter table where expected (offset 0x80)')
    copy_fn = rows[0][3]
    others = sorted({r[3] for r in rows} - {copy_fn})
    if len(others) != 2: raise ValueError('unexpected scatter handlers: %r' % others)
    kind = {copy_fn: 'copy', others[0]: 'decompress', others[1]: 'zero'}
    return [dict(kind=kind[h], load=l, file=l - FLASH_BASE, exec=e, size=s) for l, e, s, h in rows]


def cmd_inspect(path):
    data, sha, ver = load(path)
    print('file     %s (%d bytes)' % (path, len(data)))
    print('sha256   %s' % sha)
    print('version  %s' % (ver or 'UNKNOWN - Mosura supports only the versions listed in KNOWN'))
    try:
        for r in regions(data):
            print('  %-10s file 0x%06x -> 0x%08x  size 0x%07x' % (r['kind'], r['file'] if r['kind'] != 'zero' else 0, r['exec'], r['size']))
    except Exception as e:
        print('regions  could not parse: %s' % e)
    return 0 if ver else 2


def cmd_unpack(path, outdir):
    data, sha, ver = load(path)
    if not ver: print('warning: unknown firmware version (%s)' % sha[:16])
    os.makedirs(outdir, exist_ok=True)
    for r in regions(data):
        if r['kind'] == 'zero': continue
        blob = data[r['file']:r['file'] + r['size']] if r['kind'] == 'copy' else decompress(data[r['file']:], r['size'])[0]
        name = 'r_%08x_%s.bin' % (r['exec'], r['kind'])
        open(os.path.join(outdir, name), 'wb').write(blob)
        print('wrote', name, len(blob))
    print('Load these at their exec addresses in Ghidra (ARM Cortex, Thumb, little endian).')


def load_hooks():
    with open(HOOKS) as f: return json.load(f)


def cmd_hooks():
    h = load_hooks()
    print('target firmware %s' % h['firmware'])
    for x in h['hooks']:
        print('  [%-10s] %-18s %s' % (x['status'], x['name'], x.get('where', '')))
    todo = [x['name'] for x in h['hooks'] if x['status'] != 'verified']
    print('\n%d of %d hooks verified.' % (len(h['hooks']) - len(todo), len(h['hooks'])))
    return 0


def cmd_build(path, out, allow_unverified=False):
    data, sha, ver = load(path)
    h = load_hooks()
    if ver != h['firmware']:
        print('refusing: this is not the official %s System Program (sha256 %s...)' % (h['firmware'], sha[:16])); return 2
    todo = [x['name'] for x in h['hooks'] if x['status'] != 'verified']
    if todo and not allow_unverified:
        print('refusing to write a flashable image: unverified hooks: %s' % ', '.join(todo))
        print('Verify them in the DM-404 emulator first (docs/ROADMAP.md, phase 1).')
        return 3
    # Patch application is defined per verified hook (expected bytes are
    # checked before any write). With no verified hooks there is nothing to do.
    img = bytearray(data)
    for x in h['hooks']:
        for p in x.get('patches', []):
            off = int(p['file_offset'], 16); exp = bytes.fromhex(p['expect']); new = bytes.fromhex(p['data'])
            if img[off:off + len(exp)] != exp:
                print('refusing: bytes at 0x%x are not what %s expects' % (off, x['name'])); return 4
            img[off:off + len(new)] = new
    open(out, 'wb').write(img)
    print('wrote %s sha256 %s' % (out, hashlib.sha256(img).hexdigest()))
    print('EMULATOR ONLY' if todo else 'Test in the emulator before any unit.')
    return 0


def cmd_selftest():
    import random
    rnd = random.Random(404)
    cases = [b'', b'\x00' * 300, bytes(range(256)) * 3, b'MOSURA' * 50]
    cases += [bytes(rnd.choice([0, 0, 0, rnd.randrange(256)]) for _ in range(rnd.randrange(1, 2000))) for _ in range(40)]
    for c in cases:
        enc = compress(c)
        dec, used = decompress(enc, len(c)) if c else (b'', 0)
        assert dec == c, 'round trip failed (len %d)' % len(c)
    print('selftest: %d decompressor round trips ok' % len(cases))
    # a synthetic APP1 with a copy, a compressed and a zero region
    payload = b'RW' + b'\x00' * 40 + b'MOSURA' * 8
    comp = compress(payload)
    img = bytearray(0x400)
    struct.pack_into('<2I', img, 0x80, 0x80, 0xb0)                    # table 0x100..0x130
    struct.pack_into('<4I', img, 0x100, FLASH_BASE + 0x200, 0x400, 16, 0x1001)
    struct.pack_into('<4I', img, 0x110, FLASH_BASE + 0x300, 0x20000000, len(payload), 0x2001)
    struct.pack_into('<4I', img, 0x120, 0, 0x20001000, 0x800, 0x3001)
    img[0x200:0x210] = b'VECTORS-12345678'; img[0x300:0x300 + len(comp)] = comp
    r = regions(bytes(img))
    assert [x['kind'] for x in r] == ['copy', 'decompress', 'zero'], r
    assert decompress(bytes(img[r[1]['file']:]), r[1]['size'])[0] == payload
    print('selftest: scatter table parse ok')
    return 0


def main(a):
    if len(a) >= 2 and a[0] == 'inspect': return cmd_inspect(a[1])
    if len(a) >= 3 and a[0] == 'unpack': return cmd_unpack(a[1], a[2])
    if a[:1] == ['hooks']: return cmd_hooks()
    if len(a) >= 3 and a[0] == 'build': return cmd_build(a[1], a[2], '--emulator-only' in a)
    if a[:1] == ['selftest']: return cmd_selftest()
    print(__doc__); return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
