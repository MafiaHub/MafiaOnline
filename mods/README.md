Put server asset ZIPs in this directory. The server loads them in case-insensitive
alphabetical order at startup; later ZIPs override earlier files. See
[server mods](../docs/server_mods.md) for the layout and lifecycle.

The local example `freeride-extended.zip` was packaged from the downloaded
Mafia Freeride Extended Mod v3.1 by Firefox3860. Its assets expose a separate
`freeride_extended` mission. Select it with `mod.mission` in the server config.
The author's DLL, binary patch and optional add-ons are not included. Original
single-player mission scripts remain subject to Mafia1Online's existing script
suppression; multiplayer gameplay comes from server resources.

Reproduce the example (adjust the source directory):

```bash
python3 tools/package_mod.py \
  "$HOME/Downloads/Mafia_Freeride_Extended_Mod_v3.1/Mafia Freeride Extended Mod v3.1" \
  mods/freeride-extended.zip --mission freeride:freeride_extended
```

ZIP binaries and server snapshots are ignored by Git.
