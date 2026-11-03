#!/usr/bin/env python3
"""Package the 32-bit Windows Release client build for distribution.

Build it first with `builds\\build.bat Mafia1OnlineClient 32 Release` and
`builds\\build.bat Mafia1OnlineLauncher 32 Release`. --bin-dir packages another
Release or RelWithDebInfo tree; a Debug tree is refused. --channel stamps the
MafiaHub build channel the launcher patches from (.mafiahub/channel), and
--stage also writes the files to a folder, which tools/release.py publishes.
The MSVC runtime DLLs
ship app-local from the Visual Studio redist folder matching the tree's
compiler, so players need no separate VC++ redistributable: the x86 runtime
next to the client, the x64 one in cef64/ next to the 64-bit CEF host the
client starts for its web UI.
"""

import argparse
import re
import shutil
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


PROJECT_ROOT = Path(__file__).resolve().parents[1]
FRAMEWORK_ROOT = PROJECT_ROOT.parents[2]
DEFAULT_BIN_DIR = PROJECT_ROOT / "builds" / "build-prod-32" / "bin"
CRT_FILES = (
    "concrt140.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll",
    "vccorlib140.dll",
    "vcruntime140.dll",
    "vcruntime140_threads.dll",
)
# x64 only: the 64-bit CEF host needs it besides the files above.
CRT_FILES_X64 = ("vcruntime140_1.dll",)
# The 64-bit CEF host and its runtime (Framework cmake/FrameworkCefHost.cmake). Build artifacts that
# are not runtime files stay out.
HOST_DIR = "cef64"
HOST_EXCLUDED_SUFFIXES = (".pdb", ".ilk", ".lib", ".exp")
VERSION = (PROJECT_ROOT / "VERSION").read_text(encoding="utf-8").strip()
OUTPUT = PROJECT_ROOT / "builds" / "releases" / f"Mafia1OnlineClient-{VERSION}-windows-x86.zip"

RUNTIME_FILES = (
    "Mafia1OnlineLauncher.exe",
    # Replaces the files the launcher holds open when it patches (MafiaHub Services).
    "MafiaHubUpdater.exe",
    "Mafia1OnlineClient.dll",
    "FrameworkLoaderData.dll",
    "cef_subprocess.exe",
    "chrome_100_percent.pak",
    "chrome_200_percent.pak",
    "chrome_elf.dll",
    "crashpad_handler.exe",
    "d3dcompiler_47.dll",
    "discord_game_sdk.dll",
    "fw_steam_api.dll",
    "icudtl.dat",
    "libEGL.dll",
    "libGLESv2.dll",
    "libcef.dll",
    "resources.pak",
    "v8_context_snapshot.bin",
    "vk_swiftshader.dll",
    "vk_swiftshader_icd.json",
    "vulkan-1.dll",
)

README = f"""Mafia1Online client {VERSION} (Windows x86 development preview)

Extract this ZIP to any writable folder. Run Mafia1OnlineLauncher.exe from
that folder. The launcher looks for Mafia in your Steam library (app 40990)
and can also ask you to select Game.exe manually. Flat and nested game
folders work. Keep the files and folders in this ZIP together. Do not copy
them into the game folder.

You need your own copy of the original Mafia. Steam is not needed: when
Steam is closed or not installed, or Mafia is not in its library, the
launcher asks you to select Game.exe once and remembers it in
Mafia1Online_launcher.json next to the launcher. Delete that file to pick
another copy. The client supports only the 32-bit Game.exe with SHA-256:
303eb95ee2de3433511ce0cb518921dcb96b62b0f64ebfcc436723ff5083f298
This is the executable of the Steam release and of the matching non-Steam
release; any copy of it works without Steam. The launcher checks this hash
before loading the game and refuses a Game.exe with a different one. Create
a game profile with the original game first if you do not already have one.
To start a specific copy, set the MAFIA1ONLINE_GAME_ROOT environment variable
to the folder containing Game.exe or the folder containing a nested
Mafia\\Game.exe before starting the launcher.

The launcher keeps itself up to date: when a newer release is out it
downloads only the changed files and restarts. Without an internet
connection it starts the version you have.

Enter your server address in the game's online menu. For automatic joining,
copy config\\client.example.json to config\\client.json and edit the host,
port, nickname and profileIndex, then set quickJoin.enabled to true. The
127.0.0.1 example address works only when the server is on this same PC.
The multiplayer server is distributed separately.

If the game crashes, look for a .dmp file in this folder's logs directory.
The dump and the Mafia1Online-*.log file of the same run (each game
process writes its own) help identify the failing
module and stack; the matching developer PDBs are held with the build.
The web UI runs in a separate 64-bit process (cef64\\cef_host.exe) that the
game starts and stops itself, so 64-bit Windows, or a 64-bit Wine prefix, is
needed for it. Where it cannot start, the game runs the web UI inside its own
process instead; set FW_CEF_MODE=inprocess to choose that from the start.
Under Wine, the web UI uses CPU rendering and keeps Chromium's remaining GPU
service in the browser process. Set MAFIA1ONLINE_NATIVE_UI=1 to use the
native fallback when CEF is unavailable.

This package has no machine-specific game or mod path. It contains no game
files, local cache, logs, saved profiles or personal client configuration.
It is a development preview; some gameplay features are still in progress.
"""


