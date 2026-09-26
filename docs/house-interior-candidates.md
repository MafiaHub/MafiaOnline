# House interior candidates

Surveyed the local `mods/freeride-extended.zip` on 2026-09-26. The package is
Freeride Extended 3.1 by Firefox3860, mounted as `freeride_extended`.

## First choices

| Interior | Area | Model asset | Scene instance | Suggested use |
| --- | --- | --- | --- | --- |
| Sarah's apartment | Little Italy | `models/quarters_sr.4ds` | `quarters` | First residential template |
| Paulie's apartment | Little Italy | `models/quarters_pl.4ds` | `qua_p` | Another apartment layout |
| Carlo's apartment | Little Italy | `models/quarters_cr.4ds` | `qua_c` | Third apartment layout |

All three model files are present in the server ZIP and referenced by its
`missions/freeride_extended/scene2.bin`, with their scene instances enabled.
The scene requests the `.i3d` names; the packaged native model files use `.4ds`.
Associated room sectors and furnishings are also referenced by the scene.
Their inclusion and Little Italy locations agree with the
[author's 3.1 release notes](https://www.moddb.com/mods/mafia-freeride-extended-mod/downloads/mafia-freeride-extended-mod-v31).

## Other candidates

- **Clark's Motel:** the ZIP contains `models/motel.4ds` and an enabled `motel`
  scene instance, with bedroom sectors such as `motel.&& pokoj1`. A bedroom
  could become a rental-room template. The author's bundled readme describes
  an upstairs bedroom used as a single-player save location.
- **Hoboken room:** the author lists a small room in the house belonging to
  Lucas's friend from mission 9. Suitable to investigate as a cheap single-room
  dwelling; its exact scene instance has not yet been identified.
- **Tommy's Oakwood house:** listed in the author's bundled readme as an
  accessible location/save point. Its usable interior extent still needs an
  in-game survey before treating it as a house template.

## Readiness and setup

This is an asset survey, not a completed walkability check. Safe arrival points,
exit headings, floor collision, visibility and remaining furnishings need an
in-game check with the current multiplayer actor filtering. The apartment model
roots are at `(0, 0, 0)` because their geometry carries its placement; those root
positions are not teleport destinations.

The first three apartments and motel are already in the selected mission, so
using their existing geometry requires no additional mission load. Reuse a room
for multiple houses through the existing per-house virtual worlds.

Once standing at a verified arrival/exit point inside a chosen apartment, use
`/interior_exit sarah_flat` (or `paulie_flat` / `carlo_flat`). Preview with
`/interior_go sarah_flat`, return with `/leave`, then assign it using
`/house_set <house-id> <price> sarah_flat`. This survey adds no live templates or
changes to owned houses.
