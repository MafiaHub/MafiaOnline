#!/usr/bin/env python3
"""Build, publish and tag a Mafia1Online release, as KCDC's release workflow does.

    python tools/release.py --dry-run        build and package only
    python tools/release.py --dry-run --skip-build   package the existing trees
    python tools/release.py --rc 1           v<VERSION>-rc.1: testers on `testing`
    python tools/release.py                  v<VERSION>: the release itself

The version is the VERSION file. Every release uploads the client to the
MafiaHub Services `testing` channel; the package is stamped `testing` for a
release candidate and `stable` for a release, which is the channel an
installation patches from. Stable itself is advanced by promoting the build in
MafiaHub Services, never from here, so players get a release once it is
promoted. The git tag and the GitHub release (client and server ZIPs) follow
the upload.

The release builds the canonical Release trees (builds\\build.bat ... Release)
with the launcher's patcher compiled in, then builds the launcher again
without it, so a local Release build never patches itself from Services.

Environment (not needed for --dry-run, which then builds without the patcher):
    M1O_SERVICES_API_URL         MafiaHub API base URL
    M1O_SERVICES_MOD_ID          Mafia1Online's mod UUID in MafiaHub Services
    M1O_BUILD_DOWNLOAD_TOKEN     download token compiled into the launcher
    MAFIAHUB_BUILD_UPLOAD_TOKEN  upload token for publishing the client build
    M1O_SERVICES_CLI_VERSION     optional; @mafiahub/services-cli version for
                                 `builds publish` (default: the one KCDC
                                 publishes with)
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

PROJECT_ROOT = Path(__file__).resolve().parents[1]
FRAMEWORK_ROOT = PROJECT_ROOT.parents[2]
VERSION = (PROJECT_ROOT / "VERSION").read_text(encoding="utf-8").strip()
RELEASES = PROJECT_ROOT / "builds" / "releases"
CLIENT_ZIP = RELEASES / f"Mafia1OnlineClient-{VERSION}-windows-x86.zip"
SERVER_ZIP = RELEASES / f"Mafia1OnlineServer-{VERSION}-windows-x64.zip"
SERVER_BIN = PROJECT_ROOT / "builds" / "build-prod-64" / "bin"
SERVER_RUNTIME = ("Mafia1OnlineServer.exe", "libnode.dll", "crashpad_handler.exe")
UPLOAD_CHANNEL = "testing"
GITHUB_REPOSITORY = "MafiaHub/MafiaOnline"
# KCDC's release workflow publishes builds with this CLI; the scripting docs
# keep their own pin in scripting-api/services-cli.version.
SERVICES_CLI_VERSION = "0.10.0"

sys.path.insert(0, str(PROJECT_ROOT / "tools"))
import package_client  # noqa: E402  (CRT lookup shared with the client package)

SERVER_README = f"""Mafia1Online server {VERSION} (Windows x64)

