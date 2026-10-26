#!/usr/bin/env python3
"""Minimal TT Games .DAT table-of-contents reader (read-only).

Port of the listing logic of the public QuickBMS script ttgames.bms
(0.9.11a, "old format" branch plus the ".CC4" branch) so the archive layout can be
inspected without QuickBMS. Only lists entries and can extract uncompressed or
DFLT (zlib) compressed entries; LZ2K entries are reported but not decoded.

usage: ttdat.py ARCHIVE.DAT [--grep REGEX] [--extract REGEX OUTDIR]
"""
import re, struct, sys, os, zlib

FNV_OFF, FNV_PRIME = 0x811c9dc5, 0x199933


def crc(name):
    h = FNV_OFF
    for c in name.upper().replace('/', '\\').encode('latin-1'):
        h = ((h ^ c) * FNV_PRIME) & 0xffffffff
    return h


def read_toc(path):
    f = open(path, 'rb')
    info_off, info_size = struct.unpack('<II', f.read(8))
    if info_off & 0x80000000:
        info_off = (((info_off ^ 0xffffffff) << 8) + 0x100) & 0xffffffffff
    f.seek(info_off)
    m = f.read(info_size)
    type_boh, files = struct.unpack_from('<iI', m, 0)
    if files in (0x3443432e, 0x2e434334) or type_boh in (0x3443432e, 0x2e434334):
        raise SystemExit('".CC4" new-format archive: not implemented')
    pos = 8
    info = []
    for i in range(files):
        off, zsize, size, packed_off2 = struct.unpack_from('<IIII', m, pos)
        pos += 16
        packed = packed_off2 & 0xffffff
        off2 = packed_off2 >> 24
        if type_boh != -1:
            off = (off << 8) + off2
        else:
            off += off2
        info.append((off, zsize, size, packed))
    names_n, = struct.unpack_from('<I', m, pos); pos += 4
    name_info = pos
    nfs = 12 if type_boh <= -5 else 8
    name_off = name_info + names_n * nfs
    namecrc_rel, = struct.unpack_from('<I', m, name_off)
    name_off += 4
    crc_off = name_off + namecrc_rel
    crcs = list(struct.unpack_from('<%dI' % files, m, crc_off))
    crcidx = {c: i for i, c in enumerate(crcs)}
    # walk name tree (SET_NAME)
    paths = {}
    out = []
    p = name_info
    fullpath = ''
    namez = 0
    for i in range(files):
        while True:
            nxt, prev, off = struct.unpack_from('<hhi', m, p)
            p += nfs
            if off < 0:
                name = ''
            else:
                e = m.index(b'\0', name_off + off)
                name = m[name_off + off:e].decode('latin-1')
            if name and ord(name[0]) >= 0xf0:
                name = ''
            if prev != 0:
                fullpath = paths.get(prev, '')
            paths[namez] = fullpath
            if nxt > 0:
                tmp = paths.get(prev, '')
                if tmp != '':
                    old = '\\' + tmp + '\\'
                    # quickbms ">>=" : remove substring from end
                    if fullpath.endswith(old[1:]):
                        pass
                if name:
                    fullpath += name + '\\'
            namez += 1
            if nxt <= 0:
                break
        full = (fullpath + name).lstrip('\\')
        idx = crcidx.get(crc(full), None)
        out.append((full, idx))
    res = []
    for full, idx in out:
        if idx is None:
            res.append((full, None))
        else:
            res.append((full, info[idx]))
    return f, res


def unpack(f, off, zsize, size):
    f.seek(off)
    sig = f.read(4)
    if sig not in (b'LZ2K', b'DFLT'):
        f.seek(off)
        return f.read(size), 'raw'
    out = bytearray()
    f.seek(off)
    total = 0
    while total < zsize:
        sig = f.read(4)
        if sig == b'DFLT':
            czs, cs = struct.unpack('<II', f.read(8))
            data = f.read(czs)
            out += data if czs == cs else zlib.decompress(data, -15) if data[:1] != b'\x78' else zlib.decompress(data)
        elif sig == b'LZ2K':
            return None, 'lz2k'
        else:
            break
        total += 12 + czs
    return bytes(out), 'dflt'


if __name__ == '__main__':
    f, entries = read_toc(sys.argv[1])
    rx = None
    if '--grep' in sys.argv:
        rx = re.compile(sys.argv[sys.argv.index('--grep') + 1], re.I)
    ex = None
    if '--extract' in sys.argv:
        i = sys.argv.index('--extract')
        ex = re.compile(sys.argv[i + 1], re.I); outdir = sys.argv[i + 2]
    for full, inf in entries:
        if rx and not rx.search(full):
            continue
        if inf is None:
            print(f'?? {full}')
            continue
        off, zs, sz, pk = inf
        f.seek(off); sig = f.read(4)
        tag = sig.decode('latin-1') if sig in (b'LZ2K', b'DFLT') else 'raw '
        print(f'{off:#012x} {zs:>10} {sz:>10} {tag} {full}')
        if ex and ex.search(full):
            data, kind = unpack(f, off, zs, sz)
            if data is not None:
                dst = os.path.join(outdir, full.replace('\\', '/'))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                open(dst, 'wb').write(data)
