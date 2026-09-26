# Server asset mods

Place `.zip` files directly in `mods/` under the server's working directory.
Every ZIP is loaded at startup, in case-insensitive alphabetical filename order.
Later ZIPs win when they contain the same game path. Restart the server after
adding, changing or removing a ZIP; script resource reloads retain the startup
mod snapshot.

ZIP paths start at the game data root, for example:

```text
missions/my_city/scene.4ds
missions/my_city/scene2.bin
missions/my_city/tree.klz
models/custom_car.4ds
maps/custom_texture.bmp
sounds/custom_sound.wav
```

Supported roots are `missions`, `models`, `maps`, `sounds`, `tables`, `anims`
and `fonts`. Use ASCII names. Paths are case-insensitive and normalized to
lowercase; both slash styles are accepted. There must be no enclosing mod-name
directory. DLLs, ASI patches, executables and script executables are rejected.
Native mission data such as `scene2.bin` is accepted; Mafia1Online still
suppresses the original single-player mission programs as it does for stock
missions. Multiplayer behavior belongs in server/client script resources.

A mission is detected when the combined ZIP contents provide both
`missions/<name>/scene.4ds` and `missions/<name>/tree.klz`. Detection permits
entirely new lowercase names as well as overrides of stock missions. Supply
the mission's other required files too: detection cannot prove that its native
scene, actors and referenced assets are valid. Select a detected mission with:

```json
{ "mod": { "mission": "my_city" } }
```

`World.changeMission("my_city")` also accepts detected names. The server and
client both validate mission availability. Custom mission names are limited to
63 characters. The sample gamemode's clock switches only between the two stock
Free Ride maps; it preserves a configured custom mission.

## Joining and caching

1. The initial authenticated connection obtains the mod manifest (names,
   sizes and SHA-256 hashes).
2. The existing MafiaNet delta downloader transfers missing/changed ZIPs and
   script packages. The download dialog shows the file, bytes and progress,
   with Cancel returning to the menu.
3. A background worker verifies every ZIP's SHA-256, validates its paths and
   expands its assets. Each entry's decompression and CRC must succeed. The
   complete file overlay is published only after every ZIP succeeds.
4. Client scripts and the ready handshake continue, then the client
   automatically enters the selected map. No native mission opens before the
   mod overlay is ready.

The server snapshots ZIPs in `mods/.cache/` under content-addressed names.
Clients retain those ZIPs in their existing per-server download cache, so
unchanged content is not downloaded again on reconnect. Extracted assets live
under the client installation's `cache/mods/<sha256>/`, separate from the
framework's script cache. Each join compares previously extracted files byte for
byte with the verified ZIP and reuses them only if they match, allowing multiple
clients to share the cache without rewriting open assets. Disconnect removes all active
overrides but retains downloads. A checksum failure aborts joining and removes
the corrupt ZIP so the next connection can repair it. Failed or canceled
preparation never activates partial content.

Limits: 64 ZIPs per server, under 512 MiB per ZIP, 32,768 entries per ZIP, 256 MiB per
file and 2 GiB of total expanded data per session. Symlinks, traversal, absolute
paths, ambiguous Windows names, encrypted archives and duplicate paths within
one ZIP are rejected. Paths must fit the native engine's 260-byte limit;
extraction uses short numbered filenames to leave room for the client path.

## Packaging the local example

[`tools/package_mod.py`](../tools/package_mod.py) creates deterministic ZIPs
from extracted data folders. The prepared local example is
[`mods/freeride-extended.zip`](../mods/freeride-extended.zip): 50 assets,
27,610,450 bytes, exposing `freeride_extended`. It was made from the downloaded
Mafia Freeride Extended Mod v3.1 by Firefox3860, excluding its replacement DLL,
`diff/` patch and optional add-ons. The source download is untouched.

```bash
python3 tools/package_mod.py \
  "$HOME/Downloads/Mafia_Freeride_Extended_Mod_v3.1/Mafia Freeride Extended Mod v3.1" \
  mods/freeride-extended.zip --mission freeride:freeride_extended
```

Omit `--mission` to preserve the original mission directory and override it.
The example ZIP is local and Git-ignored, alongside other mod binaries.

## Native integration and verification

The retail `rw_data.dll` audited here has SHA-256
`30783f20cdd5175dc106970492b39f6509d9b5a176d0f0fa171f0e7534eb1b5d`.
Its exported `_dtaOpen@8` (ordinal 7, RVA `0x18a0`) is
`int __stdcall(const char*, unsigned char archiveFirst)`. `-1` means failure;
other returns index the DLL's own file table. IDA confirms that the loose-file
path uses `CreateFileA` and registers the same native handle structure as normal
reads. `_dtaRead@12`, `_dtaSeek@12`, `_dtaGetTime@12` and `_dtaClose@4` therefore
continue to work unchanged. The internal archive lookup uses a 260-byte stack
buffer, which is why the overlay rejects longer physical filenames even when
Win32 itself could open them.

The hook resolves the export from the loaded module, redirects matching
game-relative reads to verified cache files, and forwards unmatched requests
unchanged. It never replaces the game DLL or modifies the game installation.
Open handles retain native ownership during disconnect. Routing is cleared on
disconnect and the hook is removed during client shutdown.

The m2o checkout uses the framework's `OnInitialAssetDownloadReady` deferral
before native world loading. Its script packages already have checksum-backed
transfer, but inspection did not find this automatic ZIP game-file overlay.
Mafia1Online reuses that framework transfer and deferral. The framework adds
`OnAssetStreamerReady` so game-owned ZIPs survive upload-list rebuilds when a
script resource is refreshed. Both the framework change and updated client/server
are required; the mod version is bumped to 1.0.0 for the new session contract.

The framework's upload registration now stores the same compact hash that the
receiver sends for its cache. MafiaNet 0.18.0's single-file `AddFile` stores the
whole file as comparison data, so it cannot match the receiver's four-byte hash
and repeatedly transfers unchanged files. The adapter fixes that comparison for
both ZIP mods and script packages; SHA-256 remains the content verification
step. See the pinned [DirectoryDeltaTransfer](https://github.com/MafiaHub/MafiaNet/blob/v0.18.0/Source/src/DirectoryDeltaTransfer.cpp)
and [FileList](https://github.com/MafiaHub/MafiaNet/blob/v0.18.0/Source/src/FileList.cpp)
implementations.

`Mafia1OnlineModAssetTests` exercises invalid paths/archives, overlap precedence,
missions split across ZIPs, cached reconnect, damaged extraction repair, checksum
failure and cancel/rejoin isolation. A live Linux-server/Wine-client test loaded
the packaged `freeride_extended` scene and collision and spawned a player after
download and verification. A second connection after restarting the server
transferred zero files and retained the cached ZIP's timestamp. Windows x86
client and asset tests, Windows x64 server and Linux x64 server builds passed.
The dialog was checked at 800, 1024 and 1920 pixels wide, including Cancel and
its removal after disconnect.
