#!/usr/bin/env python3
"""Pack an extracted Mafia mod's data folders into a deterministic server ZIP."""

import argparse
import hashlib
from pathlib import Path
import zipfile

ROOTS = {"missions", "models", "maps", "sounds", "tables", "anims", "fonts"}
EXECUTABLES = {".exe", ".dll", ".asi", ".com", ".bat", ".cmd", ".ps1", ".vbs", ".js", ".lnk"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Directory containing models/, missions/, etc.")
    parser.add_argument("output", type=Path)
    parser.add_argument("--mission", help="Rename a mission directory, e.g. freeride:freeride_extended")
    args = parser.parse_args()
    rename = None
    if args.mission:
        rename = args.mission.lower().split(":")
        if len(rename) != 2 or any(not name or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789_- " for c in name) for name in rename):
            parser.error("--mission expects old_name:new_name")
    files = {}
    excluded = []
    for source in sorted(args.source.rglob("*")):
        if source.is_symlink():
            raise ValueError(f"Symlinks are not supported: {source}")
        if not source.is_file():
            continue
        relative = source.relative_to(args.source)
        parts = [part.lower() for part in relative.parts]
        if parts[0] not in ROOTS or source.suffix.lower() in EXECUTABLES:
            excluded.append(str(relative))
            continue
        if rename and len(parts) > 2 and parts[:2] == ["missions", rename[0]]:
            parts[1] = rename[1]
        name = "/".join(parts)
        if name in files:
            raise ValueError(f"Duplicate case-insensitive asset: {name}")
        files[name] = source
    if not files:
        parser.error("No game assets found; select the directory directly containing the game data folders")
    if rename and not any(name.startswith(f"missions/{rename[1]}/") for name in files):
        parser.error("The source mission was not found")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, source in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, source.read_bytes())
    with args.output.open("rb") as output:
        digest = hashlib.file_digest(output, "sha256").hexdigest()
    print(f"Packed {len(files)} assets: {args.output} ({args.output.stat().st_size:,} bytes)")
    print(f"SHA-256: {digest}")
    for name in excluded:
        print(f"Excluded: {name}")
    missions = sorted(name[9:-10] for name in files if name.startswith("missions/") and name.endswith("/scene.4ds") and name[:-10] + "/tree.klz" in files)
    print("Detected missions: " + ", ".join(missions))


if __name__ == "__main__":
    main()
