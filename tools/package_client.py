#!/usr/bin/env python3
"""Package the canonical 32-bit Windows client build for distribution."""

from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


PROJECT_ROOT = Path(__file__).resolve().parents[1]
FRAMEWORK_ROOT = PROJECT_ROOT.parents[2]
BIN_DIR = FRAMEWORK_ROOT / "builds" / "build-32" / "bin"
VERSION = (PROJECT_ROOT / "VERSION").read_text(encoding="utf-8").strip()
OUTPUT = FRAMEWORK_ROOT / "builds" / "releases" / f"Mafia1OnlineClient-{VERSION}-windows-x86.zip"

RUNTIME_FILES = (
    "Mafia1OnlineLauncher.exe",
    "Mafia1OnlineClient.dll",
    "FrameworkLoaderData.dll",
    "cef_subprocess.exe",
    "chrome_100_percent.pak",
    "chrome_200_percent.pak",
    "chrome_elf.dll",
    "concrt140.dll",
    "crashpad_handler.exe",
    "d3dcompiler_47.dll",
    "discord_game_sdk.dll",
    "fw_steam_api.dll",
    "icudtl.dat",
    "libEGL.dll",
    "libGLESv2.dll",
    "libcef.dll",
    "msvcp140.dll",
    "msvcp140_1.dll",
    "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll",
    "msvcp140_codecvt_ids.dll",
    "resources.pak",
    "v8_context_snapshot.bin",
    "vccorlib140.dll",
    "vcruntime140.dll",
    "vcruntime140_threads.dll",
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

You need your own installed copy of the original Mafia. Steam is not needed
for a manually selected copy, but the client supports only the 32-bit
Game.exe with SHA-256:
303eb95ee2de3433511ce0cb518921dcb96b62b0f64ebfcc436723ff5083f298
The launcher checks this hash before loading the game. Create a game profile
with the original game first if you do not already have one.
If Steam is installed but you want to use another copy, set the
MAFIA1ONLINE_GAME_ROOT environment variable to the folder containing Game.exe
or the folder containing a nested Mafia\\Game.exe before starting the launcher.

Enter your server address in the game's online menu. For automatic joining,
copy config\\client.example.json to config\\client.json and edit the host,
port, nickname and profileIndex, then set quickJoin.enabled to true. The
127.0.0.1 example address works only when the server is on this same PC.
The multiplayer server is distributed separately.

If the game crashes, look for a .dmp file in this folder's logs directory.
The dump and Mafia1Online.log from the same run help identify the failing
module and stack; the matching developer PDBs are held with the build.
Under Wine, the web UI uses CPU rendering and keeps Chromium's remaining GPU
service in the browser process. Set MAFIA1ONLINE_NATIVE_UI=1 to use the
native fallback when CEF is unavailable.

This package has no machine-specific game or mod path. It contains no game
files, local cache, logs, saved profiles or personal client configuration.
It is a development preview; some gameplay features are still in progress.
"""


def main() -> None:
    files = [(BIN_DIR / name, name) for name in RUNTIME_FILES]
    for folder in ("locales", "ui"):
        source_dir = BIN_DIR / folder
        if not source_dir.is_dir():
            raise FileNotFoundError(source_dir)
        files.extend((path, path.relative_to(BIN_DIR).as_posix()) for path in sorted(source_dir.rglob("*")) if path.is_file())

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

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    temporary = OUTPUT.with_suffix(".zip.tmp")
    try:
        with ZipFile(temporary, "w", compression=ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
            archive.writestr("README-CLIENT.txt", README)
            for source, name in files:
                archive.write(source, name)
        temporary.replace(OUTPUT)
    finally:
        temporary.unlink(missing_ok=True)

    print(OUTPUT)


if __name__ == "__main__":
    main()
