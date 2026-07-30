#!/usr/bin/env python3
"""Pull Haxial %FNT faces out of a Haxial executable (KDX, AppearanceEdit, ...).

Haxial imports one GDI entry point and rasterises text itself, so its faces ship
as %FNT blobs inside the binary. Extract them next to a KDX install and point the
editor at one:  SagradoKitEditor.exe --font Myoklonika.fnt

    python3 research/extract_fnt.py "KDXClient.exe" out_dir
"""
import re
import struct
import sys


def faces(data):
    for m in re.finditer(b"%FNT", data):
        o = m.start()
        if o + 0x140 > len(data):
            continue
        ver, length = struct.unpack_from(">II", data, o + 4)
        if ver != 0x00020001 or not 0x140 < length < len(data) - o:
            continue
        n = data[o + 0x18]
        name = data[o + 0x19 : o + 0x19 + n].decode("latin-1", "replace")
        if not name:
            continue
        yield name, data[o : o + length]


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    data = open(sys.argv[1], "rb").read()
    for name, blob in faces(data):
        path = f"{sys.argv[2]}/{name}.fnt"
        open(path, "wb").write(blob)
        print(f"{path}  {len(blob)} bytes  line height {blob[0x10c]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