Copy config\\server.example.json to server.json in this folder and set
mod.mission (and optionally a password), then run Mafia1OnlineServer.exe from
this folder. resources\\ holds the gamemodes (Free Ride by default) and mods\\
the asset ZIPs clients download. Clients and server must run the same
version: {VERSION}.
"""


def run(command, **kwargs):
    print("+", " ".join(str(part) for part in command), flush=True)
    subprocess.run(command, check=True, cwd=PROJECT_ROOT, **kwargs)


def output(command):
    return subprocess.run(command, check=True, cwd=PROJECT_ROOT, capture_output=True, text=True).stdout.strip()


def build(target, arch, cmake_args):
    environment = dict(os.environ, M1O_CMAKE_ARGS=" ".join(cmake_args))
    run(["cmd", "/c", f"builds\\build.bat {target} {arch} Release"], env=environment)


def patcher_args(enabled, stamp=None):
    if not enabled:
        return ["-DM1O_PATCHER_ENABLED=OFF", "-DM1O_PATCHER_API_URL=", "-DM1O_PATCHER_MOD_ID=", "-DM1O_PATCHER_DOWNLOAD_TOKEN="]
    return [
        "-DM1O_PATCHER_ENABLED=ON",
        f"-DM1O_PATCHER_API_URL={os.environ['M1O_SERVICES_API_URL']}",
        f"-DM1O_PATCHER_MOD_ID={os.environ['M1O_SERVICES_MOD_ID']}",
        f"-DM1O_PATCHER_DOWNLOAD_TOKEN={os.environ['M1O_BUILD_DOWNLOAD_TOKEN']}",
        f"-DM1O_PATCHER_CHANNEL={stamp}",
    ]


def check_repository(tag):
    if output(["git", "status", "--porcelain", "--untracked-files=no"]):
        raise SystemExit("Commit or stash your changes first: a release is built from a clean tree.")
    if output(["git", "rev-parse", "--abbrev-ref", "HEAD"]) != "main":
        raise SystemExit("Releases are cut from main.")
    run(["git", "fetch", "--quiet", "origin", "main", "--tags"])
    if output(["git", "rev-parse", "HEAD"]) != output(["git", "rev-parse", "origin/main"]):
        raise SystemExit("main differs from origin/main: pull or push first.")
    if output(["git", "tag", "--list", tag]):
        raise SystemExit(f"Tag {tag} already exists.")


def check_freeride():
    """resources/freeride is built from gamemodes/freeride and committed; a stale build must not ship."""
    source = PROJECT_ROOT / "gamemodes" / "freeride"
    if not (source / "node_modules").is_dir():
        raise SystemExit("Run npm install in gamemodes/freeride first: the release checks resources/freeride is up to date.")
    subprocess.run(["node", "scripts/build.mjs", "--check"], check=True, cwd=source)


def package_server():
    check_freeride()
    cache = package_client.read_cmake_cache(SERVER_BIN.parent / "CMakeCache.txt")
    if cache.get("CMAKE_BUILD_TYPE") != "Release":
        raise SystemExit(f"{SERVER_BIN} is not a Release build")
    # find_crt_dir reads the toolset from an x86 compiler path; this tree's is
    # .../Hostx64/x64/cl.exe, and the redist folder is the same.
    crt_dir = package_client.find_crt_dir(Path(cache["CMAKE_CXX_COMPILER"]).parent.parent / "x86" / "cl.exe", "x64")
    files = [(SERVER_BIN / name, name) for name in SERVER_RUNTIME]
    files.extend((crt_dir / name, name) for name in package_client.CRT_FILES + package_client.CRT_FILES_X64)
    # Tracked files only: a local node_modules or saved data stays out.
    for name in output(["git", "ls-files", "resources", "mods/README.md", "config/server.example.json"]).splitlines():
        files.append((PROJECT_ROOT / name, name))
    files.extend(((FRAMEWORK_ROOT / "LICENSE.txt", "LICENSE.txt"), (FRAMEWORK_ROOT / "NOTICE.txt", "NOTICE.txt")))
    for source, _ in files:
        if not source.is_file():
            raise FileNotFoundError(source)
    SERVER_ZIP.parent.mkdir(parents=True, exist_ok=True)
    temporary = SERVER_ZIP.with_suffix(".zip.tmp")
    try:
        with ZipFile(temporary, "w", compression=ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
            archive.writestr("README-SERVER.txt", SERVER_README)
            for source, name in files:
                archive.write(source, name)
        temporary.replace(SERVER_ZIP)
    finally:
        temporary.unlink(missing_ok=True)


def release_notes(tag):
    previous = subprocess.run(["git", "describe", "--tags", "--abbrev=0", "HEAD^"], cwd=PROJECT_ROOT, capture_output=True, text=True).stdout.strip()
    changes = output(["git", "log", "--no-merges", "--pretty=- %s", f"{previous}..HEAD"] if previous else ["git", "log", "--no-merges", "--pretty=- %s", "-50"])
    return (f"Mafia1Online {tag}.\n\nThe launcher updates itself; start Mafia1OnlineLauncher.exe. "
            f"Servers need the server ZIP of the same version.\n\n## Changes\n\n{changes or '- First tagged release.'}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rc", type=int, help="release candidate number: v<VERSION>-rc.N, a GitHub prerelease stamped `testing`")
    parser.add_argument("--dry-run", action="store_true", help="build and package without publishing, tagging or a GitHub release")
    parser.add_argument("--skip-build", action="store_true", help="with --dry-run: package the Release trees as they are")
    arguments = parser.parse_args()
    if arguments.skip_build and not arguments.dry_run:
        raise SystemExit("--skip-build is for --dry-run only: a release is built from its tag's sources")

    if not re.fullmatch(r"\d+\.\d+\.\d+", VERSION):
        raise SystemExit(f"VERSION {VERSION!r} is not MAJOR.MINOR.PATCH")
    tag = f"v{VERSION}-rc.{arguments.rc}" if arguments.rc else f"v{VERSION}"
    stamp = "testing" if arguments.rc else "stable"
    required = ("M1O_SERVICES_API_URL", "M1O_SERVICES_MOD_ID", "M1O_BUILD_DOWNLOAD_TOKEN", "MAFIAHUB_BUILD_UPLOAD_TOKEN")
    missing = [name for name in required if not os.environ.get(name)]
    if missing and not arguments.dry_run:
        raise SystemExit("Missing environment: " + ", ".join(missing))
    patched = not missing and not arguments.skip_build
    if not arguments.dry_run:
        check_repository(tag)
    print(f"Release {tag}: client stamped `{stamp}`, uploaded to `{UPLOAD_CHANNEL}`" + ("" if patched else " (dry run without the patcher)"))

    stage = RELEASES / tag / "client"
    if stage.parent.exists():
        shutil.rmtree(stage.parent)
    try:
        if not arguments.skip_build:
            build("Mafia1OnlineClient", 32, patcher_args(patched, stamp))
            build("Mafia1OnlineLauncher", 32, patcher_args(patched, stamp))
            build("Mafia1OnlineServer", 64, [])
        run([sys.executable, "tools/package_client.py", "--channel", stamp, "--stage", str(stage)])
        package_server()
    finally:
        if patched:
            # Back to a launcher that never patches a local Release build.
            build("Mafia1OnlineLauncher", 32, patcher_args(False))
            shutil.rmtree(PROJECT_ROOT / "builds" / "build-prod-32" / "bin" / ".mafiahub", ignore_errors=True)
    print(CLIENT_ZIP)
    print(SERVER_ZIP)
    if arguments.dry_run:
        return

    cli_version = os.environ.get("M1O_SERVICES_CLI_VERSION") or SERVICES_CLI_VERSION
    commit = output(["git", "rev-parse", "HEAD"])
    run(["cmd", "/c", "pnpm", "dlx", f"@mafiahub/services-cli@{cli_version}", "builds", "publish",
         "--api-url", os.environ["M1O_SERVICES_API_URL"], "--mod-id", os.environ["M1O_SERVICES_MOD_ID"],
         "--channel", UPLOAD_CHANNEL, "--version", VERSION, "--root", str(stage),
         "--metadata", f"tag={tag}", "--metadata", f"commit={commit}", "--metadata", "channel_portable=true", "--json"])
    run(["git", "tag", "-a", tag, "-m", f"Mafia1Online {tag}"])
    run(["git", "push", "origin", tag])
    notes = RELEASES / tag / "notes.md"
    notes.write_text(release_notes(tag), encoding="utf-8")
    command = ["gh", "release", "create", tag, str(CLIENT_ZIP), str(SERVER_ZIP), "--repo", GITHUB_REPOSITORY,
               "--title", f"Mafia1Online {tag[1:]}", "--notes-file", str(notes), "--verify-tag"]
    if arguments.rc:
        command.append("--prerelease")
    run(command)
    print(f"Published {tag}. Promote the build to `stable` in MafiaHub Services when it is ready for players.")


if __name__ == "__main__":
    main()
