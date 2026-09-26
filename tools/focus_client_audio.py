#!/usr/bin/env python3
"""Keep audio from the focused Wine test client, including its CEF streams."""

import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path


GAME_CLASS = "mafia1onlinelauncher.exe"
POLL_SECONDS = 0.3
EXIT_AFTER_EMPTY_SECONDS = 8.0
STOP_ORPHANS_AFTER_SECONDS = 4.0
running = True


def command_json(*args):
    return json.loads(subprocess.check_output(args, stderr=subprocess.DEVNULL))


def wine_prefix(pid):
    try:
        environment = Path(f"/proc/{pid}/environ").read_bytes().split(b"\0")
    except (OSError, ValueError):
        return None
    for entry in environment:
        if entry.startswith(b"WINEPREFIX="):
            return entry.removeprefix(b"WINEPREFIX=").decode(errors="replace")
    return None


def process_alive(pid):
    try:
        return Path(f"/proc/{pid}/stat").read_text().split(") ", 1)[1][0] != "Z"
    except (OSError, IndexError):
        return False


def set_muted(stream, muted):
    subprocess.run(
        ("pactl", "set-sink-input-mute", str(stream), "1" if muted else "0"),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def main():
    if len(sys.argv) != 3:
        raise SystemExit("Usage: focus_client_audio.py <first-wine-prefix> <second-wine-prefix>")
    prefixes = {str(Path(prefix).resolve()) for prefix in sys.argv[1:]}
    original_mute = {}
    last_seen = time.monotonic()
    seen_prefixes = set()
    missing_since = {}
    cleaned_prefixes = set()

    def stop(_signal, _frame):
        global running
        running = False

    signal.signal(signal.SIGTERM, stop)

    try:
        while running:
            try:
                windows = [window for window in command_json("hyprctl", "-j", "clients")
                           if window.get("class") == GAME_CLASS and process_alive(window.get("pid"))]
                active = command_json("hyprctl", "-j", "activewindow").get("address")
                streams = command_json("pactl", "--format=json", "list", "sink-inputs")
            except (OSError, subprocess.CalledProcessError, ValueError):
                time.sleep(POLL_SECONDS)
                continue

            now = time.monotonic()
            if windows:
                last_seen = now
            live_prefixes = {wine_prefix(window["pid"]) for window in windows}
            live_prefixes.discard(None)
            seen_prefixes.update(live_prefixes & prefixes)
            for prefix in seen_prefixes:
                if prefix in live_prefixes:
                    missing_since.pop(prefix, None)
                    cleaned_prefixes.discard(prefix)
                else:
                    missing_since.setdefault(prefix, now)
                    if prefix not in cleaned_prefixes and now - missing_since[prefix] >= STOP_ORPHANS_AFTER_SECONDS:
                        # Wine can leave CEF helpers and their web-radio stream
                        # alive after the game's Linux process becomes a zombie.
                        subprocess.run(("wineserver", "-k"), env={**os.environ, "WINEPREFIX": prefix},
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
                        cleaned_prefixes.add(prefix)
            if not windows and now - last_seen >= EXIT_AFTER_EMPTY_SECONDS:
                break

            # The game and its CEF subprocesses inherit a dedicated WINEPREFIX.
            # A native game stream also carries the game window's Linux PID.
            game_pids = {str(window["pid"]) for window in windows}
            focused = active in {window["address"] for window in windows}
            focused_prefix = wine_prefix(next(window["pid"] for window in windows if window["address"] == active)) if focused else None
            live_streams = set()
            for stream in streams:
                stream_id = stream["index"]
                pid = str(stream.get("properties", {}).get("application.process.id", ""))
                prefix = wine_prefix(pid) if pid else None
                if pid not in game_pids and prefix not in prefixes:
                    continue
                live_streams.add(stream_id)
                if stream_id not in original_mute:
                    original_mute[stream_id] = bool(stream.get("mute", False))
                should_mute = original_mute[stream_id] or not (focused and prefix == focused_prefix)
                if bool(stream.get("mute", False)) != should_mute:
                    set_muted(stream_id, should_mute)

            for stream_id in set(original_mute) - live_streams:
                del original_mute[stream_id]
            time.sleep(POLL_SECONDS)
    finally:
        for stream_id, was_muted in original_mute.items():
            set_muted(stream_id, was_muted)


if __name__ == "__main__":
    main()
