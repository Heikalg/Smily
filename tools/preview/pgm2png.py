#!/usr/bin/env python3
"""Convert the harness's P5 sprite sheets to PNG (stdlib only)."""
import struct
import sys
import zlib


def read_pgm(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if not data.startswith(b"P5"):
        raise ValueError(f"{path}: not a P5 PGM")
    pos, fields = 2, []
    while len(fields) < 3:
        while pos < len(data) and data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            while data[pos : pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos : pos + 1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1  # single whitespace separator before the raster
    width, height, _maxval = fields
    return width, height, data[pos : pos + width * height]


def write_png(path, width, height, gray):
    raw = b"".join(
        b"\x00" + gray[y * width : (y + 1) * width] for y in range(height)
    )

    def chunk(tag, payload):
        body = tag + payload
        return (
            struct.pack(">I", len(payload))
            + body
            + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        )

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )
    with open(path, "wb") as fh:
        fh.write(png)


def main(paths):
    for src in paths:
        width, height, gray = read_pgm(src)
        dst = src[:-4] + ".png" if src.endswith(".pgm") else src + ".png"
        write_png(dst, width, height, gray)
        print(f"  {dst}  {width}x{height}")


if __name__ == "__main__":
    main(sys.argv[1:])