def read_cmake_cache(path: Path) -> dict:
    entries = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"^([A-Za-z_][A-Za-z0-9_]*):[A-Z]+=(.*)$", line)
        if match:
            entries[match.group(1)] = match.group(2)
    return entries


def find_crt_dir(compiler: Path, arch: str = "x86") -> Path:
    # .../VC/Tools/MSVC/<version>/bin/Host<host>/x86/cl.exe -> .../VC/Redist/MSVC/<version>/<arch>
    parts = compiler.parts
    tools = [i for i, part in enumerate(parts) if part.lower() == "tools"]
    if not tools or parts[-2].lower() != "x86":
        raise SystemExit(f"{compiler} is not an x86 MSVC compiler")
    vc_root, version = Path(*parts[: tools[-1]]), parts[tools[-1] + 2]
    redist = vc_root / "Redist" / "MSVC"
    # The redist folder normally carries the compiler's version; fall back to the newest.
    candidates = sorted(redist.glob(f"{version}/{arch}/Microsoft.VC*.CRT")) or sorted(redist.glob(f"*/{arch}/Microsoft.VC*.CRT"))
    if not candidates:
        raise SystemExit(f"No {arch} MSVC runtime under {redist}")
    return candidates[-1]


def main() -> None:
    parser = argparse.ArgumentParser(description="Package the 32-bit Windows client build.")
    parser.add_argument("--bin-dir", type=Path, default=DEFAULT_BIN_DIR, help="build output to package")
    parser.add_argument("--channel", help="MafiaHub build channel to stamp into .mafiahub/channel")
    parser.add_argument("--stage", type=Path, help="also write the packaged files to this (new) folder")
    arguments = parser.parse_args()
    bin_dir = arguments.bin_dir.resolve()
    if arguments.channel is not None and not re.fullmatch(r"[a-z0-9._-]{1,32}", arguments.channel):
        raise SystemExit(f"Invalid channel {arguments.channel!r}")

    cache = read_cmake_cache(bin_dir.parent / "CMakeCache.txt")
    if cache.get("CMAKE_BUILD_TYPE") not in ("Release", "RelWithDebInfo"):
        raise SystemExit(f"{bin_dir} is a {cache.get('CMAKE_BUILD_TYPE', 'unknown')} build; package a Release build")
    crt_dir = find_crt_dir(Path(cache["CMAKE_CXX_COMPILER"]))
    crt_dir_x64 = find_crt_dir(Path(cache["CMAKE_CXX_COMPILER"]), "x64")

    files = [(bin_dir / name, name) for name in RUNTIME_FILES]
    files.extend((crt_dir / name, name) for name in CRT_FILES)

    host_dir = bin_dir / HOST_DIR
    if not (host_dir / "cef_host.exe").is_file():
        raise FileNotFoundError(host_dir / "cef_host.exe")
    files.extend(
        (path, path.relative_to(bin_dir).as_posix())
        for path in sorted(host_dir.rglob("*"))
        if path.is_file() and path.suffix.lower() not in HOST_EXCLUDED_SUFFIXES
    )
    files.extend((crt_dir_x64 / name, f"{HOST_DIR}/{name}") for name in CRT_FILES + CRT_FILES_X64)
    for folder in ("locales", "ui"):
        source_dir = bin_dir / folder
        if not source_dir.is_dir():
            raise FileNotFoundError(source_dir)
        files.extend((path, path.relative_to(bin_dir).as_posix()) for path in sorted(source_dir.rglob("*")) if path.is_file())

    files.extend(
        (
            (PROJECT_ROOT / "config" / "client.example.json", "config/client.example.json"),
            (FRAMEWORK_ROOT / "LICENSE.txt", "LICENSE.txt"),
            (FRAMEWORK_ROOT / "NOTICE.txt", "NOTICE.txt"),
        )
    )
    cef_license = next((FRAMEWORK_ROOT / "vendors" / "cef").glob("*_windows32_minimal/LICENSE.txt"))
    files.append((cef_license, "CEF_LICENSE.txt"))
    files.append((cef_license.with_name("CREDITS.html"), "CEF_CREDITS.html"))

    for source, _ in files:
        if not source.is_file() or source.is_symlink():
            raise FileNotFoundError(source)

    texts = {"README-CLIENT.txt": README}
    if arguments.channel is not None:
        texts[".mafiahub/channel"] = arguments.channel

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    temporary = OUTPUT.with_suffix(".zip.tmp")
    try:
        with ZipFile(temporary, "w", compression=ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
            for name, text in texts.items():
                archive.writestr(name, text)
            for source, name in files:
                archive.write(source, name)
        temporary.replace(OUTPUT)
    finally:
        temporary.unlink(missing_ok=True)

    if arguments.stage is not None:
        stage = arguments.stage.resolve()
        if stage.exists():
            raise SystemExit(f"{stage} already exists")
        for name, text in texts.items():
            (stage / name).parent.mkdir(parents=True, exist_ok=True)
            (stage / name).write_text(text, encoding="utf-8")
        for source, name in files:
            (stage / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, stage / name)

    print(OUTPUT)


if __name__ == "__main__":
    main()
