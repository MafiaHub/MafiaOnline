#!/usr/bin/env python3
"""Read stock Mafia DTA metadata or search one decoded asset in memory.

This is a research aid for native mission anchors. It never writes game assets.
The format and keys come from reM/projects/rw_data and Game/WinMain.cpp.
"""

import argparse
import pathlib
import re
import struct


KEYS = {
    "a8": (0x6A63FA71, 0xEC45D8CE),  # patch and menus
    "a6": (0x728E2DB9, 0x5055DA68),  # textures
    "a1": (0xE7375F59, 0x0900210E),  # mission scenes
    "a2": (0x1417D340, 0xB6399E19),  # models
    "a3": (0xA94B8D3C, 0x771F3888),  # animations
    "ac": (0xA94B8D3C, 0x771F3888),  # animations
    "a4": (0x4F4BB0C6, 0xEA340420),  # animations
    "aa": (0xD4AD90C6, 0x67DA216E),  # tables and sounds
    "a5": (0xF4F03A72, 0xE266FE62),
    "a7": (0x959D1117, 0x5B763446),
    "a9": (0x7F3D9B74, 0xEC48FE17),
}


def decrypt(data, key):
    return bytes(byte ^ key[index % 8] for index, byte in enumerate(data))


def decompress(block):
    if block[0] == 0:
        return block[1:]
    if block[0] != 1:
        return block[1:]
    flags = block[1] << 8 | block[2]
    bits = 16
    source = 3
    output = bytearray()
    while source < len(block):
        if bits == 0:
            flags = block[source] << 8 | block[source + 1]
            source += 2
            bits = 16
        if flags & 0x8000:
            length = block[source + 1]
            distance = block[source] << 4 | length >> 4
            if distance:
                for _ in range((length & 15) + 3):
                    output.append(output[-distance])
                source += 2
            else:
                output.extend(bytes([block[source + 3]]) * ((length << 8 | block[source + 2]) + 16))
                source += 4
        else:
            output.append(block[source])
            source += 1
        flags = flags << 1 & 0xFFFF
        bits -= 1
    return output


def archive_files(handle, key):
    handle.seek(4)
    _, table_offset, table_size, _ = struct.unpack("<IIII", decrypt(handle.read(16), key))
    handle.seek(table_offset)
    table = decrypt(handle.read(table_size), key)
    for offset in range(0, len(table), 28):
        _, header_offset, data_offset, _ = struct.unpack_from("<III16s", table, offset)
        handle.seek(header_offset)
        _, _, _, _, size, blocks, properties = struct.unpack("<IIIIIIQ", decrypt(handle.read(32), key))
        handle.seek(header_offset + 32)
        name = decrypt(handle.read(properties & 0x7FFF), key).decode("latin1")
        yield name, size, blocks, properties, data_offset


def asset_bytes(handle, key, entry):
    _, size, blocks, properties, data_offset = entry
    handle.seek(data_offset)
    result = bytearray()
    for _ in range(blocks):
        packed_size = struct.unpack("<I", handle.read(4))[0] & 0xFFFF
        block = handle.read(packed_size)
        if properties & 0x8000:
            block = decrypt(block, key)
        result.extend(decompress(block))
    if len(result) != size:
        raise ValueError(f"decoded size {len(result)} differs from archive size {size}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=pathlib.Path, help="Path to a supported stock .dta archive")
    parser.add_argument("--name", help="Exact archive member to decode; omit to list members")
    parser.add_argument("--find", help="Case insensitive byte pattern to find in decoded member")
    args = parser.parse_args()
    archive = args.archive.stem.lower()
    if archive not in KEYS:
        parser.error(f"unsupported stock archive: {archive}")
    first, second = KEYS[archive]
    key = struct.pack("<II", first ^ 0x39475694, second ^ 0x34985762)
    with args.archive.open("rb") as handle:
        entries = list(archive_files(handle, key))
        if not args.name:
            for name, size, *_ in entries:
                print(f"{size:>9}  {name}")
            return
        match = next((entry for entry in entries if entry[0].lower() == args.name.lower()), None)
        if match is None:
            parser.error(f"archive member not found: {args.name}")
        data = asset_bytes(handle, key, match)
    if args.find:
        for hit in re.finditer(re.escape(args.find.encode()), data, re.IGNORECASE):
            context = data[max(0, hit.start() - 32):hit.end() + 48]
            print(f"{hit.start():>9}: {context!r}")
    else:
        print(f"{match[0]}: {len(data)} decoded bytes, first 64: {data[:64]!r}")


if __name__ == "__main__":
    main()
