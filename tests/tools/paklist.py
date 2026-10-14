#!/usr/bin/env python3
"""List the contents of a Quake/Hexen II PACK archive: name and size per file.

Usage: python -I tests/tools/paklist.py <pak> [substring-filter]
"""
import struct
import sys

ENTRY = struct.Struct("<56sii")  # name, offset, size


def read_pak(path):
    with open(path, "rb") as f:
        header = f.read(12)
        if len(header) != 12:
            raise ValueError("file too short for a PACK header")
        magic, diroff, dirlen = struct.unpack("<4sii", header)
        if magic != b"PACK":
            raise ValueError("not a PACK archive (magic %r)" % magic)
        if dirlen % ENTRY.size:
            raise ValueError("bad directory length %d" % dirlen)
        f.seek(diroff)
        data = f.read(dirlen)
    entries = []
    for i in range(dirlen // ENTRY.size):
        raw, off, size = ENTRY.unpack_from(data, i * ENTRY.size)
        name = raw.split(b"\0", 1)[0].decode("latin-1")
        entries.append((name, size))
    return entries


def main(argv):
    if len(argv) < 2 or len(argv) > 3:
        sys.stderr.write(__doc__)
        return 2
    flt = argv[2].lower() if len(argv) == 3 else None
    try:
        entries = read_pak(argv[1])
    except (OSError, ValueError) as e:
        sys.stderr.write("paklist: %s\n" % e)
        return 1
    for name, size in entries:
        if flt is None or flt in name.lower():
            print("%s\t%d" % (name, size))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
