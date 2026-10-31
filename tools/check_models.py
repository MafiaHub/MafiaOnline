#!/usr/bin/env python3
"""Check that every stock model and clip the Free Ride source names exists.

A map object, NPC or vehicle with a misspelt model fails only in the game, and
quietly: MapObject.create throws and the piece is left out, an NPC never
spawns. This reads the names the gamemode's TypeScript source spells out
('name.i3d' literals, and the bare names lib/house_rooms.ts passes to put()
and raw()) and looks each one up among the models (models\\*.4ds) and clips
(anims\\*.5ds) of the game's archives and the Freeride Extended package.
Names built at run time are not seen.

Usage: tools/check_models.py [--game <Mafia folder>] [--verbose]
The game folder defaults to MAFIA1ONLINE_GAME_ROOT, then Steam's default
library. Exit status 1 lists what is missing. It only reads game files.
"""

import argparse
import os
import pathlib
import re
import struct
import sys
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import inspect_stock_dta as dta  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "gamemodes" / "freeride" / "src"
MOD = ROOT / "mods" / "freeride-extended.zip"
STEAM_DEFAULT = pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\Mafia\Mafia")
# The archives that hold models and clips.
ARCHIVES = ("a2", "a3", "a4", "ac")

LITERAL = re.compile(r"'([^'\n]+?)\.i3d'", re.IGNORECASE)
BARE = re.compile(r"\b(?:put|raw)\(\s*'([^'\n]+)'")


def game_root(given):
    for candidate in (given, os.environ.get("MAFIA1ONLINE_GAME_ROOT"), STEAM_DEFAULT):
        if candidate and (pathlib.Path(candidate) / "a2.dta").is_file():
            return pathlib.Path(candidate)
    return None


def stock_names(game):
    """Lower-case stems of every model and clip in the game's archives."""
    names = set()
    for archive in ARCHIVES:
        path = game / f"{archive}.dta"
        if not path.is_file():
            continue
        first, second = dta.KEYS[archive]
        key = struct.pack("<II", first ^ 0x39475694, second ^ 0x34985762)
        with path.open("rb") as handle:
            for name, *_ in dta.archive_files(handle, key):
                names.add(stem_of(name))
    return names


def mod_names():
    if not MOD.is_file():
        return set()
    with zipfile.ZipFile(MOD) as package:
        return {stem_of(name) for name in package.namelist()}


def stem_of(member):
    """'models\\Foo Bar.4ds' -> 'foo bar' for models and clips, '' otherwise."""
    parts = re.split(r"[\\/]", member.lower())
    if len(parts) < 2 or parts[-2] not in ("models", "anims"):
        return ""
    return parts[-1].rsplit(".", 1)[0]


def referenced():
    """Every name the source spells out, with the first file and line naming it."""
    found = {}
    for path in sorted(SOURCE.rglob("*.ts")):
        if "data" in path.relative_to(SOURCE).parts:
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if line.lstrip().startswith("//"):
                continue
            for match in (*LITERAL.finditer(line), *BARE.finditer(line)):
                name = match.group(1)
                found.setdefault(name.lower(), (name, f"{path.relative_to(ROOT)}:{number}"))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", help="The Mafia folder holding a2.dta")
    parser.add_argument("--verbose", action="store_true", help="List every name found")
    args = parser.parse_args()

    game = game_root(args.game)
    if game is None:
        parser.error("no Mafia folder with a2.dta: pass --game or set MAFIA1ONLINE_GAME_ROOT")

    known = stock_names(game) | mod_names()
    names = referenced()
    missing = {key: value for key, value in names.items() if key not in known}

    if args.verbose:
        for key, (name, where) in sorted(names.items()):
            print(f"{'MISSING' if key in missing else 'ok':8} {name:32} {where}")

    print(f"{len(names)} names checked against {len(known)} models and clips; {len(missing)} missing.")
    for name, where in sorted(missing.values()):
        print(f"  missing: {name}  ({where})")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
