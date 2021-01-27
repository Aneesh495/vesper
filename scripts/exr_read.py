"""Bounded independent reader for uncompressed Vesper OpenEXR scanlines."""

import math
import struct
from pathlib import Path


def read(path, wanted):
    file = Path(path)
    if file.stat().st_size > 256 * 1024 * 1024:
        raise ValueError("EXR exceeds verifier size ceiling")
    data = file.read_bytes()
    if data[:8] != struct.pack("<II", 20000630, 2):
        raise ValueError("unsupported EXR layout")
    offset = 8

    def string():
        nonlocal offset
        end = data.index(0, offset, min(offset + 128, len(data)))
        value = data[offset:end].decode("ascii")
        offset = end + 1
        return value

    attrs = {}
    while (name := string()):
        kind = string()
        size, = struct.unpack_from("<I", data, offset)
        offset += 4
        if name in attrs or size > 4096 or offset + size > len(data):
            raise ValueError("invalid EXR attribute")
        attrs[name] = (kind, data[offset:offset + size])
        offset += size
    if attrs["compression"][1] != b"\0":
        raise ValueError("unexpected EXR compression")
    x0, y0, x1, y1 = struct.unpack("<iiii", attrs["dataWindow"][1])
    width, height = x1 + 1, y1 + 1
    if x0 or y0 or width < 1 or height < 1 or width * height > 4_194_304:
        raise ValueError("invalid EXR image size")
    channels, types, cursor = [], {}, 0
    items = attrs["channels"][1]
    while items[cursor]:
        end = items.index(0, cursor)
        channel = items[cursor:end].decode("ascii")
        pixel_type, _, x_step, y_step = struct.unpack_from("<4I", items, end + 1)
        if pixel_type not in (0, 2) or x_step != 1 or y_step != 1 or channel in channels:
            raise ValueError("unsupported EXR channel")
        channels.append(channel)
        types[channel] = "I" if pixel_type == 0 else "f"
        cursor = end + 17
    if not set(wanted).issubset(channels):
        raise ValueError("missing EXR channels")
    values = {c: [] for c in wanted}
    for y in range(height):
        location, = struct.unpack_from("<Q", data, offset + 8 * y)
        line, size = struct.unpack_from("<II", data, location)
        if line != y or size != len(channels) * 4 * width or location + 8 + size > len(data):
            raise ValueError("invalid EXR scanline")
        for channel in wanted:
            start = location + 8 + 4 * width * channels.index(channel)
            row = struct.unpack_from("<" + types[channel] * width, data, start)
            if any(not math.isfinite(v) or v < -1e-9 for v in row):
                raise ValueError("invalid raw EXR sample")
            values[channel].extend(row)
    return (width, height), values, attrs["vesper"][1].decode()
