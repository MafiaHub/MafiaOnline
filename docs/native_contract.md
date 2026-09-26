# Retail executable contract

The only supported image is the `Game.exe` whose SHA-256 is
`303eb95ee2de3433511ce0cb518921dcb96b62b0f64ebfcc436723ff5083f298`.
It was audited from the Steam installation; a byte-identical copy may be used
without Steam.
It is a 32 bit PE with preferred base `0x400000`, entry point `0x6123b4`, and
image size `0x2dad76`. Its imports include `LS3DF.dll`, `IJoy.dll`, and
`rw_data.dll`; addresses inside those modules must come from their loaded
bases. The launcher checks the hash before the PE loader opens the game image.
The x86 launcher is linked at `0x400000` with a `.fwgame` reservation larger
than the retail image, so the Framework PE loader maps the game's absolute
addresses at their original values.

## In-process display setup

The retail setup is a packed `S_game_setup` of size `0x3f` at `0x647ee4`.
reM's field order and IDA data references agree on width at `+0x02`, height
at `+0x06`, fullscreen at `+0x15`, and suspend-when-inactive at `+0x1f`.
The independent SDK models these as typed `GameSetup` members with compile
time offset and size checks. `MafiaEntryPoint` loads the setup before calling
`InitSystem`. The mod temporarily sets width `1024`, height `768`, fullscreen
false, and suspend-when-inactive false for that call. It restores all four
original fields immediately afterward, on both success and failure. This
keeps the resulting IGraph window and render mode at 1024 by 768 while
preserving the user's persisted setup on the normal exit path. reM shows the
retail exit path and `HandleVideoMemOverflow` callback both write the global
setup to the `LS3D_setup` registry value. The exit path runs after the hook
restores the fields. The callback is registered during `InitSystem`, so a
second narrow hook restores the four original fields before forwarding a
synchronous video-memory-overflow error to the retail callback, which then
serializes and exits. No profile, game, or system file is edited directly by
the mod.

| Hook or call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `InitSystem` `0x5bf5a0` | `81 EC C4 02 00 00 53 55 56 57 89 54 24 18 8B E9 E8 6D 0C 05 00 33 DB 33 F6` | `bool __fastcall(HINSTANCE, bool* profileSelectionClosed)`; instance in `ECX`, out pointer in `EDX`, byte return in `AL`. Its sole retail call is `MafiaEntryPoint` at `0x5becf5`. IDA reads width at `0x5bf819`/`0x5bf841`, height at `0x5bf814`/`0x5bf83c`, fullscreen at `0x5bf8ae`, and suspend-inactive at `0x5bf8cb`; it forms the IGraph descriptor then calls virtual `IGraph::Init` at `0x5bf976`. It also mounts archives, initializes graphics, sound, 3D and menus, and reports failure through return false. The hook calls the original once with temporary typed setup fields and preserves its return/out parameter. | The hook is installed before the game entry point and removed before native shutdown. The global setup outlives `InitSystem`; no pointer to its temporary field values is retained. IDA finds no later direct reads of these four fields beyond initialization and setup serialization. Restore the original values even when initialization fails. |
| `HandleVideoMemOverflow` `0x5beb00` | `51 81 7C 24 08 02 00 00 80 0F 85 B6 00 00 00 8D 44 24 00 8D 4C 24 08` | `void __cdecl(int error, int, const char*)` in reM; IDA confirms the first stack argument is compared with `0x80000002`. Its sole retail reference is the error-callback pointer placed into the IGraph descriptor at `0x5bf832`. On that video-memory error it clears the texture-color setting, writes all `0x3f` bytes of `g_InitSettings` to the `LS3D_setup` registry value at `0x5beb55`, presents the error and exits. The hook restores only the four temporary display fields before chaining so a callback during `InitSystem` cannot serialize them. Other error codes retain the retail path. | The callback pointer is held by IGraph after successful initialization and may be called later. The hook is installed before `InitSystem` and removed before the original `CloseSystem` releases IGraph. The saved original fields are valid only while the InitSystem hook is active and are not retained across shutdown. |

The window is created through the retail graphics initialization. Fullscreen
must be false for a 1024 by 768 game window that can be placed beside another
client. Clearing suspend-when-inactive in the same descriptor lets a second
client continue ticking while its window lacks focus. Both settings remain
session local because the original setup fields are restored after IGraph
initialization.

### Window close during a multiplayer session

IDA of the installed `LS3DF.dll` confirms its window procedure handles
`WM_CLOSE` by calling `IGraph::Close()` immediately and then `exit(0)`. This
bypasses `MafiaEntryPoint`'s mission close and `CloseSystem`, and consequently
the Framework client shutdown. In a live two-client test the window vanished
but the launcher process remained in an `ntsync` wait. Host ptrace policy
blocked a stack trace, so the precise wait inside the close path is not yet
proven. The installed `IGraph::Close` has an infinite wait for its texture
manager thread near entry; that is a plausible source, not a confirmed one.

The mod intercepts only `WM_CLOSE`, then requests the existing native exit
route: it closes the live main menu with retail Exit result `24`, or asks an
active `C_game` loop to enter its Exit state. After the game loop returns,
the menu execution hook returns Exit result `24` to `MafiaEntryPoint`, which
closes the mission and calls `CloseSystem` in order. All other window messages
go to the original procedure. Because `LS3DF.dll` may load away from its
preferred base, the independent SDK resolves the procedure's `0x6da60` RVA
from the loaded module base.

| Hook or call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `LS3DF!MsgProc` preferred `0x1006da60` (`RVA 0x6da60`) | `6A FF 68 00 9F 09 10 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 51 55 8B 6C 24 1C 83 FD 14` | `LRESULT __stdcall(HWND, UINT, WPARAM, LPARAM)`, `ret 16`; registered as the main IGraph window procedure. IDA branch at `0x1006da9f` checks `WM_CLOSE=16`, calls IGraph vtable slot `+0x6c` at `0x1006daad`, then CRT `exit(0)` at `0x1006dab2`. `WM_DESTROY` posts WM_QUIT; activation and keyboard/system messages have separate paths. The hook consumes only close and forwards all other messages. | IGraph owns the HWND while initialized. Hook is installed after LS3DF loads and removed before native `CloseSystem` releases IGraph. The hook retains no window or game pointer. |
| `LS3DF!IGraph::Close` preferred `0x100701a0` | `53 33 DB 53 FF 15 A0 B1 09 10 F7 05 E8 55 1C 10 00 00 10 00 74 29` | Native IGraph virtual `Close` slot `+0x6c`. IDA confirms it calls `WaitForSingleObject(textureThread, INFINITE)` at `0x100701cc` when the thread flag and run flag are set, then releases buffers, textures, input and Direct3D. It is called by `MsgProc` on WM_CLOSE and by native graphics teardown. The mod does not call or hook it directly. | It must run only after mission and Framework users of the graphics driver have stopped, through normal `CloseSystem`; the close-message hook defers to that path. |

The hook records below were read from the retail image in IDA and checked
against reM's annotated `Game` source. Byte strings are the retail bytes
before the hook is installed. The exact hash and fixed base validate the
address constants in the independent `code/sdk` target before the game starts. Hooks live in the shell's
`core/boot`; future player, car and mission
hooks belong to their feature directories.

| Hook | Original bytes | Calling convention and lifetime | Callers and effects | Unload |
| --- | --- | --- | --- | --- |
| `C_mission::Tick` at `0x5407e0` | `53 55 56 8B F1 57 8A 46 7C 84 C0 74 20 83 7E 78 01 7D 1A` | `void __thiscall(C_mission*, unsigned int frameTimeMs)`; `this` in `ECX`, one 32 bit stack argument, `ret 4` at `0x540948`. `C_mission` survives mission close/open and is released during `CloseSystem`. | IDA xrefs `0x5495c6`, `0x5be8ea`, `0x5ebc1e`: menu and gameplay loops. It restores suspended audio, ticks time shifts, scene, driver, animated models, initialized `C_game`, and transparency records. The mod calls the original exactly once, then its own update. | The mod stops updating before `CloseSystem` tears down the mission. Both hooks are disabled at normal process shutdown. |
| `C_mission::Close` at `0x5405e0` | `53 56 8B F1 33 DB 38 5E 0C 0F 84 EE 01 00 00 8B 4E 24 3B CB 74 0D E8 65` | `void __thiscall(C_mission*)`; `this` in `ECX`, no stack arguments. The mission object survives close/open cycles. | Called by `Open`, `UnInit`, menu/gameplay loops and load helpers. It runs `C_game::Done`/`DoneIntern`, deletes actors and programs, clears caches and roads, and closes the scene. The world feature invalidates native handles before calling the original. | The world hook is removed in `Application::PreShutdown`, before the shell calls native `CloseSystem`; no handle survives disconnect or mission close. |
| `CloseSystem` at `0x5c0080` | `55 56 57 B9 28 46 6D 00 E8 A3 4F 04 00 E8 BE 86 E4 FF` | `void __cdecl()`; no arguments, one caller at `0x5bf4aa`. Runs once at normal process shutdown. | Tears down profile, animations, vehicle materials, consoles, then calls `C_mission::UnInit` and releases the graphics and sound drivers. The mod shuts down before calling the original. | This is the teardown path. No native game object is retained after it returns. |

`C_game::SetTrafficVisible` at `0x5a8470` starts with retail bytes
`53 8A 5C 24 08 8A CB E8 E4 64 EA FF 84 DB 0F 94 C3 8A CB E8 78 25 F0 FF`.
It is `void __thiscall(C_game*, bool visible)`, with the game in `ECX`, one
stack byte promoted to a 32 bit argument, and `ret 4` at `0x5a848f`. The
function does not dereference `this`. Retail call sites are `0x474744`,
`0x53d5de`, and `0x53d6b1`; the function calls
`C_traffic_generator::SetVisible` at `0x44e960` with `visible`, calls
`C_traffic_car::Scipni` at `0x4aaa00` with its inverse, and writes the same
inverse to `g_bPoliceFreezeActive` at `0x63d7ac`. The mod calls it only while
the native `C_game` is initialized and owned by the open mission. It has no
hook or retained pointer; native `C_mission::Close` owns cleanup. This function
does not control tram or metro generators or their visible frames.

### Stock traffic and rail behavior

reM's `C_traffic_generator::SetVisible` switches off existing active
pedestrian models, and `C_traffic_generator::TickElements` and its AI entry
return while `g_bTrafficVisible` is false. `C_traffic_car::Scipni` switches
off existing traffic car models and dynamic collisions; both traffic-car AI
paths return while `g_bTrafficCarsDisabled` is true. `C_game::Init` initially
enables traffic, so the world service calls `SetTrafficVisible(false)` only
after successful native Init and before the game loop. These native functions
keep their normal cleanup ownership.

`C_rail_generator` is separate. Its `GameInit` creates tram, metro and
passenger model pools with the vehicle root frames initially off, and its
`GameDone` releases those pools. The generator's `AI` method selects a track
and a free railway from the pools, assigns the railway, then maintains active
assignments. `C_railway::Update` turns the root and wagon frames on when its
passenger traffic mode activates. The mod suppresses only generator AI during
a server-owned mission. It leaves `GameInit` and `GameDone` native, so pool
resources still follow the retail lifecycle. No rail assignment is made in
the mod-owned game loop, and no tram or metro root or wagon frame is activated
by this generator.

The stock `FREERIDE` and `FREERIDENOC` `scene2.bin` archives contain
placed Salina and metro model frames, and their actor records assign actor
type 8 (`C_railway`). There are 38 named rail frames in the day scene and
36 in the night scene. They are independent of `C_rail_generator`; blocking
generator AI alone leaves these actors in the map. After `C_game::Init`,
the world service finds each named actor frame and calls the retail
`C_railway::Update` at `0x4894d0` in its `DEACTIVATING` mode. reM shows
that branch turning off the root and wagon frames and removing their dynamic
collisions, radar markers, engine and curve sounds. It then leaves the actor in
`INACTIVE` mode and suppresses `C_railway::AI` at `0x488140` for the two
Free Ride city missions, so proximity cannot reactivate it. The actor and
its allocations stay mission-owned for normal `GameDone` and `Close`.
Other stock missions still use their native railway AI.

### Drawbridges and city traffic lights

reM `C_bridge::GameInit` resets the bridge to closed and clears its shutdown
flag. During a multiplayer mission the client calls retail
`C_bridge::ShutDown(true)` immediately after that initialization, leaving the
deck, collision and bridge signals under the native closed state. Bridge AI
then ignores opening requests. Native `GameDone` and mission close still own
the actor.

reM `C_game::Tick` advances `m_uSemaphoreTime` through ten 3000 ms phases;
`m_iSemaphoreStateZ` starts five phases after `m_iSemaphoreStateX`.
`TickSemaphores` reads those phase fields for lamp visuals, and traffic AI
reads the same fields. The server replicates one 0..29999 ms cycle position
twice per second in the always-visible world state. Each client corrects the
native timer and derived X/Z phases and calls retail `TickSemaphores` when
the visible phase changes. Late joiners receive the current cycle position.

| Hook or call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_rail_generator::AI` `0x597bf0` | `83 EC 30 8B 44 24 34 53 55 56 57 8B F9 89 7C 24 18 8B B7 80 00 00 00` | `void __thiscall(C_rail_generator*, unsigned frameMs)`, `ret 4` at `0x597fdf`. IDA confirms the retail rail-generator vtable at `0x6259e8` points to this method from its AI slot `0x625a1c`; `C_actor::Tick` calls that virtual slot at `0x406563` whenever the actor is active and not network-controlled. IDA shows the method advancing its interval, finding nearby track nodes and free vehicles, and updating the track assignment records. reM identifies the tram/metro assignment and release logic. The hook calls the original outside a server mission and skips it within one. | Mission owns the generator actor. Native `GameInit` (`0x596860`, vtable `0x625a4c`) and `GameDone` (`0x597a20`, vtable `0x625a50`) remain installed and own hidden rail pools and teardown. No generator pointer is retained. The AI hook is removed before `CloseSystem`. |

## Mission transition contract

The retail `C_mission::Open` entry at `0x5409d0` begins with
`64 A1 00 00 00 00 6A FF 68 43 F6 61 00 50 64 89 25 00 00 00 00 83 EC 24`.
IDA shows `this` in `ECX`, four stack arguments, and `ret 10h`; reM identifies
them as the mission name, scene flags, model flags, and progress UI flag. It
calls `Close` first, then changes the stored mission name, loads scene, road,
path and binary data, and returns a status code. The retail caller set includes
`0x549151`, `0x54919a`, `0x549975`, the menu and gameplay loops at `0x5bd910`
and `0x5bebe0`, and load helpers at `0x604ee0` and `0x606150`. On failure it
can leave the previous world closed and the new world only partly loaded.

`C_mission::Close` at `0x5405e0` begins with
`53 56 8B F1 33 DB 38 5E 0C 0F 84 EE 01 00 00 8B 4E 24 3B CB 74 0D E8 65`.
It is a no-argument `__thiscall` on the same long-lived `C_mission` object;
IDA shows no stack arguments. It is also called by `Open`, `UnInit`, the menu
and gameplay loops. It calls `C_game::Done` and `DoneIntern`, deletes actors
and mission programs, clears caches and roads, and closes the scene. Native
actor pointers cannot survive it. `C_game::Init` at `0x5a0810` begins with
`64 A1 00 00 00 00 6A FF 68 1E 0B 62 00 50 64 89 25 00 00 00 00 81 EC C8`;
it is a no-argument `__thiscall` with a byte result and is called after
successful `Open` by the retail loops. Its paired `Done` is at `0x5a3c60`.

`Close` now has an invalidation hook under `client/src/features/world/`. Calls
to `Open`, `C_game::Init` and `C_game::Done` remain candidates for the later
mission transition feature. A server-driven call must first own the native
menu/game loop's `Close`/`UnInit` transition; the retail loop's script and
automatic spawn paths also need separate evidence. No mod call or hook to
`Open`, `Init` or `Done` is installed yet.

## Stock mission asset check

Read-only enumeration of the installed Steam `a1.dta` file table found 77
`missions\\<name>\\tree.klz` paths. The shared
`features/world/mission_catalog.h` contains exactly these 77 names, which the
server config schema accepts as lowercase directory names. The archive also
contains `00menu`, `autosalon` and `carcyclopedia` scenes without collision
trees; they are not gameplay mission targets. This catalog only proves that
the expected collision asset exists in the archive. The native scene loader
can still fail for another reason, so clients must report a completed load
before the server releases the transition barrier.

The retail gameplay loop's `g_bReloadMission` path calls `ReloadMission` at
`0x5bd910` after `C_mission::Tick`. In reM's matching `WinMain.cpp`, its
simple branch closes the active game and mission, calls `Open` for the next
name, loads `missions\\<name>\\tree.klz`, and calls `C_game::Init`; it does not
check the `Open` result. This path is only active inside the gameplay loop.
No hook or write to the reload globals has been added, because a failed open
can strand clients in a partially loaded world and the retail script and
automatic spawn paths still need to be suppressed.

### Main menu to game handoff (research, not installed)

IDA confirms `GM_Menu::ExecuteMenu` at `0x5eba40` begins with
`83 EC 18 55 56 8B F1 33 ED 3B F5 88 54 24 0C C7 44 24 10 E8 03 00 00 C6`.
reM identifies its three arguments as the menu pointer in `ECX`, a menu tick
flag in `EDX`, and a final stack boolean. It runs the native menu loop,
destroys that menu before returning the result, and is called by the main
menu, pause and death menus. The main menu branch in WinMain uses result 20
for Multiplayer, but its default case just reopens the menu; no gameplay loop
starts for that result. A future hook must distinguish the main menu before
calling the original, because its pointer is dead after the call. The hook
would be removed before `CloseSystem`.

IDA confirms `GameLoopSingle1` at `0x5be750` begins with
`64 A1 00 00 00 00 6A FF 68 4B 0E 62 00 50 A1 E0 7E 64 00 64 89 25 00 00`.
It is a no-argument `__cdecl` with three retail WinMain call sites at
`0x5bef6b`, `0x5bf203`, and `0x5bf3ac`. reM shows it returns immediately if
`C_game` is not initialized. Otherwise it owns input, mission tick, render,
pause menu, and native reload processing. The pause menu can invoke stock
save/load paths, so entering this loop also requires control of those paths.
Its caller closes the mission after it returns; a mod handoff must not leave
the old mission or handles alive when returning to WinMain.

`C_mission::OpenBin` at `0x541030` begins with
`6A FF 64 A1 00 00 00 00 68 57 F8 61 00 50 B8 08 20 00 00 64 89 25 00 00`.
IDA shows `C_mission::Open` at `0x5409d0` as its sole caller. reM shows
`OpenBin` loads scene objects, **creates stock actors** from the actor chunk,
and allocates mission programs from the program chunk. Its model flags do not
skip those two chunks. `C_game::Init` at `0x5a0810` then discovers the stock
player actor, initializes every actor, makes traffic visible, calls
`C_mission::GlobalProgramGameInit` and runs `GameInitStart`/`GameInitEnd`.
The `GlobalProgramGameInit` entry at `0x540520` begins with
`56 57 8B F9 8B 47 58 85 C0 74 1A 8B 77 5C 2B F0 C1 FE 02 74 10 8B 47 58`;
IDA finds its only caller inside `C_game::Init`. Suppressing only
`GlobalProgramRun` would still let stock programs initialize and leave stock
actors in the world. Both are released by native `C_mission::Close`; any
future filters must preserve that ownership and be removed before shutdown.

## Retail main menu under the web UI

The mod ships no menu definition. The retail `GM_MainMenu` loads its own
`mainmenu` definition and keeps running the `00menu` scene, camera and loop
that tick the client. The CEF page draws the connection screen over it, so the
retail components are hidden rather than replaced. Without the page, the menu
keeps only Exit; the connection then comes from quick join.

| Hook or call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `GM_MainMenu::OnCreate` `0x5e0690` | `56 8B F1 83 C9 FF E8 E5 B7 00 00 83 3D 18 48 6D 00 02 77 0E` | Virtual no-argument `__thiscall`, returns int. `GM_Menu::Create` calls it after building components. The hook calls the original (camera target, Continue state, freeride store, initial focus), then applies visibility: while the web UI is active every component loses draw bit `0x10`; otherwise only the retail items `990101`-`990112` do, leaving Exit `990113`. | No menu pointer outlives the menu: the list of components the suppression hid is cleared on every `OnCreate`, and restores only while that menu is the active main menu. Removed before `CloseSystem`. |
| `GM_MainMenu::OnClick` `0x5e06e0` | `64 A1 00 00 00 00 6A FF 68 2C 1B 62 00 50 64 89 25 00 00 00 00 53 8B 5C` | Virtual `int __thiscall(GM_MainMenu*, uint32_t)`, `ret 4`. `GM_Menu::Tick` dispatches component IDs. The hook passes only Exit (`990113`, which opens the retail exit confirmation) to the original, and nothing while the web UI is active; New Game, Load, Options and the other retail items can never start. | Retains nothing; removed before shutdown. |
| `GM_Menu::FindComponentByID` `0x5eb2f0` (layout evidence) | `8B 41 14 8B 49 18 56 3B C1 73 12 8B 54 24 08 8B 30 39 56 04` | Walks `GM_Component*` pointers from `menu+0x14` (begin) to `menu+0x18` (end) and compares the id at `component+0x04`. reM's `GM_Menu` layout asserts agree (`m_Components` at `+0x10`, allocator first). The SDK's `NativeMenu::ComponentsBegin/End` read the same fields. | The vector and components are owned by the native menu and freed by `ExecuteMenu`. |
| `GM_Menu::SetHidden` `0x5eaf20` | `8B 44 24 04 50 E8 C6 03 00 00 8A 4C 24 08 84 C9 74 0B 85 C0 74 0F 83 48 08 10` | Nonvirtual `__thiscall(menu, componentId, bool)`, `ret 8`. True **sets** draw bit `0x10` of the flags at `component+0x08` (`83 48 08 10`), false clears it (`83 60 08 EF`); draw and hit testing both require the bit. reM's name is inverted, so the SDK exposes it as `SetVisible`; `NativeComponent::SetVisible` flips the same bit on a component pointer. | Called only for live native menus; no component pointer is retained past the menu. |

`g_pActiveMenu` is the native pointer at `0x6bd890`. It is set by
`GM_Menu::ExecuteMenu`, changed while a child menu is open, and cleared after
the active menu is destroyed. The status updater checks its vtable against
`GM_MainMenu` before calling `SetText`; it does not retain a native menu
pointer.


## Server-selected mission handoff

The following entries were checked again in IDA against the supported retail
image before installing the mission handoff. The `C_mission` object is global:
IDA's WinMain loads its pointer from `0x63788c`; its game pointer is at
`C_mission + 0x24`. Both survive ordinary `Open`/`Close` cycles and are
destroyed only during `CloseSystem`. The collision object is the static
`g_collision` at `0x647f48` (WinMain call at `0x5bf1d6`). These offsets and
addresses are only used after the launcher validates the exact executable.

| Native entry | Original retail bytes | Convention, callers and side effects | Unload and failure |
| --- | --- | --- | --- |
| `GM_Menu::MenuCloseAll` `0x5eae40` | `8B 44 24 04 56 50 8B F1 E8 C3 FF FF FF 8B 76 04 85 F6 74 1E` | `void __thiscall(GM_Menu*, uint32_t closeResult, uint32_t loopResult)`, `ret 8`; called by main and child menu handlers. It marks the menu and parents for close and sets `g_uMenuResult`. Called only on a live main menu in its tick, with cancelled close result and retail Multiplayer loop result 20. | No pointer retained; `ExecuteMenu` destroys the menu. No patch at this entry. |
| `GM_Menu::ExecuteMenu` `0x5eba40` | `83 EC 18 55 56 8B F1 33 ED 3B F5 88 54 24 0C C7 44 24 10 E8 03 00 00` | Static `uint32_t __fastcall(GM_Menu*, bool tickMission, bool unused)`; IDA callers include WinMain `0x5bebe0` and gameplay `0x5be750`. It owns menu input, ticks and rendering, then destroys its menu before returning. The hook records main-menu identity before the original and runs the mod handoff only after it returns result 20. | Hook disabled/removed in client `PreShutdown`, before `CloseSystem`. It never dereferences the destroyed menu. |

The `ExecuteMenu` hook must be installed in `InitClient`, before WinMain
enters its first menu loop. Framework initializes from `C_mission::Tick`
*inside* that loop; installing the entry hook there leaves the current call
unwrapped. Live testing confirmed `MenuCloseAll` set loop result 20, but
WinMain received it directly and reopened the menu because retail treats
result 20 as an unhandled/default case. Mission lifecycle hooks can still be
installed from Framework `PostInit` because they do not need to wrap the
already active menu call.
| `C_mission::Open` `0x5409d0` | `64 A1 00 00 00 00 6A FF 68 43 F6 61 00 50 64 89 25 00 00 00 00 83 EC 24` | `int __thiscall(C_mission*, const char*, bool, uint32_t, bool)`, `ret 16`; IDA callers include `0x5bebe0`, `0x5bd910`, `0x549170`. It calls `Close`, loads scene, roads, actors and programs; zero means success. Called after menu destruction with retail flags `false, 0xffffffff, true`. | Failure can leave a partly loaded scene; the mod calls `Close`, reports failure and lets WinMain reopen `00menu`. No retained mission pointer across shutdown. |
| `g_collision::LoadCollision` `0x5c2b70` | `83 EC 48 53 56 8B F1 57 C7 86 C4 02 00 00 00 00 00 00 E8 49 FD FF FF` | `int __thiscall(g_collision*, const char*, bool)`, `ret 8`; WinMain calls it after Open at `0x5bf1db` and other mission branches. It deletes the previous collision tree, opens `missions\\<name>\\tree.klz`, links frames and builds static/dynamic grids. A positive grid length signals loaded collision; missing file returns a nonpositive value. | Native game teardown owns the object. On failure the mod closes the mission and reports failure. |
| `C_game::Init` `0x5a0810` | `64 A1 00 00 00 00 6A FF 68 1E 0B 62 00 50 64 89 25 00 00 00 00 81 EC C8` | `bool __thiscall(C_game*)`, byte result; called by WinMain and reload after Open/collision. It initializes game managers, selects the stock player from loaded actors, initializes actors, camera and HUD, then sets initialized. The mod calls it only after successful Open and collision load. | Paired native `C_mission::Close` calls `C_game::Done` and frees actors. Failure is reported, then Close runs. |
| `GameLoopSingle1` `0x5be750` | `64 A1 00 00 00 00 6A FF 68 4B 0E 62 00 50 A1 E0 7E 64 00 64 89 25 00 00` | `void __cdecl()`; WinMain calls it after `Init` at `0x5bef6b`, `0x5bf203`, `0x5bf3ac`. It processes input, ticks mission, renders and returns when `C_game` state is Exit or game uninitialized. The mod calls it only after successful Init. | Stack-local loop; no retained pointer. On return the mod closes the mission before giving control back to WinMain. |
| `C_game::SetState` `0x47b630` | `8B 44 24 04 89 41 48 C2 04 00` | `void __thiscall(C_game*, int)`, `ret 4`; IDA shows the value stored at `C_game+0x48`, and reM's enum gives Exit = 1. Native state setters call it; the mod uses Exit after a server mission change or disconnect so the native loop returns after the current tick. | Game pointer comes from the live mission and is not retained after `Close`. No patch at this entry. |
| Mission event fields `C_mission+0x70/+0x74` | WinMain `0x5bef4b`: `mov [eax+70h], ebx; mov [eax+74h], ebx` | Retail resets current and previous event before `C_game::Init`; reM `ResetEvents` confirms both fields. The mod performs the same two zero writes after Open/collision. | The mission object survives until `CloseSystem`; writes occur only in a live transition. |
| `C_mission::GlobalProgramGameInit` `0x540520` | `56 57 8B F9 8B 47 58 85 C0 74 1A 8B 77 5C 2B F0 C1 FE 02` | `void __thiscall(C_mission*)`; only IDA caller is `C_game::Init`. It initializes each loaded stock program. The hook suppresses it only during a mod-managed mission, preserving native core Init. | Removed before `CloseSystem`. The stock program allocations still belong to mission Close. |
| `C_mission::GlobalProgramGameDone` `0x540550` | `56 57 8B F9 8B 47 58 85 C0 74 1A 8B 77 5C 2B F0 C1 FE 02` | `void __thiscall(C_mission*)`; only IDA caller is `C_game::Done`. It runs stock program cleanup; skipped when program Init was skipped. | Removed before `CloseSystem`; mission Close deletes program allocations. |
| `C_mission::GlobalProgramRun` `0x540580` | `53 8B 5C 24 08 56 57 8B F9 53 8B 4F 24 E8 BE F7 06 00` | `void __thiscall(C_mission*, char*)`, `ret 4`; IDA callers include `C_game::Init`, `C_game::Done`, game tick and script dispatch. It invokes built-in game programs or a named stock mission program. Skipped only while the mod owns the mission. | Removed before `CloseSystem`; no program pointers retained. |

The handoff keeps native actor initialization and cleanup. Stock gameplay
programs are stopped at their common opcode dispatch, while normal game tick,
actor update and scene rendering continue. Traffic is disabled through the
native game call after Init; the rail-generator AI hook prevents tram and metro
assignment without bypassing rail pool cleanup. Ownership of stock mission
actors and server-controlled player creation still require multiplayer runtime
verification across mission transitions.

## Native car lifetime

The vehicle feature installs one hook on the exact retail `C_car` destructor.
IDA identifies `0x41bcb0` as a no-argument `__thiscall` with the car pointer in
`ECX`; its first bytes are
`6A FF 68 99 DB 61 00 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 08`.
Its sole direct code caller is the scalar deleting destructor at `0x41bc90`,
which is referenced by the `C_car` vtable at `0x623658`. That wrapper calls
the destructor and frees memory when its stack flag requests it. The car is
game-owned from actor initialization until this deleting destructor; native
mission close and temporary actor removal can both reach that path. The
destructor removes the radar car entry, frees car-owned vectors, destroys the
`C_Vehicle` subobject, then destroys `C_actor`. reM's `C_car.cpp` agrees with
the entry and radar side effect. The hook invalidates the matching network
handle **before** the original can destroy the car. It neither frees nor
retains the car. The hook is disabled and removed in client `PreShutdown`,
before `CloseSystem`; mission close also clears the entire registry before
native actor destruction. For mod-created temporary cars, the hook takes the
scene frame from the car service before calling the original destructor, then
calls the audited scene `DeleteFrame` after native destruction. It never reads
the frame from the destroyed car.

### Car creation path

IDA confirms retail `C_mission::CreateActor` at `0x53f7d0` allocates a
`0x221c` byte `C_car` for actor type 4 and calls its constructor at
`0x41bb80`. reM's script cheat path provides a complete example: create an
`I3D_model` frame through the loaded LS3DF driver, load the model through
`sModelCache`, place the model in the scene, initialize and game-initialize
the car, register it with `C_game::InternTempActAdd`, then release the local
frame reference. The cache and LS3DF frame interfaces and their ownership are
audited below. The client creates a temporary native car for each streamed
server car through the mission factory and game temporary-actor path. It
applies sampled server poses through the live model frame and queues native
removal when the replica streams out. Model-open and Init failures roll back
the actor and both scene and creator frame references. A model-load failure
is not retried until the replica streams in again or the mission changes.
This path has passed compilation but still needs live spawn, despawn, and
mission-close tests.

## Optional direct connection and existing profile selection

The optional repository-local `builds/build-32/bin/config/client.json` is read before Mafia's
startup menus. The retail profile picker is never shown: a profile only
carries bindings and settings here. The retail `G_LoadSaveClass::Init` still
initializes defaults, HUD, materials, input bindings and the `00menu` scene,
enumerates profile headers, then calls `GM_Menu::ExecuteMenu` for
`GM_Menu_profileselect`. The mod uses the existing menu execute hook before
the picker is created, selects `quickJoin.profileIndex` (read even when quick
join is disabled, default 0, falling back to the first profile when absent),
destroys the uncreated menu through the already-audited `GM_Menu::Destroy`,
and returns 16. The retail Init then releases the enumeration, loads the
chosen profile and publishes native bindings. The main menu loop supplies
native scene ticks until the connection and server mission are ready; quick
join enters that mission without a click. No native profile is created.

| Retail item | Original bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `GM_Menu_profileselect` vtable `0x6279dc` | `10 50 60 00 C0 84 5E 00 20 92 40 00 00 A4 47 00` | IDA shows the vtable written into the newly allocated `0x54` byte picker at `G_LoadSaveClass::Init` `0x604f97` and `ProfileChange` `0x6050c7`. It identifies only this menu subclass, before `ExecuteMenu` creates components. | The skipped menu allocation is destroyed once by `GM_Menu::Destroy`; no menu pointer is retained. The vtable is valid for the exact supported image. |
| `G_LoadSaveClass::ProfileSelect` `0x605570` | `56 57 8B 79 04 85 FF 74 37 8B 51 08 B8 31 0C C3 30 2B D7` | `void __thiscall(loadSave*, unsigned index)`, `ret 4`. IDA callers are picker `OnClick` `0x5e8949` and profile creation `0x60570a`. It compares the index with the enumerated `0x54` byte header count and copies the chosen header into `m_SelectedProfile` at `+0x20`; it neither creates nor writes a profile. reM agrees. | Called only between `ProfileEnumFiles` and `ProfileEnumRelease` inside native Init. The header vector starts with allocator state followed by begin/end/capacity pointers at `+0x04/+0x08/+0x0c`; no pointer is kept after return. |
| `G_LoadSaveClass::ProfileSave` `0x6059f0` | `6A FF 68 AB 20 62 00 64 A1 00 00 00 00 50 64 89 25 00 00 00 00` | `void __thiscall(loadSave*)`; IDA callers at `0x60503e`, `0x605097`, and `0x607334` cover native Close, profile change and refresh. It opens `savegame\\mafia%03d.sav` for writing, then writes profile header, bindings, control settings and reserved data. The mod guard suppresses this file-writing call. | Hook installed before native Init for every mod session. It stays active through native `CloseSystem`, whose profile Close may save, and is removed immediately after `CloseSystem` returns. It owns no profile memory or file. |
| `G_LoadSaveClass::ProfileCreate` `0x6055c0` | `6A FF 68 6B 20 62 00 64 A1 00 00 00 00 50 64 89 25 00 00 00 00` | `bool __thiscall(loadSave*, unsigned char* name)`, `ret 4`; the picker `OnCloseChild` calls it at `0x5e86fe` after the create child returns (IDA bytes `8D 4E 34 51 B9 28 46 6D 00 E8 BD CE 01 00`). IDA shows it creates `savegame`, writes a new `mafia%03d.sav`, appends a header and selects it. The mod guard returns false before any of those effects. | Hook active from before native Init through `CloseSystem`; no name pointer is retained. The picker that reaches it is never shown. |
| `G_LoadSaveClass::ProfileDelete` `0x605740` | `81 EC 80 00 00 00 53 55 8B E9 8B 8C 24 8C 00 00 00 56 57` | `void __thiscall(loadSave*, unsigned index)`, `ret 4`; IDA caller `GM_Menu_profiledelete::OnClick` at `0x5e8c24`. It unlinks the selected profile file and up to 600 save slots, then removes its header. The mod guard returns without these effects. | Same startup/shutdown lifetime as Create. The picker that reaches it is never shown. |
| `G_LoadSaveClass::SaveGameSave` `0x606ea0` | `6A FF 68 6B 21 62 00 64 A1 00 00 00 00 50 64 89 25 00 00 00 00` | `void __thiscall(loadSave*, bool quickSave)`, `ret 4`; IDA caller is `Refresh` `0x607326`. It writes `savegame\\mafia%03d.%03d` with screenshot and world state. The mod guard suppresses this write. | Hook active for the entire mod session through native `CloseSystem`; no save buffer is created. |

The selected profile remains game-owned. Mafia1Online does not mutate the
profile header or game installation. Profile and save-game writes are
suppressed in every mod session, including native shutdown. An existing
profile is required; with none present, a startup error is shown and the
skipped picker returns Back without creating a file.

## Native chat and health display

The chat feature uses the game's existing indicator font and scene render
callback. Its keyboard hook is resolved from the loaded `LS3DF.dll` base;
the preferred addresses below are evidence from IDA, never runtime absolute
addresses. The indicators object is the game-owned static at `0x6bf980`;
`C_Input` is the game-owned static at `0x647c30`. Both exist during the
menu and gameplay loops, and the mod removes these hooks before
`CloseSystem` starts tearing down input, graphics and indicators.

| Hook or call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `cbScene` `0x5bd8d0` | `8B 44 24 04 83 E8 02 75 2A E8 02 96 01 00 E8 4D 96 01 00 B9 80 F9 6B 00` | `uint32_t __stdcall(message, arg1, arg2)`, `ret 0ch`. The scene callback pointer is passed to `SetCallback` by `MafiaEntryPoint` at `0x5bfc4f`; IDA shows message 2 draws inventory, debug lines, indicators, the active menu, then native console. The hook updates server health before the original draws and draws the active chat input after it. Other messages pass through unchanged. | The callback belongs to the live native scene; it retains no scene pointer. Removed in `Application::PreShutdown` before `CloseSystem`; no render call is made after removal. |
| `G_IndicatorsClass::PlayerSetLives` `0x5f88e0` | `8B 44 24 04 89 81 10 42 00 00 50 81 C1 1C 42 00 00 68 4C 81` | `void __thiscall(indicators*, unsigned health)`, `ret 4`. Retail callers include `C_game::Init` `0x5a0810`, reload `0x5bd910`, and human status updates `0x58a5a0`. It stores health at `+0x4210` and formats the native lives text at `+0x421c`; the existing indicator draw path renders it when the lives flag is set. | Called only during scene render for a ready mission and current local authoritative combat state. The static indicators object remains alive until game shutdown. |
| `G_IndicatorsClass::OutText` `0x603880` | `B8 E4 38 00 00 E8 76 D3 00 00 53 8B 9C 24 EC 38 00 00 85 DB 89 8C 24 9C` | `float __thiscall(indicators*, unsigned char* text, float x, float y, float width, float height, unsigned color, unsigned options, unsigned fontId, unsigned char* end)`. IDA finds calls in `DrawAll` `0x5fb060`, menu text routines and other HUD functions. It converts the text through the indicator font map and emits transformed vertices; font ID 3 is the stock console font. | Called only inside scene message 2 after the original render callback, while indicator fonts and graphics are live. It does not retain the text; the chat owns its string. |
| `G_IndicatorsClass::ConsoleAddText` `0x5f9d50` | `53 8B 5C 24 08 56 8B F1 85 DB 74 73 8B 8E E8 43 00 00 33 D2 83 F9 05` | `void __thiscall(indicators*, unsigned char* text, unsigned color)`, `ret 8`. IDA callers include mission/script messages at `0x582416`, `0x608326`, `0x608c9f`. It copies at most 64 bytes into a five-line ring, masks color to RGB and sets a five-second lifetime. Chat must provide at most 63 native bytes so the copied line has a terminator. | Called on the game thread only while the mission is ready. The text buffer is copied; no caller storage is retained. The native indicator tick and teardown own the ring. |
| `C_Input::Update` `0x4f0620` | `83 EC 60 53 8A 5C 24 68 55 8B E9 56 57 8B 45 08 85 C0 74 65 8B 4D 0C 2B` | `void __thiscall(C_Input*, bool acquire)`, `ret 4`; IDA callers are the main menu, single-player and replay loops. It reads keyboard, mouse and joystick state, then fills action state/pressed vectors. The chat hook calls the original once, then clears both published action vectors while chat input is open or a just-handled key remains down, so typing cannot drive the player. | The static input object remains live during each loop. The hook owns no native input memory; it is removed before `CloseSystem`. |
| `C_Input::GetState` `0x4f01a0` | `F6 44 24 08 01 74 49 8B 91 D4 00 00 00 85 D2 74 10 8B 81 D8 00 00 00 2B` | `int __thiscall(C_Input*, float** output, bool pressedOnly)`, `ret 8`. IDA callers include the game and menu loops; it returns a borrowed vector pointer and element count, or -1 when unavailable. The chat hook calls it twice after `Update`, once for current and once for pressed state, then zeros only positive returned counts. | The vectors belong to the same live `C_Input` object and are used before any later input update can reallocate them. No pointer is retained. |
| `IGraph::ReadKey` LS3DF preferred `0x10071960` | `F6 05 BC 52 1C 10 02 56 74 29 8B 0D 40 59 1C 10 A1 78 54 1C 10 C7 05 78` | `unsigned __stdcall(IGraph*)`, `ret 4`, via IGraph vtable slot `+0x9c`. Retail game loops call it after the input update. In normal mode it returns the new DirectInput scan code and stores its translated character in LS3DF global preferred `0x101c546c`; in buffered mode it consumes one buffered key/character. The hook delegates to the original. During a ready mission it handles T, Enter, Escape, Backspace and printable chat characters; handled scan codes return zero to retail logic until release, including the second `ReadKey` call in a retail frame. During any server-owned mission it also consumes Escape before retail gameplay sees it. Chat receives Escape first when ready, so it still cancels text entry. | Runtime function and character global use offsets from the loaded `LS3DF.dll` base. The IGraph object and DLL remain live until graphics shutdown. The hook is removed before `CloseSystem`; it retains no graph pointer or translated character address beyond the DLL lifetime. |

IDA checked the exact Steam `Game.exe` gameplay loop at `0x5be750`
(original entry bytes `64 A1 00 00 00 00 6A FF 68 4B 0E 62 00 50 A1 E0 7E
64 00 64 89 25 00 00`). At `0x5be7c3` it calls IGraph's `ReadKey` vtable
slot `+0x9c` for the gameplay key, then calls it again at `0x5be825` for cheat
input. At `0x5be916` it compares the first scan code with DirectInput Escape
(`1`). The Escape branch calls `C_game::PauseAllSounds` at `0x5be943`, creates
`GM_GameMenu`, and runs `GM_Menu::ExecuteMenu`; its result can exit the game
loop or invoke native load. The pause menu blocks the single-player loop, so
it must not open during multiplayer. This is a branch in the existing
`GameLoopSingle1` function, not a new hook or patch. The ReadKey hook feeds it
zero only while `World::NativeMissionActive()` is true; native menu and profile
input retain their stock Escape behavior. The hook retains no mission pointer,
and `World::SetNativeMissionActive(false)` clears the condition on every
mission exit before input/graphics teardown.

The indicator scale factors at `+0x40c0/+0x40c4` and menu offset at
`+0x4b80` are established by reM's layout assertions and IDA's `DrawAll`
reads. The chat input uses those values to place one short line above the
game's existing five-line console area. The lives flag is bit `0x4` of
indicator flags at `+0x40a4`; IDA's `DrawAll` tests it before drawing
the Mafia HUD lives display. The hook sets that bit only while the current
local replicated combat state is available.

## Intro video suppression

| Hook | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `PlayVideo` `0x55d460` | `81 EC 14 02 00 00 53 55 56 8B E9 8D 74 24 20 57 89 6C 24 10 8B C5 2B F5` | `bool __fastcall(const char* name, float width, float height)`: name in `ECX`, floats on stack, byte result in `AL`, `ret 8` at `0x55d765`. IDA shows the three startup callers at `0x5bfdeb`, `0x5bfdff`, `0x5bfe13` and a mission script caller at `0x47880c`; reM identifies these as `logo1`, `logo3`, `logo2`, and `intro`. The original locates an AVI or BIK, creates an `IShow`, opens and plays it, waits for key press or end, then releases it. The hook returns success for those four names and delegates all other video names to the original. | Installed before the game entry point, so it covers startup logos. It owns no game object or video handle and is removed in client `PreShutdown`, before native `CloseSystem`. |

## Player observation contract

The first player stage observes the stock player selected by `C_game::Init`
after the world service reports a successful native load. IDA's main menu
caller at `0x5ebc1e` passes the pointer stored at `0x63788c` to
`C_mission::Tick`; reM identifies this global as `g_pMission`. The mission
layout has `m_pGame` at `+0x24`, and the game has `m_pPlayer` at `+0xe4`.
Retail `C_game::GetPlayer` at `0x47b520` is exactly
`8B 81 E4 00 00 00 C3`, confirming the latter offset. The native pointer
is borrowed and may legitimately be null before `Init`, after `Done`, or in a
mission without a player. The player's actor type at `+0x10` is 2; IDA's
`C_mission::CreateActor` case 2 allocates the `0xb00` byte `C_player` and
calls its constructor at `0x58f170`. No caller retains a pointer across
`C_mission::Close`; the world registry invalidates it before native cleanup.

The independent SDK represents these borrowed objects as `NativeMission`,
`NativeGame`, `NativeActor`, and `NativeHuman` views, with compile-time
`offsetof` checks against reM's retail layout assertions. Mission scene,
game, and event fields are at `+0x10`, `+0x24`, `+0x70`, and `+0x74`.
`NativeGame` checks initialized, state, player, death-trigger, and death-timer
fields at `+0x40`, `+0x48`, `+0xe4`, `+0x2ad8`, and `+0x2fd4`.
`NativeActor` checks type, network-control, position, direction, alive, dead,
frame, and frame-release fields at `+0x10`, `+0x20`, `+0x24`, `+0x30`,
`+0x5d`, `+0x5e`, `+0x68`, and `+0x6c`. A human's current properties begin at
`+0x640`; reM's inline `C_human::GetHealth` and `SetHealth` address the float
at `+0x644`. Updating this float does not perform native revival or a death
animation transition, so the SDK exposes it only as a property view.

| Native call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_actor::GetWorldPos` `0x407fe0` | `56 8B 71 68 85 F6 74 2D F6 86 AC 00 00 00 20 75 07 8B CE E8 38 7C 20 00` | `S_vector __thiscall(C_actor*)`: actor in `ECX`, hidden 12 byte result pointer on stack, `ret 4`. IDA callers include `0x426340`, `0x44ccf0`, `0x49a300`, `0x530ad0`, `0x5350b0`, and `0x5fb060`; reM identifies actor world-position reads. If its frame exists, the call lazily refreshes the frame world matrix and copies translation; otherwise it returns the actor's stored position. | Called only for a registry-resolved actor while the matching native mission generation is ready. It takes no reference or allocation. The pointer and handle are invalidated before mission close and at disconnect/stream out; no hook needs removal. |
| `C_actor::GetWorldDir` `0x408040` | `83 EC 10 56 8B 71 68 85 F6 0F 84 61 01 00 00 F6 86 AC 00 00 00 20 75 07 8B` | `S_vector __thiscall(C_actor*)`: actor in `ECX`, hidden 12 byte result pointer on stack, `ret 4`. IDA callers include `0x44ccf0`, `0x5350b0`, `0x570740`, `0x5721b0`, `0x581220`, and `0x587d70`. It lazily refreshes the frame matrix and normalizes its forward vector, falling back to the stored actor direction when no frame exists. | Same borrowed actor and mission-generation rule as world position. It owns no memory and is not called during native unload. |

Native dynamic player creation remains under audit. reM's actor-duplicate
script creates an I3D model, loads it through the model cache, links it into
the scene, calls `C_mission::CreateActor`, initializes the actor, and registers
it with `C_game::AddTemporaryActor`. `C_entity` (type 27) is the NPC human
class. That chain has several LS3DF virtual calls and owns model/frame
references, so no player or car feature may call part of it until each call
and its failure cleanup have been verified in IDA.

### Temporary actor factory audit

The following is the retail creation/removal path. All LS3DF calls below are
through a live object's vtable, so a relocated `LS3DF.dll` is supported. IDA
examined the installed DLL at preferred base `0x10000000`; the addresses in
this table identify the original functions and bytes, not call targets to
hardcode. The game calls the same slots in `FreerideSetup` at `0x60e9d0` and
the script actor-duplicate branch in `C_program::Process` at `0x46d3b0`.

| Call | Original bytes | Convention and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `I3D_driver::CreateFrame` vtable `+0x50` (`0x100193e0`) | `64 A1 00 00 00 00 8B 4C 24 08 6A FF 68 50 93 09 10 50 64 89 25 00 00 00` | `I3D_frame* __stdcall(driver*, int frameType)`, type 9 creates an `I3D_model` with one reference. IDA's `FreerideSetup` calls this slot at `0x60ea12`; caller handles null. | Driver global `0x647ed8` exists until `CloseSystem`; caller releases a frame on each failure or transfers its reference to an initialized actor. |
| `C_I3D_model_cache::Open` `0x4087e0` | `6A FF 68 E6 D9 61 00 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 20` | `LS3D_RESULT __thiscall(cache*, model*, const char*, flags, callback, context, unused)`; `ret 18h`. IDA callers include `0x541030`, `0x5a0810`, `0x60e9d0`; at `0x60ea5f` the latter passes cache global `0x647dd0`. It opens a model from the configured game archive directories, or duplicates a cached one; negative results mean failure. | Cache owns its own duplicates and is cleared by native shutdown. Caller owns the destination model and releases it on failure. |
| `I3D_model::SetName` vtable `+0x28` (`0x10033b30`) | `8B 44 24 08 56 8B 74 24 08 50 56 E8 B0 7B FE FF 85 C0 7C 07 8B CE E8 75` | `LS3D_RESULT __stdcall(model*, const char*)`; IDA resolves this model override from vtable `0x1009c160`; `FreerideSetup` calls the slot at `0x60ea1c`. It copies the name and propagates it to child frames; negative result is failure. | Name memory is model-owned after the call; frame release cleans it. |
| `I3D_frame::LinkTo` vtable `+0x2c` (`0x1001b9d0`) | `81 EC 90 00 00 00 53 8B 9C 24 9C 00 00 00 55 8B AC 24 9C 00 00 00 8B 85` | `LS3D_RESULT __stdcall(frame*, parent*, flags)`; script actor duplicate uses this slot. It links into a parent sector and changes scene-ring membership; flags zero do not preserve old world transform. | Parent sector is mission-owned. Unlink/release occurs in actor or mission teardown. |
| `I3D_frame::SetWorldPos` vtable `+0x04` (`0x1001b160`) | `83 EC 4C 56 8B 74 24 54 57 8B BE 20 01 00 00 85 FF 75 48 8A 86 AC 00 00` | `void __stdcall(frame*, const S_vector*)`. Converts world position through the parent inverse and invalidates the cached transform; its use is visible in the retail actor setup path. | Does not retain the vector. Frame must remain live through the call. |
| `I3D_frame::SetDir` vtable `+0x0c` (`0x1001ab50`) | `81 EC D8 00 00 00 53 56 8B B4 24 E8 00 00 00 D9 06 57 D9 E1 C7 44 24 14` | `void __stdcall(frame*, const S_vector*, float roll)`. Computes local orientation, updating flags; retail FreerideSetup calls it for the player and car frames. | Does not retain the vector. A following `Update` publishes the transform. |
| `I3D_frame::Update` vtable `+0x18` (`0x1001ca60`) | `55 56 57 8B 7C 24 10 8B AF AC 00 00 00 81 E5 00 20 00 40 74 2D 8B 8F 18` | `void __stdcall(frame*)`; recursively refreshes scene graph state. Retail setup calls after position/direction and before actor Init. | No retained pointer; must run while parent sector lives. |
| `I3D_scene::AddFrame` vtable `+0x5c` (`0x10049290`) | `53 8B 5C 24 0C 39 9B 0C 01 00 00 56 57 0F 84 48 01 00 00 8B 53 04 42 8B` | `void __stdcall(scene*, frame*)`; IDA shows it increments the frame refcount and attaches scene ownership. Retail FreerideSetup calls this for the player/car frames. | `C_human::GameInit` adds another reference then removes the frame from scene; the actor's `m_bRemoveFrame` releases the remaining actor reference at destruction. The creator releases its own reference after successful `AddTemporaryActor`. |
| `I3D_scene::DeleteFrame` vtable `+0x60` (`0x10049400`) | `8B 54 24 04 8B 82 34 02 00 00 85 C0 53 56 57 0F 8E F2 00 00 00 83 F8 01` | `LS3D_RESULT __stdcall(scene*, frame*)`; `C_human::GameInit` calls this after incrementing the frame refcount, and failure cleanup may use it after AddFrame. It removes scene ownership and releases that reference. | Only needed in an incomplete creation rollback; model remains caller-owned until final release. |
| `I3D_frame::Release` vtable `+0` (`0x1001aae0`) | `8B 4C 24 04 8B 41 04 48 85 C0 89 41 04 7F 09 8B 01 6A 01 FF 50 40 33 C0` | `int __stdcall(frame*)`; decrements refcount and dispatches deleting destructor at zero. Retail setup releases its creator reference after adding frames to the scene. | Must not be used after it returns zero; actor destructor also calls it when `m_bRemoveFrame` is set. |
| `C_mission::CreateActor` `0x53f7d0` | `64 A1 00 00 00 00 8B 4C 24 04 6A FF 68 D9 F5 61 00 50 33 C0 49 64 89 25` | `C_actor* __thiscall(mission*, int type)`, one stack argument and `ret 4`; `this` is optimized unused. IDA callers include `OpenBin`, save/load, FreerideSetup and scripts. Type 27 allocates `C_entity` (`C_human` subclass); type 4 allocates `C_car`. Returns null on allocation or unsupported type. | Constructor gives one actor reference. Virtual `Release` at actor vtable `+0x44` destroys an unregistered actor on failure. |
| `C_actor::Init` virtual `+0x48`; `C_entity::Init` `0x507470` | `8B 44 24 04 89 89 E0 0A 00 00 50 E8 D0 7E 06 00 84 C0 0F 95 C0 C2 04 00` | `bool __thiscall(actor*, frame*)`, `ret 4`. Vtable `0x625090 + 0x48` resolves to this entry for `C_entity`. It calls `C_human::Init`, which requires a model frame and attaches it to the actor. | On failure release actor and creator frame; after success the actor owns the frame when `m_bRemoveFrame` at actor `+0x6c` is set. |
| `C_game::AddTemporaryActor` `0x5a77c0` | `51 53 55 56 57 8B F9 8B 4C 24 18 BB 04 00 00 00 8B 97 24 01 00 00 8D B7` | `void __thiscall(game*, actor*)`, `ret 4`. IDA callers include script processing `0x46d3b0`, human actions and effects. It invokes virtual `GameInit` and inserts active/inactive actor into game lists and temporary ownership. | Game removes it via `RemoveTemporaryActor`/`_RemoveTemporaryActor` or `Done`; actor pointer becomes invalid at virtual `Release`. |
| `C_game::RemoveTemporaryActor` `0x5a79a0` | `8B 91 34 01 00 00 83 EC 08 85 D2 56 8D B1 30 01 00 00 57 8B 7C 24 14 74` | `void __thiscall(game*, actor*)`, `ret 4`. IDA callers include `C_program::Process` and game action paths. Marks the actor dead and queues delayed removal; `_RemoveTemporaryActor` later runs invalidation, `GameDone`, and `Release`. | The mod invalidates its handle before asking for removal and never dereferences the queued pointer again. Mission close can release it earlier. |

For native network car creation and retirement, the following calls and
ownership details are also verified against reM and the retail IDA image:

| Call | Original bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_car::Init` virtual actor slot `+0x48`, `0x41bd80` | `81 EC 58 02 00 00 53 8B 9C 24 60 02 00 00 55 56 57 8B E9 33 FF 53 89 BD` | `bool __thiscall(car*, I3D_model*)`, `ret 4`. The car vtable at `0x623618` points to this entry. It first calls `C_actor::Init`, then `C_Vehicle::Init`, then finds AI, seat, body, fuel and model frames. IDA returns zero at several validation failures. | On failure, release the unregistered actor, remove a frame already added to the scene, and release the creator's frame reference. `C_Vehicle` owns an extra frame reference once its Init begins; its destructor releases it. No native pointer is bound until Init succeeds. |
| `C_car::GameInit` virtual actor slot `+0x64`, `0x41ed00` | `83 EC 1C 53 55 8B E9 56 33 DB 57 8D 75 70 53 8B CE 89 74 24 18 E8 66 CA` | `void __thiscall(car*)`. The retail car vtable pointer at `0x62367c` resolves to this entry, and `C_game::AddTemporaryActor` calls that virtual slot. reM shows it registers seat use objects, initializes vehicle physics, sounds, lights and shadow, and marks active runtime state. | Called by the game during temporary actor registration, after `C_car::Init` succeeds and while mission scene/game services are live. The matching virtual `GameDone` runs on temporary removal or mission close. |
| `C_car::SetParticlesActive` `0x47b440` | `8A 44 24 04 88 81 0C 22 00 00 C2 04 00` | `void __thiscall(car*, bool)`, `ret 4`; IDA's retail car cheat at `0x478742` is the direct caller. It sets the `m_bParticlesActive` byte at car `+0x220c`. | Called after `C_game::AddTemporaryActor` has run `GameInit`. This flag makes `C_car::GameDone` unlink its dynamically created model and release the vehicle's frame reference. |
| `C_car::GameDone` virtual actor slot `+0x68`, `0x41f5e0` | `55 8B E9 56 57 8D 75 70 33 FF 57 8B CE E8 8E C1 0A 00 8B CE E8 A7 5F 0A` | `void __thiscall(car*)`. The game calls the virtual slot from `_RemoveTemporaryActor` `0x5a7ec0` and `ClearTemporaryActors` during mission close. It removes seat use objects, disables collision and sounds, and for particle-active cars unlinks/releases the model and clears both `C_Vehicle` and `C_actor` frame pointers. | Never call directly; `C_game` invokes it before virtual actor `Release`. The car destructor hook observes the final destruction before the scene closes. |
| `C_game::_RemoveTemporaryActor` `0x5a7ec0` | `8B 91 24 01 00 00 53 56 57 85 D2 74 7E 8B 81 28 01 00 00 2B C2 C1 F8 02` | `void __thiscall(game*, actor*)`, `ret 4`; called from game tick at `0x5a58cc` and `0x5a63ae`. It removes the actor from temporary ownership, invalidates references, invokes virtual `GameDone` then virtual `Release`. | This is the completion of queued `RemoveTemporaryActor`; the mod must not dereference its borrowed actor pointer after queuing removal. The destructor hook invalidates the native handle and releases the mod-created scene frame. |

reM's LS3DF `I3D_scene::AddFrame` at `0x10049290` increments the frame's
reference count and owns it in the scene's frame list. `I3D_frame::LinkTo`
only changes the parent relation; it does not remove the scene's list entry.
`C_car::GameDone` releases the vehicle's reference, but the scene still owns
one. Therefore a mid-mission car removal must call the audited
`I3D_scene::DeleteFrame` after the car has completed `GameDone` and destruction.
`DeleteFrame` removes the scene owner and releases that reference. A failed
car initialization must remove the added scene frame and release the creator
reference itself. `C_mission::Close` calls `C_game::Done` and `DelActors`
before `I3D_scene::Close`, so the scene remains valid during the car destructor
hook. The source cheat uses `thunderbird00.i3d` at script processing
`0x478673`/`0x478742`; this is the sample `/car` default and a known stock
model filename.

IDA shows `C_actor::Tick` at `0x4064f0` testing the byte at actor `+0x20`:
when true, it calls `NetDirect` instead of AI. reM names it
`m_bIsNetworkControlled`; a freshly constructed temporary remote actor starts
false and can be marked true after `GameInit`. The actor frame pointer at
`+0x68`, position at `+0x24`, direction at `+0x30`, and remove-frame flag at
`+0x6c` are checked against reM's offset assertions and IDA's frame/actor
call sites. `C_mission::Close` and `C_game::Done` may destroy these actors,
so the client service must resolve a generation-checked handle at each use.

### Temporary car physics and interaction

The exact Steam `Game.exe` car vtable at `0x623618` resolves virtual AI
(`+0x34`) to `C_car::AI`, virtual Update (`+0x38`) to `C_car::Update`, and
virtual NetDirect (`+0x3c`) to an empty return. These are native functions,
not mod hooks; no instruction bytes are changed. The client sets the typed
`NativeActor::SetNetworkControlled(false)` after registering its temporary
car, so the normal native AI/physics and Update calls continue. This choice
applies to both a simulation controller and remote replicas.

| Native path | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_actor::Tick` `0x4064f0` | `56 8B F1 8B 46 68 8B 88 10 01 00 00 83 F9 09 75 1D 8B 0D 8C 78 63 00 8B` | `void __thiscall(actor*, unsigned frameMs)`, `ret 4`. `C_game::Tick` calls it for every active primary actor, including temporary actors registered by `C_game::AddTemporaryActor`. The actor flag at `+0x20` chooses virtual `NetDirect` when true or virtual `AI` when false; it always calls virtual `Update` afterward. | The game owns the actor until `RemoveTemporaryActor` completes or the mission closes. The mod changes only its typed flag while its generation-checked handle resolves, and does not retain the pointer across destruction. |
| `C_car::NetDirect` `0x403db0` | `C2 04 00` | `void __thiscall(car*, unsigned frameMs)`, `ret 4`; car vtable slot `+0x3c` at `0x623654`. It does nothing. With the network-controlled flag true, the native vehicle physics tick is skipped. | No separate cleanup; this is a virtual call on a live car only. |
| `C_car::AI` `0x41f780` | `83 EC 08 53 55 56 8B F1 33 DB 57 38 9E 7D 21 00 00 0F 84 84 00 00 00 38` | `void __thiscall(car*, unsigned frameMs)`, `ret 4`; car vtable slot `+0x34` at `0x62364c`. It processes native brake/steer and collision state, then calls `C_Vehicle::Tick` at `0x41fa2a`, advancing the wheels and vehicle frame. | It executes through the game's actor tick while the car is active. The mod does not call AI itself or change its vtable. |
| `C_car::Update` `0x41fac0` | `64 A1 00 00 00 00 6A FF 68 28 DC 61 00 50 64 89 25 00 00 00 00 81 EC 8C` | `void __thiscall(car*, unsigned frameMs)`, `ret 4`; car vtable slot `+0x38` at `0x623650`. The native actor tick calls it after AI/NetDirect. Near `0x41ffxx`, it admits seat use objects only when the car is slow, enter protection is clear, and `IsCarUsable(C_CAR_USABLE_WHEEL_CONTACT)` passes. Camera distance gates visual and sound effects, not the actor's physics tick. | The game updates seat use objects each tick and tears them down in `GameDone`. No mod pointer to a seat use object is kept. |
| `C_car::IsCarUsable` `0x429850` | `53 8A 5C 24 08 55 56 F6 C3 01 0F 84 A0 00 00 00 D9 81 24 02 00 00 D8 1D` | `bool __thiscall(car*, unsigned mode)`, `ret 4`; called by `C_car::Update` and human vehicle interactions. For wheel-contact mode at speed below 1, it needs at least three contacting wheels. With NetDirect empty, the contact state may never be established for a freshly spawned car. | A read-only native check on a live car; the mod does not call or hook it. |

`I3D_frame::m_mWorldMat` at `+0x10` has three basis rows and translation
at row 3, as asserted in reM and audited above. The typed SDK
`NativeFrame::GetWorldBasis` calls the already audited virtual `Update` then
copies those rows. It is used only on a live frame owned by the loaded mission
to report the controller's native pose. The server validates the reporter,
mission generation, sequence and bounded displacement, then broadcasts its
server-owned car transform. Other clients correct that transform once per
new snapshot while their native car physics continues. On disconnect, the
server transfers the simulation controller to another mission-ready client;
mission reset destroys the replica and clears movement state. A script teleport
increments a reliable transform revision and sends the requested pose with that
revision. The controller snaps its existing native frame through the audited
frame/actor transform path, then calls `C_car::Reset(0, false)` so its
physics pose and dynamic collision follow the frame, before reporting another
pose. Without the reset the controller's physics wrote its old position back
to the frame on the next tick and the teleport was lost. Observers take the
same reset on any snap. Each movement RPC
carries the revision, so delayed physics reports from before the teleport
cannot revert it on the server; this also covers a state update arriving ahead
of the unreliable transform channel. The reset zeroes native velocities; the
script's requested linear velocity is applied afterwards.
Native deformation, impact damage, and wheel/steering inputs still need
separate authoritative capture and reconciliation.

### Native car engine state

The engine report reads a typed `NativeCar` field only while its
generation-checked native handle resolves. IDA shows `C_car::C_car` at
`0x41bba6` forming `car+0x70` and passing it to `C_Vehicle::C_Vehicle`;
`C_car::~C_car` at `0x41bcd1` forms the same subobject pointer. reM declares
`C_car : C_actor, C_Vehicle`; `C_actor` is `0x70` bytes. `SetEngineOn`
compares `m_bEngineOn` at vehicle `+0xc2c` at `0x4cb610` and writes it at
`0x4cb5d6`/`0x4cb5f1`, so the full car offset is `+0xc9c`. It writes
`m_bEngineRunning` at vehicle `+0x638` at `0x4cb5dd`/`0x4cb5f7`, so the full
car offset is `+0x6a8`; `EngineSnd` reads that member at `0x4ebb27`. reM
asserts both offsets relative to `C_Vehicle`. The SDK contains a typed
`NativeVehicle` subobject at car `+0x70` and checks all three offsets at
compile time. Feature code reads typed
`EngineRunning()`/`EngineOn()` and calls `SetEngineOn()` without accessing
a native offset.

| Native call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_Vehicle::SetEngineOn` `0x4cb5b0` | `8A 44 24 08 53 33 DB 56 3A C3 8B F1 74 46 8A 44 24 0C C7 86 20 0C 00 00` | `bool __thiscall(C_Vehicle*, bool on, bool silent)`, `ret 8` at `0x4cb687`/`0x4cb69d`. IDA callers include car AI `0x41f8bf`, `0x41fa21`, human vehicle interactions `0x5717b5`, `0x5730c7`, game/script paths, and vehicle initialization. Silent mode writes engine-on and engine-running bytes immediately and skips sound. Non-silent mode checks fuel/health and calls `EngineSnd` `0x4ebab0`; that starts/stops native audio and can defer the on transition until the start sound allows it. | Call only after temporary `C_car::GameInit`, while the mission, vehicle sounds and frame are live. Native `GameDone` owns sound cleanup. The mod has no hook here and retains no `C_Vehicle*` after a native handle invalidates; uninstall has no extra action. |
| `C_Vehicle::EngineSnd` `0x4ebab0` | `53 56 8B F1 57 B3 01 8A 86 32 04 00 00 84 C0 74 60 8B 86 4C 06 00 00 85` | `void __thiscall(C_Vehicle*, bool enable)`, called by `SetEngineOn` at `0x4cb664`. On start it marks `m_bEngineRunning` before `m_bEngineOn` becomes true, and starts the native ignition sound when present. On stop it turns sound off and clears both flags. | The mod does not call or hook it directly. It runs inside the audited `SetEngineOn` call while native sound handles belong to the live vehicle; `GameDone` releases them. |
| `C_Vehicle::SetLinearVelocity` `0x47b100` | `51 8B 54 24 08 8D 81 90 1F 00 00 56 57 8B 3A 8B F0 89 3E 8B 7A 04 89 7E` | `void __thiscall(C_Vehicle*, const S_vector*)`, `ret 4`. IDA shows a copy into `C_Vehicle+0x1f90`, speed magnitude stored at `+0x59c`, and a conditional world-update callback at `+0x308` using owner `+0xc74`; reM confirms these effects. IDA has a retail call at `0x4730bb`. | Call only on a live, initialized network car after `GameInit`, with a stack-owned finite velocity vector. It retains no vector pointer. Resolve the generation-bound car each frame, and do not call after native removal or mission close. No hook or extra unload step. |
| `C_Vehicle::SetSteer` `0x4cb4d0` | `D9 44 24 04 D8 1D 38 32 62 00 DF E0 25 00 41 00 00 0F 84 82 00 00 00 D9` | `bool __thiscall(C_Vehicle*, float normalizedAngle)`, `ret 4`. IDA callers include `C_car::AI` at `0x41f816`/`0x41f9ff`, vehicle/game and player control paths. It rejects values outside `[-1,1]` and writes normalized steering input to `+0x624` after applying native steering linearity when enabled; native vehicle tick advances the steering target and wheel visuals. | Call only on a live initialized remote car and with finite normalized input. No retained pointer and no hook; native AI/tick may overwrite input on the next frame, so the synchronized input is refreshed. |
| `C_Vehicle::SetGear` `0x4cb070` | `8B 54 24 04 56 83 FA FF` | `bool __thiscall(C_Vehicle*, int gear)`, `ret 4`. `C_player::AI_drive` calls it at `0x591d1e`/`0x591d53`. It writes the requested gear at +0x560; `MotorRot` (`0x4e19d0`) engages it into `m_iPreviousGear` (+0x55c) on the clutch step. It can refuse at the rev limit or with a destroyed gearbox. The mod calls it on observer cars when the controller's requested gear differs, retrying on the next frame. | Live initialized network car that the local player is not using. |
| `C_car::AI` `0x41f780` (hooked) | `83 EC 08 53 55 56 8B F1 33 DB 57 38 9E 7D 21 00 00` | `void __thiscall(C_car*, unsigned frameMs)`, `ret 4`; vtable slot +0x34 (C_car vtable 0x623618, entry 0x62364c). `C_actor::Tick` (`0x4064f0`, called from `C_game::Tick` at 0x5a5604) calls it, then `C_car::Update` (`0x41fac0`); `G_Camera::Tick` runs after the actor loop (0x5a5765) and render after `C_mission::Tick`. It runs `C_Vehicle::Tick` (`0x4df9a0`), the physics, at 0x41fa2a. For a car another client simulates, the detour skips it: it writes the interpolated frame pose, sets `m_vLinearVelocity`/`m_vAngularVelocity`, calls `UpdateImportantVariables` (`0x4cde90`, vehicle `this`), sets `m_uStateFlags` (+0x1a4) bit 0 so `C_Vehicle::Update` keeps running `DynUpdate`, sets steering input/target (+0x624/+0x628) and rpm (+0x548), then `DoWheelsCollision` (`0x4e6af0`, `S_vector* __thiscall(C_Vehicle*, S_vector* result, float dt, const S_vector* gravity, bool deform)`, `ret 0x10`, deform false as in `UpdateReplayPlayback` at 0x421d4b) and `EngineFreq` (`0x4ebe40`, `void __thiscall(C_Vehicle*, float)`, `ret 4`). This is the retail replay step without the playback flag. Camera, occupants and render then all read the same pose. Before `DoWheelsCollision` it sets or clears `VEHICLE_WHEEL_HAS_VISUAL | VEHICLE_WHEEL_SKIDDING` (0x1000010) in each wheel's flags (+0x120) from the controller's skid mask and zeroes `m_fSlipValue` (+0x108), exactly as `UpdateReplayPlayback` does at 0x421cd4/0x421cf5; the controller builds the mask as the retail recorder does, a bit per wheel that is skidding (0x10) while in contact (0x08). In the optional predicted mode (F9 on the client) the detour instead calls the original and then, still before `C_car::Update`, blends the frame pose and velocities toward the replicated pose extrapolated to the present, snapping with `C_car::Reset` beyond 5 m or about 45 degrees. | Only for live, active network cars this client does not simulate and has a sample for; everything else calls the original. An inactive car is not ticked, so the post-tick path places it instead. |
| Driver pedal fields | `SetPower` `0x4cb130`, `SetBrake` `0x4cb2d0`, `SetHandbrake` `0x4cb710`, `SetClutch` `0x4cb460` | `AI_drive` resolves the pedals into power +0x3a4/+0x3a8, brake target +0x5b8, handbrake output +0x434 (= time +0x3b8 / max time +0x3b4 × torque +0x3b0) and clutch +0x5e0. An automatic gearbox swaps gas and brake while reversing, so the controller reports these resolved values, with the handbrake as output/torque in [0,1]. `DoLights` (`0x4d7cd0`) rebuilds brake light 0x100 from +0x5b8 and reverse light 0x1000 from +0x55c every frame, so the replicated light bits alone are overwritten. `Tick`'s automatic gearbox runs when `m_bDirectControl` (+0x4cc) and a driver seat are set, so observers clear +0x4cc and follow the controller's gear. | Written each frame on observer cars only while the local player neither enters nor sits in them, since `Use_Actor` sets the driver's +0x4cc. On becoming controller the mod releases the replayed pedals. |
| `C_Vehicle::OpenDoor` `0x4cda20` | `8B 44 24 04 56 57 8B F1 8D 3C C0 8B 86 A4 0C 00 00` | `void __thiscall(C_Vehicle*, int seat, float fraction, bool instant)`, `ret 0xc`. Sets `S_DOOR::m_fTargetAngle` (+0x24) = fraction × `m_fMaxAngle` (+0x28) on `m_Seats[seat].m_pDoor` (seats vector at +0xca0, stride 0x24, door at +0x20) and flags it for `DoDoors`, which swings it at `m_fSpeed` with door sounds. Retail opens doors only from human entry/exit animation notifies. The controller reports each target; other clients call it with `instant` false. | Skipped for a seat whose door animation runs on this client: the local player's seat, or an owner whose `m_pUsedActorLeave` is the car. |
| `C_Vehicle::SetSpeedLimit` `0x4cb6a0` and `m_bSirenOn` +0x431 | `D9 44 24 04 D8 1D 30 32 62 00` | `SetSpeedLimit`: `bool __thiscall(C_Vehicle*, float)`, `ret 4`, writes +0x4a4 plus a margin; `Use_Actor` passes 16 or 1000 by the driver's limiter, and `MotorForce` caps torque by it. Observers follow the controller's limited flag (+0x4a4 below 800). `m_bSirenOn` has no setter or player control in retail; police AI writes it and `C_Vehicle::Update` passes it to `SirenSnd` (`0x4edb10`) while sounds are enabled. Siren lamps light from `VEHICLE_LIGHT_STATE_MASTER` 0x10000 (`EnableLights` `0x47b300`). The mod writes both from the server-owned siren. | Live network car. |
| `C_Vehicle::SetFuel` `0x4cb6e0` | `D9 44 24 04 D8 91 A0 1F 00 00 DF E0 25 00 41 00 00 75 08 DD D8 D9 81 A0` | `bool __thiscall(C_Vehicle*, float fuel)`, `ret 4`. IDA callers include `C_car::Update`, `CarExplosion`, game and save/load paths. It clamps fuel to the model-specific tank capacity at `+0x1fa0`, then stores it at `+0xc30`; it does not reject negative values. | Call only with finite non-negative authoritative fuel while the initialized vehicle is live. No retained pointer, hook, or extra unload action. |
| `C_Vehicle::SetAngularVelocity` `0x47b1b0` | `8B 54 24 04 56 8D 81 20 04 00 00 8B 32 89 30 8B 72 04 89 70 04 5E 8B 52` | `void __thiscall(C_Vehicle*, const S_vector*)`, `ret 4`. IDA copies the vector to `+0x420` and invokes the same conditional world-update callback at `+0x308` using owner `+0xc74`; a retail call is at `0x4730de`. reM confirms angular velocity is physics state. | Call only on a live initialized remote car with a stack-owned finite bounded vector. It retains no pointer. No hook or extra unload action. |
| `C_Vehicle::SetHorn` `0x4a9c10` | `8A 44 24 04 88 81 30 04 00 00 C2 04` | `void __thiscall(C_Vehicle*, bool)`, `ret 4`. IDA player control callers at `0x4a82f6`/`0x4a82ff`; it stores the horn state at `+0x430`, which `C_car::Update` later reads for native horn sound. | Call on a live initialized remote vehicle; its sound handles are owned by the car and cleaned by `GameDone`. No retained pointer or hook. |
| `C_Vehicle::EnableLights` `0x47b300` and inline light-state writer | `8A 44 24 04 84 C0 8B 81 1C 01 00 00 74 07 0D 00 00 01 00 EB 05 25 FF FF` | `void __thiscall(C_Vehicle*, bool)`, `ret 4`; IDA save/load callers at `0x473c02`/`0x473c31`/`0x473c49`. It writes `m_uLightStateFlags` at `+0x11c` and invokes the conditional world-update callback at `+0x308` with owner `+0xc74`. reM's inline `SetLightStateFlags` has the same write/callback effects. The SDK merges only headlights, indicators, brake/reverse active/check, and master bits so native blink phase and unrelated state remain owned by the game. | Apply after `GameInit` on the resolved remote car. The callback and owner are native-owned; no pointer is retained. No code patch or hook, and `GameDone` cleans native lights. |

reM `C_Vehicle_layout_asserts` and the corresponding IDA reads/writes prove
native dynamics fields relative to the embedded `C_Vehicle`: engine rotations
`+0x548`, gear `+0x560`, maximum gear `+0x568`, speed `+0x59c`, steering input
`+0x624`, target `+0x628`, fuel `+0xc30`, visual steering angle `+0xc64`,
linear velocity `+0x1f90`, and tank capacity `+0x1fa0`. Light flags are at
`+0x11c`, angular velocity at `+0x420`, and horn state at `+0x430`. These
fields are read through an SDK
view of the live subobject, not feature-level address arithmetic. Engine
rotations and gear are observed for replication; the current implementation
does not force those private drivetrain fields on remote cars because native
physics owns their transitions.

The owner sends the already mapped steering input at `+0x624`. The remote
client writes that field through the SDK's typed `ApplySteeringInput`; this
reproduces the only assignment made by retail `SetSteer` without running its
local-control steering-linearity mapping a second time. The native vehicle
tick still owns the smoothed target at `+0x628` and the visual wheel angle.
The field write is limited to finite, server-validated input on a live remote
car, and requires no unload action beyond native handle invalidation.

reM `I3D_frame::SetDir` constructs orientation from forward direction and a
separate roll angle. Car presentation derives roll around the replicated
forward axis from the sampled quaternion, then passes it to the already
audited LS3DF `SetDir` call. Native vehicle physics still advances wheel and
suspension state between snapshots.

The following retail paths were audited before deciding whether the owner can
report water/out-of-map outcomes and whether the remote client can force the
drivetrain. No new hook or native call is installed for these paths.

| Native path | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_Vehicle::Update` `0x4e0a70` | `83 EC 28 53 55 57 8B F9 8A 87 A8 1F 00 00 84 C0 74 0C 8A 87 A9 1F 00 00 88 87 2C 0C` | `int __thiscall(C_Vehicle*, unsigned frameMs)`, `ret 4`. `C_car::Update` calls it from the normal actor tick. It returns `VEHICLE_UPDATE_INVALID` when **vertical linear velocity** at vehicle `+0x1f94` is below `-85.0`; this is not a position or map boundary test. It otherwise advances sounds, wheel contact, drivetrain and vehicle physics. | The game calls it only while the car is active. A hook would require preserving its actor tick ordering and native destruction semantics; none is installed. |
| `C_car::Update` `0x41fac0` | `64 A1 00 00 00 00 6A FF 68 28 DC 61 00 50 64 89 25 00 00 00 00 81 EC 8C 00` | `void __thiscall(C_car*, unsigned frameMs)`, normal virtual actor update. If `C_Vehicle::Update` returns invalid, the car is not traffic, and replay is not active, it calls `SetActState(ACT_STATE_DEAD)`. It also applies occupant harm for dangerous wheel surface materials and handles local horn/engine sounds. | Mission actor tick owns the call; `GameDone` and native destruction clean up. The mod does not call or hook this method. |
| `C_car::Collision_Filter_Body` `0x426340` | `83 EC 24 33 C0 53 55 8B EA 56 57 8B F1 8A 45 03 83 F8 29 0F 85 CC 01 00 00` | `const g_collision_header* __fastcall(C_car*, edx collision, position, direction)` in reM. The body collision callback sets `m_bCollisionProcessed` for both surface material 31 (water/impact effect) and material 40 (player-fall volume). The retail `CAR_INWATER` script command reads this same flag, so it does not uniquely identify submersion. The material 31 path can invoke local player-fall behavior and damage occupants. | Collision engine owns the callback while the car and scene live. No pointer from it is retained and no hook is installed. |
| `C_Vehicle::SetGear` `0x4cb070` | `8B 54 24 04 56 83 FA FF 0F 8C A4 00 00 00 3B 91 68 05 00 00 0F 8F 98 00 00 00 39 91` | `bool __thiscall(C_Vehicle*, int gear)`, `ret 4`; called by retail automatic/manual transmission and replay paths. It can reject an out-of-range gear, reverse while in full-control drive mode, damaged gearbox, or a shift that would exceed redline. A successful change resets shift timing and changes clutch flags before writing the gear. | Requires an initialized live drivetrain and model ratios. It has no retained argument, but repeatedly forcing it on a remote physics simulation would fight local shift decisions; the mod currently observes gear only. |

reM `C_Vehicle::UpdateSteeringWheels` `0x4ddc60` computes wheel steering
from mapped input, speed and per-wheel geometry. `UpdateRotationWheels`
`0x4de020` integrates contact angle from wheel displacement, surface contact,
current gear/engine rotations, wheel damage and deformation, then writes each
wheel frame. Applying a single remote RPM or gear value would not reproduce
these per-wheel inputs and could perturb the car's native physics or shift
audio. Remote clients therefore run native wheel ticks with the replicated
steering and velocities; owner gear/RPM remain telemetry pending a validated
per-wheel state protocol. Neither a `CAR_INWATER` result nor the vertical
velocity invalid check proves an out-of-map terminal cause. Water and fall
volume outcomes now come from the native body collision callback's material
ID; the separate falling velocity check reports retail's invalid-fall outcome.

The simulation controller reports observed native ignition intent through
a reliable ordered RPC. The server checks that the sender is the assigned
controller for the current mission and accepts only newer report sequences,
then changes its replicated `engineOn` and revision. A server script may also
change that revision; the simulation controller applies a different requested
state, while a matching revision acknowledges its own report. A remote client calls
the non-silent native method once for each revision to play the retail
start/stop effect. A late join initializes the final state silently. If
native start sound defers a change for two seconds, the client applies the
authoritative final state silently so engine state converges. The local
controller's engine is not overwritten by a delayed replica field update;
controller transfer first applies the server's state silently.

### Vehicle damage investigation and explosion contract

The following retail paths are mapped for later damage replication. Only the
`CarExplosion` hook described below is currently installed. The current `CarEntity` damage and
terminal fields are server script state only; the client car service does
not yet apply generic damage fields to a native vehicle. The `Exploded`
terminal state is applied after server approval. This audit does not establish a
complete authoritative damage protocol.

| Retail path | Original bytes | Callers, side effects and lifetime evidence |
| --- | --- | --- |
| `C_car::Hit` `0x423600` | `8B 44 24 04 81 EC B0 00 00 00 83 F8 03 53 55 56 57 8B F1 0F 87 C6 10 00` | The actor hit virtual path handles projectile, deformation, explosion and fire types. reM shows it can crack/detach individual deform zones, break tires and wheels, damage fuel tank, alter engine/gearbox health and overall car damage, and apply impulses. IDA shows calls to `Deform`, `DeformGlass` and both detached-part helpers. It may run during ordinary native combat while the car and its frame are alive. Its complete virtual slot/calling convention and all external callers still require proof before a hook. |
| `C_Vehicle::DoCollision` `0x4cbd70` | `83 EC 24 53 55 8B 6C 24 38 56 8B F1 8B 44 24 40 D9 45 00 D8` | IDA caller `0x425040`; reM shows it updates velocity and calls `Deform` and `DamageVehicle` for a physical impact. This runs inside the live vehicle physics tick; no damage state is safe to read after vehicle destruction. |
| `C_Vehicle::Deform` `0x4d5610` | `81 EC 60 01 00 00 53 55 8B 9C 24 6C 01 00 00 8B E9 8B 8C 24` | IDA callers include `C_car::Hit` and `C_Vehicle::DoCollision`. It mutates mesh vertices and per-zone crack levels/flags, damages lights and wheels, and invokes a car callback that may detach parts. reM's `m_DeformZones` are dynamic model-specific entries, so a single `detachedParts` mask cannot reconstruct arbitrary deformation. Mesh and zone resources belong to the live model and are released by native vehicle teardown. |
| `C_Vehicle::DamageVehicle` `0x4d6720` | `D9 44 24 04 D8 99 C4 01 00 00 DF E0 25 00 41 00 00 75 7D 8B` | Called by collision and vehicle damage paths. It changes engine and gearbox health and damage flags; it is not a complete car health update. The damage flags include per-frame `VEHICLE_DAMAGE_APPLIED`, cleared in vehicle tick, so copying that bitset as a persistent terminal state would be wrong. |
| `C_car::Prepare_DropOut_Wheel` `0x426dd0`; `C_car::Prepare_DropOut` `0x426ec0` | Wheel: `83 EC 2C 8B 81 A8 0C 00 00 56 57 8B 34 90 C7 44 24 1C 00 00`; zone: `83 EC 24 8D 04 52 56 8B B1 88 02 00 00 57 8D 04 82 8D 34 86` | reM marks both `__fastcall` and IDA finds calls from `C_car::Hit` and `CarExplosion`. They call `Drop_Out` `0x427010`, which creates and registers a separate temporary debris actor, then hide the original wheel/part frame and mark it destroyed/broken. The debris actor has its own native lifetime; its pointer cannot stand in for the network car. |
| `C_car::CarExplosion` `0x421d60` | `6A FF 68 5B DC 61 00 64 A1 00 00 00 00 50 64 89 25 00 00 00 00` | `void __thiscall(C_car*, unsigned int flags)`, `ret 4` at `0x422acb`; IDA call sites are `C_car::Update` after the burn timer and the script process path. The body accesses `C_car` fields at `+0x20f0`/`+0x224` and calls `C_Vehicle::SetEngineOn` using the embedded vehicle pointer. It calls game explosion/audio/debris functions, may create a new temporary wreckage `C_car`, ejects occupants, hides and deactivates the original, marks its health zero and engine off. The original car can remain pending deactivation while the wreckage has a distinct native identity. | The actor, mission, model and game must be live; call only from a game tick after the replicated terminal decision. The car destructor hook invalidates its handle before native destruction. Hook is removed before mission/system teardown; the mod retains neither the actor nor the temporary wreckage pointer after the call. |

The owner-only damage hooks use the same exact Steam build and are installed
after Framework/world initialization, then disabled and removed before client
shutdown. They resolve the native car through its generation-bound registry
handle on each invocation and never retain a car or mesh pointer. Calls on
ordinary game cars still run the original native function.

| Hook target | Original bytes, convention and retail callers | Side effects and unload |
| --- | --- | --- |
| `C_Vehicle::Deform` `0x4d5610` | `81 EC 60 01 00 00 53 55 8B 9C 24 6C 01 00 00 8B E9 8B 8C 24 7C 01 00 00`; `void __thiscall(vehicle*, const S_vector* position, const S_vector* direction, float radius, float force, unsigned flags, const S_vector* callbackDirection)`, `ret 0x18`. IDA callers: `C_car::Hit` `0x424448`, `C_Vehicle::DoCollision` `0x4cbf76`, and vehicle simulation paths `0x4e827d`, `0x4e9c79`, `0x4ea1c9`. | Mutates model-specific vertex buffers, zone crack levels/flags, wheel/light states, may invoke detach callbacks, and dirties native mesh/physics state. The hook suppresses this mutation only for a live network car simulated by another client; the simulation controller runs the original and marks its mesh checkpoint dirty after return. No native memory is retained across the call. |
| `C_Vehicle::DamageVehicle` `0x4d6720` | `D9 44 24 04 D8 99 C4 01 00 00 DF E0 25 00 41 00 00 75 7D`; `void __thiscall(vehicle*, float collisionDamage)`, `ret 4`. IDA callers: `C_Vehicle::DoCollision` `0x4cbf89` and vehicle simulation paths `0x4e8290`, `0x4e9c8e`, `0x4ea1de`. | Changes engine/gearbox health, destroyed state and transient damage flags. The hook suppresses this mutation for a live network car simulated elsewhere; owner and ordinary cars run the original. Native game cleanup owns these fields; no extra unload work is needed after removing the hook. |
| `C_car::Hit` `0x423600` | `8B 44 24 04 81 EC B0 00 00 00 83 F8 03 53 55 56 57 8B F1`; `bool __thiscall(car*, int hitType, const S_vector* direction, const S_vector* position, const S_vector* normal, float damage, C_actor* attacker, unsigned flags, I3D_frame* frame)`, `ret 0x20` at `0x42471c`, vtable `0x623618+0x7c`. The retail `C_game::ProcessShootRecord` calls this virtual slot with normalized trajectory, impact position and normal; car AI, explosions, script and vehicle collision paths can also invoke `Hit`. | Projectile hits can forward to a child actor, crack/detach zones, damage lights/wheels/engine/body, create debris and activate the car. Returning false causes the retail shoot loop to retry the dynamic collision. The hook consumes car body projectiles on tracked cars and forwards a local shooter's pellet to the server; only the current car controller calls the retail original after server acceptance. Calls on a child actor frame continue through retail so the protected human hit hook can report occupant damage. The server checks the accepted shot and recent car pose; the controller transforms the car-local impact to its current pose before calling this function. No vector, frame or actor pointer survives the hook. Disable and remove the hook before game teardown. |

IDA `C_car::Hit` projectile branch at `0x4236a8` follows frame parent
links at `+0x120`, reads the actor pointer from the frame internal buffer at
`+0x08`, and stops at a model frame type `9` at `+0x110`. It forwards hits on
an actor-owned child frame through that actor's virtual `Hit` at `0x423717`.
reM `I3D_frame` asserts those three offsets; the SDK's typed `NativeFrame`
view exposes them only while the frame is live. The frame-null fallback
rerays from `position - direction` over `direction` at `0x42364d`–`0x423694`.
Delayed replay therefore uses a validated car-local impact and transforms it
to the controller's current world frame. That fallback ray is exactly one
unit long and ends at the impact, so rounding in the car-local round trip can
stop it short of the surface and `Hit` then applies no damage. The controller
instead casts its own ray from half a unit before the impact through the
surface against the same root, and passes the resulting frame to `Hit`, as
`C_game::TickShoot` does for a live bullet. This cannot prove the same child
frame is selected in every model after substantial deformation.

| Native call | Evidence and convention | Ownership and lifetime |
| --- | --- | --- |
| `I3D_scene::TestColHierarchy`, scene vtable `+0xd0` | Retail `C_car::Hit` loads `g_pScene` from `0x63d9d4` and calls `[vtable+0xd0]` at `0x42368a` with `(scene, const S_vector* start, const S_vector* direction, I3D_frame* root, I3D_COLLISION*, 0, nullptr, 0, nullptr)`; the root is `C_Vehicle::m_pFrame` read from `car+0x3bc` at `0x423676`. reM `I3D_scene.h` declares the same argument order and a `0x1c` byte `I3D_COLLISION` (distance, normal, three property words, hit id). Returns the nearest hit frame in the subtree or null. | Read-only query on the live mission scene; the collision record is caller-owned stack memory. The returned frame is borrowed and is only passed straight to `C_car::Hit` in the same tick. |
| `C_Vehicle::m_pFrame` `vehicle+0x34c` | reM asserts `C_Vehicle::m_pFrame` at `0x34c`; `C_car` embeds `C_Vehicle` at `+0x70`, which gives the `car+0x3bc` read above. | Owned by the car model; valid while the car is alive. |
| `C_car::Reset` `0x422be0` | `D9 44 24 04 D8 1D 30 32 62 00`; `void __thiscall(C_car*, float speed, bool full)`, callee cleans 8 bytes; callers push the bool then the float. The fuel pump at `0x480440`–`0x480452` calls frame `SetDir`, frame `Update` and then `Reset(0, false)`; script opcodes and garages call `Reset(0, true)`. reM `C_Vehicle::Reset` `0x4c3860` rebuilds `m_mWorld`, its inverse, basis, position, world centre and sector from the frame, zeroes linear/angular velocity, gear, steering and horn, marks physics active and turns the engine off; `full` also reinitializes damage and deformation. It also clears each wheel's transient and damaged flags. | Called with `full = false` only, after a pose snap, on a live registered car during the game tick. The caller restores the replicated wheel damage flags and health, engine, horn and linear velocity afterwards. No pointer is retained. |
| Vehicle physics pose `vehicle+0x320/+0x398/+0x3f8/+0xc3c..+0xc60/+0xce8..+0xde8`, body/extra collision `+0x22c/+0x274`, flags `+0x1faa..+0x1fac` | reM asserts `m_vPosition 0x320`, `m_vWorldCenter 0x398`, `m_vMassCenter 0x3f8`, `m_vForward/Right/Up 0xc3c/0xc48/0xc54`, `m_mWorld/m_mInverseWorld/m_mPreviousWorld/m_mPreviousInverseWorld 0xce8/0xd28/0xd68/0xda8`, `m_BodyCollision 0x22c`, `m_ExtraCollision 0x274`, `m_bSpecialDynamicCollisionMode/m_bBodyDynamicCollisionEnabled/m_bExtraDynamicCollisionEnabled 0x1faa/0x1fab/0x1fac`; `tDynamicCollObject` is `0x48` bytes with `m_vPosition` at `+0x10`. The physics tick at `0x4e0d1e`–`0x4e0d46` copies `vehicle+0x320` into `vehicle+0x23c` and calls `g_collision::DynUpdate` `0x5c3ac0` as `__thiscall(0x647f48, object*)`. | An observer writes the frame's world matrix into the physics pose and refreshes the body and extra collision after each replicated pose, mirroring the pose part of `C_Vehicle::Reset` without touching velocities, wheels or engine. An active vehicle otherwise writes its own physics position back to the frame each tick, and bullets test the stale collision. Special per-cell mode, unused by retail game code, is left to the native tick. |
| `C_Vehicle::SwitchSpecialProjectorTexture` `0x4d6580` | `bool __thiscall(vehicle*, I3D_frame* projector, unsigned short mask)`, `ret 8` at `0x4d6662`. Retail `Deform` calls it at `0x4d579d`–`0x4d57c6` for a light with `OWNS_PROJECTOR` (`0x80000000`), passing the light's projector at `+0x08` and mask 1 when the light frame's local x (`frame+0x80`) is negative, else 2. The body only narrows the projector state and swaps its texture, so repeated calls are idempotent. | Called once when a replicated light becomes destroyed. The projector is owned by the vehicle. |
| Wheel `+0xb4` surface material, `+0x188` deform angle | reM asserts `S_wheel::m_iSurfaceMaterial 0xb4`; `C_car::Update` at `0x4216ca` compares it with 40 (fall volume) and 31 (water) before its lethal occupant hits. `InitDamage` at `0x4d5520` writes `m_fCurrentDeformAngle` at `+0x188` beside health `+0x18c`. | Read on the controller to report a terminal outcome; the deform angle is replicated so observers show bent wheels. |
| Engine power/destroyed `vehicle+0x1ac/+0x1b0/+0x1b8/+0x5f0`, burn state `car+0x20e8/+0x20ec/+0x20f0` | reM field order around asserted `m_fEngineHealth 0x1b4` and `m_bEngineDestroyed 0x5f0`; `C_car::m_bBurning` asserted at `0x20f0` after the burn timer and duration. Retail save state stores the current engine power directly. A silent `SetEngineOn` clears `m_bEngineDestroyed`, so the SDK wrapper preserves it. | Replicated in the damage snapshot. A new controller inherits the burn countdown, so a burning car still explodes through the server-owned explosion path. |

For a network car, the explosion hook suppresses the local call and the
simulation controller sends one reliable intent. `C_car::Update` keeps
requesting an explosion each tick after its burn timer until it becomes dead,
so the per-car client stream deduplicates these requests. The server verifies
mission generation, controller GUID and active terminal state, commits the
`Exploded` state and lethal damage to recorded occupants, then clears seats.
Clients call the original exactly once when they observe the new terminal
sequence. This includes the controller, so it does not receive two explosions.
A late join creates its native car first and then applies the terminal state;
native wreckage creation remains game-owned. Native explosion side effects
such as debris are local presentation; any surviving wreckage actor currently
lasts until native mission cleanup.

`PROGRAM_CMD_CAR_INWATER` in reM reads `C_car::m_bCollisionProcessed`
at `+0x2219`. That byte is set by `C_car::Collision_Filter_Body`
`0x426340` for at least material IDs 31 and 40, and it is reset during
activation. The evidence does not establish a unique submerged terminal
state. Neither `C_car::Update` nor `C_Vehicle::Tick` has a proven generic
out-of-map terminal check in the inspected source. The multiplayer report
reads the native body callback's material ID (31 for water, 40 for a fall
volume); it does not use this bit as a water detector or infer a generic map
boundary. The old aggregate
bitmask fields do not represent model-specific light, zone, wheel or vertex
damage. Native part snapshots and bounded vertex checkpoints now carry those
values, while explosion debris and terminal lifecycle still need verification.

### Per-part native damage state and save format

The exact LS3DF.dll mesh methods used by retail vehicle save/load were
checked against the DLL's vtables before adding typed SDK wrappers:

| Native path | Original DLL bytes and vtable slot | Calling convention, effects, lifetime |
| --- | --- | --- |
| `I3D_mesh_object::GetLOD` `0x100313f0` | `8B 44 24 04 8B 4C 24 08 8B 44 88 0C C2 08 00`, mesh-object vtable `0x1009c030+0x04` | `I3D_mesh_level* __stdcall(mesh*, int)`. It has no index check; SDK checks `0 <= lod < lodCount <= 10` first. Returned level is mesh-owned and must not outlive the live model. |
| `I3D_mesh_level::LockVertices` `0x10030e80` | `8B 44 24 04 8B 48 10 85 C9 75 05 33 C0 C2 08 00`, mesh-level vtable `0x1009bffc+0x04` | `I3D_vertex_mesh* __stdcall(level*, unsigned flags)`. Retail vehicle save/load passes flags 0. It locks the pooled D3D8 vertex buffer, may return null if the buffer is absent, and returns a borrowed 32-byte vertex array. The buffer must be unlocked on the same game thread before mission/model teardown. |
| `I3D_mesh_level::UnlockVertices` `0x10030f90` | `8B 44 24 04 8B 48 10 85 C9 56 74 57 8B 70 0C`, mesh-level vtable `+0x08` | `void __stdcall(level*)`. It releases the pooled D3D buffer lock; no vertex pointer remains valid afterward. |
| `I3D_object::UpdateVertices(int)` `0x10036a50` | `8B 44 24 04 8B 4C 24 08 8B 84 88 E4 01 00 00`, object-frame vtable `0x1009c348+0x78` | `void __stdcall(object*, int lod)`. Marks that LOD's render cache dirty. A deform-zone frame is an `I3D_object*` in reM; call only on that live frame and a checked LOD. |
| `I3D_mesh_level::UpdateBBox` `0x10030a50` | `56 8B 74 24 08 8B 06 6A 00 56 FF 50 04`, mesh-level vtable `+0x28` | `void __stdcall(level*)`. It scans the current positions and locks the vertex buffer without unlocking it, so the SDK must unlock after this call. |
| `I3D_mesh_object::UpdateBoundVolume` `0x10031400` | `8B 4C 24 04 B8 CA 1B 0E 5A 89 41 3C`, mesh-object vtable `+0x14` | `void __stdcall(mesh*)`. Rebuilds the mesh volume from LOD boxes. Use after updating the level box, while both mesh and frame are live. |

reM's vehicle save encodes each displaced vertex as a 4-byte word:
`word >> 18` is a vertex index, and signed 6-bit X/Y/Z components in bits
12/6/0 are multiplied by `0.02` and added to that vertex's original mesh
position. Retail restores every original position before applying a saved
checkpoint; otherwise vertices omitted from a later checkpoint retain stale
deformation. The SDK validates LOD/count/index/order before writing any
position, uses the retail zero-flag lock sequence, then unlocks, dirties the
render cache, updates bounding volumes, and releases the box recomputation
lock. All pointers stay within the live native car's generation-bound handle;
no hook is installed and unloading retains no mesh pointers.

For a later authoritative repair, reM `C_Vehicle::InitDeform(false)` restores
broken deform-zone frames with `SetOn(true)` after resetting their mesh, and
vehicle wheel initialization restores wheel frames with `SetOn(true)`. The
SDK mirrors that visible frame transition when replicated broken/destroyed
flags clear. Full wheel physics repair also resets suspension/contact and
other transient fields in retail initialization; a frame toggle alone does
not prove that parity, so native repair still needs multiplayer testing.

The exact retail car has a `C_Vehicle` subobject at car `+0x70`. Its
model-specific light vector begins at vehicle `+0x100`, deform-zone vectors
have objects at `+0x204` (original) and `+0x214` (current), with their begin
pointers at `+0x208/+0x218`; wheel count is at `+0x4c4` and wheel
pointer array at `+0xc38`. reM declares a 32-byte light record with flags
`+0x14` and damage `+0x18`, a 52-byte deform zone with type `+0x0c`, flags
`+0x0e` and crack level `+0x20`, and a 468-byte wheel with flags `+0x120`
and health `+0x18c`. The current vehicle's engine health is at `+0x1b4`,
gearbox health at `+0x1ec`; the car's body damage and maximum are at
`+0x20c4/+0x20bc`. IDA `C_car::Hit` reads the deform vector and wheel array,
scales zone indexes by `0x34`, wheel indexes through the pointer array,
subtracts individual zone/wheel health, and calls native glass and dropout
helpers. `C_Vehicle::DamageVehicle` at `0x4d6720` reads/writes engine
health at `+0x1b4` and gearbox health at `+0x1ec`; it sets transient damage
bits at `+0xf8`. A replicated aggregate flag cannot express these values.

The native save code provides a precise damage checkpoint format, but its
whole-state loader is unsafe to use as a live network applier without a
separate parser and model-specific validation:

| Retail path | Original bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_Vehicle::GetVehicleStateSize` `0x4ceb30` | `83 EC 20 53 8B D9 56 57 8B 8B 00 01 00 00 85 C9 75 04 33 C0 EB 0B` | `int __thiscall(C_Vehicle*)`, plain return. `SaveVehicleState` at `0x4cee12` calls it; reM counts model-specific lights, deform zones, both mesh LODs' displaced vertices, seats, and wheels. | Reads live model meshes and locks their vertices before unlocking. The vehicle and model must remain initialized throughout the call; do not retain mesh pointers after game/mission teardown. No hook is installed. |
| `C_Vehicle::SaveVehicleState` `0x4cee00` | `83 EC 38 53 56 57 8B 7C 24 48 8B F1 C7 07 09 00 00 00 E8 19 FD FF FF` | `bool __thiscall(C_Vehicle*, void* output)`, `ret 4`. Retail `C_car::SaveGameSave` at `0x42bc00` calls it. It writes version 9 and a packed `0x171` byte header, then 12-byte light records, 18-byte deform-zone headers with variable 4-byte displaced vertices, 18-byte seat records, and 100-byte wheel records. It includes individual crack levels, broken flags, wheel health, broken tires, detached flags, light damage, and actual mesh displacement. | Caller owns an output buffer sized by `GetVehicleStateSize`; the native method retains none of it. Model LOD vertex locks are released before return. No hook is installed. |
| `C_Vehicle::LoadVehicleState` `0x4cfab0` | `81 EC 80 00 00 00 53 8B 9C 24 88 00 00 00 55 8B E9 83 3B 09 74 0D` | `bool __thiscall(C_Vehicle*, const void* input)`, `ret 4`; retail `C_car::SaveGameLoad` calls at `0x42be1a`. It checks only version 9, deactivates the vehicle, resets native physics, overwrites pose, engine, seats, doors and runtime flags, then applies per-light/deform/mesh/wheel data. reM shows no input-buffer-length validation and no displaced-vertex-index bounds check before writing mesh vertices. | Input must be a fully validated, model-matched native save buffer while the car and all meshes are live. Never pass bytes received over the network directly. A whole-state load during occupancy would replace seat/physics state; the mod adds no call or hook until a damage-only application path is proven. |

`C_car::Hit` at `0x423600` is vtable slot `+0x7c`: IDA's car vtable
at `0x623618` contains `00 36 42 00` at `0x623694`. It is
`bool __thiscall(C_car*, E_hit_type, const S_vector& direction,
const S_vector& position, const S_vector& normal, float damage,
C_actor* attacker, unsigned int flags, I3D_frame* frame)`, with the result in
`AL` and a stack cleanup of 32 bytes. All three vector arguments are borrowed
from the caller. Retail bullet collision uses the virtual slot; no direct code
xref points to its entry. The function can spawn separate temporary debris,
break glass, apply impulses, start fuel burning, and mutate per-part health.
The installed hook is removed before game shutdown and retains no caller
vector, frame, attacker, or debris pointer. The server validates an accepted
Fire pellet and the car's recent pose before forwarding the hit to the current
controller. Projectile replay on that controller calls the original through
the MinHook trampoline; non-projectile hits still run locally only on that
controller and require separate authority work.

## Native combat inventory and in-car shooting

The following contract was checked against the exact Steam `Game.exe` with
SHA-256 `303eb95ee2de3433511ce0cb518921dcb96b62b0f64ebfcc436723ff5083f298`
in IDA, using reM only as read-only supporting evidence. A native human is
game-owned from `C_human::GameInit` through `GameDone` and the actor deleting
destructor. `GameInit` creates weapon model frames, weapon sounds and muzzle
joints, resets `G_Inventory`, and initially calls `ChangeWeaponModel`.
`GameDone` releases those resources. Network code may apply a selected item
only after `GameInit`, and may not retain the human or its weapon frames after
stream out, mission close, or native actor destruction.
IDA's `G_Inventory::GetAmmo` at `0x609d20` reads selected item ID at inventory
`+0x20`, loaded ammunition at `+0x24`, and reserve at `+0x28`; `Shooting`
calls it before firing. This independently confirms the typed inventory
view used below.

| Retail item | Original bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_human::ChangeWeaponModel` `0x57ec20` | `83 EC 14 53 55 56 8B F1 57 C6 86 30 02 00 00 00 00 E8 2B F3 00 00` | `void __thiscall(C_human*)`, no stack arguments. IDA callers include game initialization `0x574bd0`, normal equip/weapon paths, and `C_human::Shooting` `0x584620`. It reads selected item ID at human `+0x4a0` (IDA `0x57ee98`), rebuilds the game-owned hand weapon model and muzzle, updates sounds, item type and the local ammo display. reM confirms the selected `S_GameItem` lives in `G_Inventory` at human `+0x480`, inventory `+0x20`, with 16-bit item ID, 32-bit loaded/hidden ammunition at `+4/+8`, and a borrowed using-object pointer at `+12`. | Call only for a registered, initialized native human on the game thread when the selected item changes. GameDone owns and releases every loaded weapon frame; the mod stores no frame pointer. No hook is needed at this entry. |
| `G_IndicatorsClass::PlayerSetAmmo` `0x5f8910` | `8B 44 24 04 8B 54 24 08 89 81 14 42 00 00 89 91 18 42 00 00 52 50 81 C1`; IDA callers include `C_human::ChangeWeaponModel` `0x57ed5a`, `C_player::AI` `0x59143f`, car fire `0x593513`, and UI initialization `0x5f6d4f`. reM identifies the HUD singleton `g_pIndicators` at `0x6bf980`. | `void __thiscall(G_IndicatorsClass*, unsigned loaded, unsigned reserve)`, `ret 8`. IDA stores both values at singleton `+0x4214/+0x4218` and formats its cached display text as `%d/%d`. It changes only HUD data, with no weapon model or animation side effect. | The UI singleton exists while the game UI is initialized; call on the game thread only for the current player after a successful server inventory reconciliation. No hook or native pointer is retained; nothing to unload. |
| `C_human::Shooting` `0x584620` | `81 EC E8 00 00 00 53 55 56 8B F1 57 8B 86 F4 01 00 00 8B 8E F0 01 00 00` | `void __thiscall(C_human*)`, no stack arguments. IDA callers include `Do_Shoot` `0x583590`, human AI `0x572b80`, and player AI `0x58f4b0`. In the car branch it verifies weapon ammunition and muzzle, a car in `m_pUsedActorEnter`, valid seat, and settled vehicle animation. It calls `C_game::NewShoot` at `0x584bf5`, then increments the human shot count at `+0x9ac` (`0x584bfa`–`0x584c06`) and spends one native round. The on-foot branch likewise increments that field at `0x5857d1`–`0x5857d7`. reM confirms these conditions and side effects. | A hook may call the original exactly once, compare shot count before/after, and submit only actual local shots. The game owns the human and shot lifetime. Disable and remove the hook before `CloseSystem`; reset combat bindings on stream out/disconnect/mission change. |
| `C_game::NewShoot` `0x5a84a0` | `81 EC D4 00 00 00 D9 84 24 E8 00 00 00 D8 8C 24 E8 00 00 00 D9 84 24 EC 00 00 00` | `bool __thiscall(C_game*, C_actor*, S_vector position, S_vector direction, float damage, int particleEffectId, I3D_frame*, int shotCount)`, `ret 2ch`; IDA call at `0x584bf5` copies both 12-byte vectors by value and supplies the actor plus four trailing arguments. Other callers are game shoot paths at `0x5672b1` and `0x5a72a9`. It queues collision-tested bullet records, particle effects, sound and traffic panic. reM `C_game::NewShoot` confirms. | On a local shot the hook calls the original once and copies the native input plus each queued pellet result. During a server accepted remote replay it calls the original once using transmitted origin/direction and scatter draws, preserving native `TickShoot` collision, muzzle effects and sound while passing zero direct damage; authoritative server damage arrives separately. The hook rejects and removes newly appended remote records if count or collision result diverges. Other calls delegate unchanged. Remove before `CloseSystem`; no game pointer is retained. |
| Game `rnd()` `0x408470` | `51 E8 1B 83 20 00 89 44 24 00 DB 44 24 00 D8 0D 04 33 62 00 59 C3` | `float __cdecl()`, no arguments, float in x87 `ST0`, plain `ret`. IDA body calls the CRT `rand`, converts the result to float, and multiplies by `0x623304` constant. IDA callers include `C_human::Shooting` (`0x584989`, `0x58499e`) and `C_game::NewShoot` scatter loop (`0x5a8aac`, `0x5a8abd`), plus many unrelated game systems. reM `Game_math.cpp` confirms. It advances process RNG state on every call. | A thread-local hook may capture only the `2 * shotCount` values consumed by `NewShoot` scatter while the original local shot is being queued. Remote replay first invokes the original to preserve local RNG advancement, then substitutes the transmitted scatter value for those calls inside a scoped `NewShoot`. All other calls use the original. Remove the hook before game shutdown; no RNG or game object pointer is retained. |
| `C_game::m_ShootRecords` at `+0x1d4` | `C_game::NewShoot` at `0x5a8aa4` passes `this+0x1d4` to the VC6 vector insert. IDA insert `0x5bbe30` reads allocator `+0`, begin `+4`, end `+8`, capacity `+12`, advances end by `0x5c`, and reassigns all three pointers on growth; `0x5a8dad` reads `this+0x1dc` (vector end). reM `C_game.h` asserts the vector offset and declares each 0x5c-byte `S_shoot`: start `+0x00`, clipped trajectory `+0x0c`, full range `+0x18`, current position `+0x1c`, damage `+0x28`, actor `+0x2c`, frame `+0x30`, static collision `+0x34`, static hit position `+0x38`, normal `+0x44`, hit flag `+0x50`, impact sound `+0x54`, material `+0x58`. | Copy only newly appended local record trajectories immediately after `NewShoot`. On remote replay, compare newly appended native records with the accepted trajectories within the same hook call; if draw count, record count, or a trajectory diverges, move the POD vector end pointer back to its pre-call position so `TickShoot` cannot advance a different projectile. The game owns vector allocation, collision pointers, and ordinary removal during `TickShoot` or mission shutdown. Never retain a vector element or transmit pointers. |

The stock Steam `aa.dta` contains `tables\\predmety.def`, a 6580-byte table
of 35 fixed 0xbc-byte `S_item` records (research extract SHA-256
`29cba6ee58f1a5ddeef7e33a685fab69eed109fdae7be2b7906cb2e9f8092ca5`).
reM's `S_item` layout places flags at `+0x20`, magazine capacity at `+0x60`,
range at `+0x74`, and damage at `+0x78`. The nine firearm records 6–14 have
capacity/range/damage respectively: `6/30/30`, `6/100/75`, `6/70/40`,
`7/90/55`, `50/120/50`, `8/70/250`, `2/40/250`, `5/400/80`, and
`5/1500/100`. Only records 11 and 12 carry the shot-type-10 flag. This
table is evidence for server rules; the game still loads its own copy.

reM `C_game::NewShoot` divides item damage by pellet count when queuing a
multi-pellet shot. IDA `C_game::TickShoot` at `0x5a8e70` (original bytes
`6A FF 68 AB 0B 62 00 64 A1 00 00 00 00 50 64 89 25 00 00 00 00`)
confirms the later range falloff. For a dynamic hit before the full shot range,
damage is unchanged through
roughly one third of that range and then multiplied by
`1 - (distance - range * 0.33000001) / (range * 0.66000003)`; at or beyond
full range it passes zero damage. IDA `C_human::Hit` at `0x5762a0`
(original bytes `8B 44 24 04 83 EC 34 53 55 33 DB 56 57 8B 7C 24 5C`)
multiplies generic hits to hands by `0.30000001`, legs by `0.5`, and head by
`1.5`. Its special shotgun branch forces a fatal result within three units
of a victim on foot. `C_human::HitInCar` at `0x578290` (original bytes
`8B 44 24 04 56 85 C0 57 8B F1 74 05 83 F8 05 75`) receives generic
hits before that branch and subtracts unscaled damage regardless of body
part. These three functions remain game-owned; the server copies only their
proven arithmetic. The server's hit target and impact point still originate
from a bounded client report, so this is not server-verified line of sight.

The retail throw animation event `HUMAN_NOTIFY_THROW_GRENADE` creates a
temporary game-owned `C_grenade` through `C_game::NewGrenade` at `0x5ac580`.
Item 15 chooses timed explosion; item 5 chooses collision fire. reM shows a
five-second timed detonation, 15-unit explosion radius with 400 force, and
five-second fire with 2.5-unit radius and 50 damage. The native hooks and
remote replay are documented in “Grenades and Molotovs” below. The local
client owns the real projectile and reports its detonation; observers create
one disarmed display projectile from the accepted throw origin and impulse.

The stock `C_player::AI_car_fire` path at `0x592b70` uses the current selected
item and calls `Do_Shoot`; the car-shoot branch in `Shooting` works only after
native seat reconciliation establishes `m_pUsedActorEnter` and seat ID. A
complete combat release still needs server validation of line of sight,
dropped items and throwable projectile outcomes. The scoped firearm replay
queues actual native bullets for `TickShoot`; moving target collisions can
still differ between peers, and health remains server owned.

A seated shooter's arm is posed by the work-state-9 branch of
`C_human::Movement` (`0x57a710`), not by `PoseSetPoseAimed` (`0x579ea0`),
which returns false while `m_pUsedActorLeave` is set or the human sits in a
car. While `m_bIsAiming` (+0x1e5) is set it plays the window animation and
each frame turns the arm toward `m_vShootTarget` (+0x200, read at
`0x57b504`) from the muzzle frame (+0x210). `AI_car_fire` refreshes that
target every frame for the player. For a remote seated human the mod writes
the replicated aim direction, 100 units from the actor, into the target on
every aim update, so the arm follows between shots.

### Persistent aiming pose

### Server accepted weapon action replay

IDA's annotated retail `Game.exe` confirms that `C_human::Shooting` first
indexes the event table at human `+0x1f0` by the index at `+0x1f4` and returns
unless the event type is `3` (`ITEM_PROPERTY_END`). The default native event
table at `0x6368d0` starts with `{3, 0}`; its original first 40 bytes are
`03 00 00 00 00 00 00 00 00 00 00 00 FA 00 00 00 03 00 00 00 00 00 00 00 00 00 00 00 F4 01 00 00 03 00 00 00 00 00 00 00`.
IDA data references to that table include `C_human::GameInit` at `0x5755f8`,
`ChangeWeaponModel` at `0x57f355`, and weapon event processing at `0x58df6c`.
reM confirms the table's entries and the pointer/index/elapsed-time offsets
`+0x1f0/+0x1f4/+0x1f8`. The game owns the static table for process life; an
initialized human owns its event cursor only until native destruction. A
server accepted remote shot may set this cursor to the default terminal event
for the immediate `Shooting` call; on failed replay it restores the prior
cursor. On success `Shooting` replaces the cursor with that weapon's event
table, preserving its recoil and muzzle animation. The scoped `NewShoot`
replay documented above queues real native projectiles with zero direct
damage; the server later publishes accepted health changes. No hook is added
to the table, and no cursor pointer survives the game-thread call.

| Retail item | Original bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_human::Do_Reload` `0x585b40` | `51 53 56 8B F1 57 8B 86 F8 06 00 00 85 C0 0F 85 07 01 00 00 8B 86 00 07` | `void __thiscall(C_human*)`, no stack args, plain `ret` in IDA. Callers include player AI at `0x5916fb`, vehicle player AI at `0x592e5b`, and empty-magazine `Shooting` at `0x584689`. It requires no carried box/body, terminal weapon event, selected firearm type 1–3 and positive reserve ammo. It chooses the weapon reload event, sets the native reload flags and, for the pump shotgun, detaches the weapon from its target until the pump event. reM agrees. | Call only on a live generation-bound remote human, during a server accepted reload event, with the selected weapon model initialized. Native event table and target frames stay game owned; clear references on stream out and mission close. No hook is installed. |
| `C_human::Do_Shoot` `0x583590` | `53 56 8B F1 33 DB 39 9E F8 06 00 00 0F 85 4B 09 00 00` | `bool __thiscall(C_human*, bool state, const S_vector* target)`, `ret 8`; IDA callers include human/player AI `0x59141f`, `0x592e8a` and script action at `0x52872d`. For firearm state true, it updates the shoot target, enters aiming if needed, and calls `Shooting` if already aiming. Its melee press, held and release paths choose the native windup and attack animations. reM agrees. | A direct call requires a live initialized human and game-thread target pointer; it retains no target. The client hooks local melee input and replays server accepted press, hold and release on generation-bound remote humans. Firearms still use the narrower `Shooting` replay. |

reM's `G_Inventory::Nabij` replaces the loaded magazine from reserve for every
stock firearm except pump shotgun item 11. The extracted `predmety.def` flags
confirm `ITEM_FLAG_DISCARD_MAG` on items 6–10 and 12–14. When reserve is
smaller than a magazine, the old loaded rounds are discarded and the new
loaded count can decrease. Item 11 instead calls `NabijJeden` at each reload
animation event and loops until full or out of reserve. Combat intents follow
these native transitions one step at a time; a smaller reserve remains a valid
reload and must not be treated as a failed or absent action.

IDA's `G_Inventory::SelectByID` at `0x6081d0` (original bytes
`53 56 33 C0 57 66 8B 41 20 8B 7C 24 10 3B C7 75 08`) independently
identifies selected item at inventory `+0x20`, five 16-byte weapon entries at
`+0x30`, coat weapon at `+0x80`, four item entries starting `+0x90` with count
at `+0x08`, and pickup item at `+0xd0`. It calls `Select` when matching an
entry, which may swap items, drop the previous selection, update the hand
model and UI. The SDK uses this only as layout evidence; it does not call
`SelectByID`. reM's `G_Inventory` layout assertions give size `0xe4`.
IDA's `G_Inventory::Insert` at `0x608ce0` reads the live item table pointer
at `0x6d4c14` and count at `0x6d4c18`. Each `S_item` is `0xbc` bytes with
flags at `+0x20`; IDA scales the item index by 47 dwords and tests flag
`0x200000` for the coat slot, `0x40000` for a coatless big item, and mask
`0x24` for a weapon. reM confirms these constants and the item size. The
table is allocated during game data loading and freed by native data
shutdown. Read it only while a mission is ready; never retain its pointer
across mission unload. The five small slots plus one coat slot and selected
hand are native owned; a full server state may rewrite the simple
`S_GameItem` values but never free their borrowed using-object pointers.


IDA confirms `C_human::m_bIsAiming` at `+0x1e5`; reM asserts the surrounding
layout (`m_bIsCrouching` at `+0x1e4`, `m_iWeaponItemType` at `+0x1e8`). The
game owns this byte for the life of the initialized human. Read it only from
the local registered human and apply remote state only through a
generation-checked native handle. A new native generation must receive the
latest server state even if its revision has not changed.

| Retail item | Original bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_human::Do_Aimed` `0x57f830` | `56 8B F1 8B 86 F8 06 00 00 85 C0 75 52 8B 86 00 07 00 00 85 C0 75 48 53 8A 5C 24 0C 84 DB 74 0F` | `void __thiscall(C_human*, bool)`, `ret 4` at `0x57f88c`/`0x57f897`. IDA callers include `C_player::AI_car_fire` `0x592b70`, player AI `0x58f4b0`, and native enter/exit paths. It refuses aiming if the human is carrying a box (`+0x6f8`) or dead body (`+0x700`), applies a pending weapon model when aiming, writes `m_bIsAiming`, and fades any melee aim overlay when aiming stops. reM `C_human.cpp` agrees. | Call only on a registered initialized human on the game thread. It changes native animation state, with no returned resource to own. The mod adds no hook at this entry; generation invalidation or mission close ends all access. |
| `C_human::Do_Crouched` `0x57f8a0` | `83 EC 18 56 8B F1 8B 86 F8 06 00 00 85 C0 0F 85 DA 00 00 00` | `void __thiscall(C_human*, bool)`, `ret 4` at `0x57f98b` and `0x57f999`. IDA callers include `C_player` control processing at `0x59177e`, entity AI at `0x58b875`, and script action paths. It reads the crouch flag at human `+0x1e4`. Carrying a box or body forces standing. On uncrouch it tests a sphere above the native frame against static and dynamic collision and keeps crouch if blocked. reM `C_human.cpp` agrees. This call changes the flag but does not itself rebuild the animation overlay; `PersonAnim` uses the new flag. | Call only on a live registered human during the matching mission on the game thread, after server stance acceptance. The frame and collision storage belong to the native human. Never retain them across stream out, native destruction or mission close. There is no hook or patch to unload. |
| `C_human::PersonAnim` `0x573e50` | `83 EC 48 33 C0 53 55 56 8B F1 57 8B 7E 74 81 E7 FF 01 00 00 8B CF 89 7E 74 83 F9 05 7C 0A 83 F9` | `void __thiscall(C_human*, float blend, bool setDuration, bool loaded)`, `ret 0c`. Existing locomotion replay already calls it. IDA/reM show it selects a one-handed, two-handed or rifle aim overlay from `m_bIsAiming` and weapon item type, and mutates the game's animation machine and state. Normal movement, weapon and human AI paths also call it. | Its animation machine and frame belong to the live human. Re-evaluate it on an aim transition for an on-foot remote actor, then let regular native movement updates continue. In a car, native seated animation remains the owner of base pose; do not overwrite it with pedestrian locomotion. No new hook or unload step. |
| `C_human::PoseSetPoseAimed` `0x579ea0` | `81 EC B4 00 00 00 55 8B E9 56 8B 85 9C 00 00 00 85 C0 74 0D 5E 32 C0 5D 81 C4 B4 00 00 00 C2 0C` | `bool __thiscall(C_human*, S_vector target by value)`, `ret 0c`. IDA caller `C_player` `0x5937b0` aims the local neck/back toward the camera target; reM shows it refuses pose adjustment while entering/leaving a car or seated, and requires a live neck frame/base mesh and target within about 60 degrees of current direction. It writes interpolated pose directions and target on success. | The frame/base mesh are native owned; only call for an on-foot initialized remote human with a current server pose target, after resolving its generation-bound handle. No pointer survives stream out or mission close; no hook is installed. |

Aim is a lasting replicated state. A local transition submits the native flag
and normalized target direction; the server publishes the accepted flag and
direction in its complete state to observers. The controller owns its native
pose, so the server excludes it from aim/pose state echoes. The camera pose target also updates while
the player is not aiming, including with no weapon selected, so remote neck
and back movement follows ordinary look up/down input. Those pose updates
do not emit `playerAimChange` scripting events unless the aim flag changes.
The retail `C_player::Update` call at `0x5937b0` passes active camera world
position plus ten units of camera forward direction to `PoseSetPoseAimed`.
The shot target stored on `C_human` is a separate 100 unit camera ray and can
terminate early at collision. For the remote hand and torso pose, the client
therefore sends the ten unit pose target relative to its actor position as a
separate bounded offset. The server retains that offset in the complete state;
the remote client adds it to its current interpolated actor position before
calling the audited native pose function. The remote target uses a 45 ms
exponential blend between accepted updates, and player transforms use at
least 75 ms of snapshot interpolation; the controlling client stays on its
native pose without this presentation delay. `I3D_frame` world reads and the
scene's active camera lifetime are documented above. A crouch transition is
similarly retained in the complete state and applied through `Do_Crouched`
before `PersonAnim` rebuilds the correct standing/crouched weapon overlay.
Shot events remain discrete, separately deduplicated effects. On disconnect,
mission reset and native destruction, the client discards any cached aim
application with the corresponding generation.

## Native Game Over and stock player lifetime

The stock failure menu is entered by `C_game::Tick`, even when the mission's
global programs are suppressed. IDA confirms two independent conditions:
`C_game::m_bPlayerDeathTriggered`, used by player fall and occupied-car
collision paths, and a dead `C_game::m_pPlayer` after its death-menu timer
expires. The game must still run its original tick to advance actors, death
animation, physics, camera and mission state. During a mod-owned mission the
planned hook clears only the native failure trigger and holds the native
death-menu timer nonnegative while the server decides when to respawn. It does
not clear the actor's dead flag or reset health.

| Hook or call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_game::Tick` `0x5a51c0` | `64 A1 00 00 00 00 6A FF 68 8C 0B 62 00 50 64 89 25 00 00 00 00 81 EC CC` | `void __thiscall(C_game*, unsigned frameMs)`, `ret 4` at `0x5a6d40`; called by `C_mission::Tick` at `0x54086a`. IDA reads death-trigger byte `+0x2ad8` at `0x5a5276`, player pointer `+0xe4` at `0x5a5357`, player dead byte `+0x5e` at `0x5a5365`, and death-menu timer `+0x2fd4` at `0x5a5370`. The triggered path constructs `GM_Menu_gamefailed` and calls `GM_Menu::ExecuteMenu` at `0x5a52fa`; the dead-player path does the same at `0x5a53f3` after the timer passes below zero. The original tick also updates all game systems and must be called exactly once. | The mission owns `C_game`; hook applies only while its mod-owned mission is active. Remove hook before native `CloseSystem`. A player pointer is only read while the mission is live and is never retained across destruction or transition. |
| `GM_Menu_gamefailed` constructor `0x47a3e0` and vtable `0x624898` | Constructor: `56 8B F1 E8 08 FE 16 00 8B 44 24 08 C7 06 98 48 62 00 89 46 30 8B C6 5E`; vtable begins `70 A4 47 00 80 28 5E 00` | `GM_Menu_gamefailed` is a `GM_Menu` subclass of size `0x34` (reM); its constructor is `__thiscall(GM_Menu_gamefailed*, unsigned textId)` and writes the vtable. IDA finds four vtable writes: script VM at `0x471780`, police office `C_shvestky::Tick` at `0x49a439`, and both `C_game::Tick` death paths at `0x5a52e3` and `0x5a53dc`. Any of these can open the failure screen. The execute hook identifies the subclass by its typed SDK vtable view and returns result zero for a mod-owned mission. | A newly created failure menu belongs to its pending `ExecuteMenu` call. If execution is skipped, the hook must destroy it once. The hook is removed before native shutdown. |
| `GM_Menu::Destroy` `0x5ea690` | `56 57 8B F9 8B 77 14 8B 47 18 3B F0 73 16 8B 0E 85 C9 74 06 8B 01 6A 01` | `void __thiscall(GM_Menu*)`. IDA callers include `GM_Menu::ExecuteMenu` at `0x5ebd6e`, menu exchange at `0x5eac1e`, and another menu handler at `0x5bcc72`. It destroys owned components, invokes the base menu destructor and frees the menu allocation. `ExecuteMenu` normally performs this ownership cleanup after its loop. | The game-failed menu has not been created when the execute hook intercepts it, so its component vector is empty. The SDK calls `Destroy` exactly once on the intercepted menu; the pointer is discarded immediately. |
| `C_program::CallProcess` `0x46ccb0` | `56 57 8B 7C 24 0C 8B F1 8B 4E 10 85 C9 74 2B 8B 46 14 2B C1 8B 4E 1C C1` | `bool __thiscall(C_program*, unsigned frameMs)`, `ret 4` at `0x46ccec`; IDA callers `C_detector` tick `0x432d96`, `C_program::CallSubroutine` `0x46cbf6`, and entity script scheduler `0x51b425`. It checks the compiled instruction vector and current instruction, then dispatches the virtual `Process` slot at vtable offset `0x1c`, repeating while its command-block flag is set. reM confirms this virtual call reaches C_program, C_detector, or C_entity opcode handlers. During a server-managed mission the hook returns false before any opcode dispatch. Outside that mission it calls the original exactly once. | C_program instances belong to mission global programs or actor script objects. Their `Init`, `Done`, actor `Tick`, and native mission teardown remain native. The hook keeps no program pointer and is removed before system shutdown. A mission close destroys its programs through their normal owner. |

The `CallProcess` false return is safe at all three retail call sites. IDA
shows `C_detector::AI` tail jumping to it; AI's caller ignores the returned
register. The entity scheduler continues at `0x51b42a` without testing EAX.
`C_program::CallSubroutine` returns its value, but its only two callers,
`C_mission::GlobalProgramRun` and `C_game::GlobalProgramRun`, ignore that
value; the latter returns true from finding the named program independently.
No caller interprets false as actor deletion or a mission failure.

The first live `freeride` load showed a null native player and clear
`m_bPlayerDeathTriggered` immediately after `C_game::Init`, but still opened
Game Over before a server player spawned. The `C_game::Tick` pre-hook alone did
not prevent it. IDA's additional script and police call sites mean the
`GM_Menu::ExecuteMenu` boundary is the narrow shared point for suppressing
this retail screen. The hook logs the first caller address so the exact
trigger remains observable during runtime verification. Its zero result is
neutral for all three callers: neither `C_game`, `C_shvestky`, nor the script
end-of-mission opcode selects an exit/restart/load state for zero.
The live caller was `0x47179f`, inside the mission bytecode interpreter's
end-of-mission opcode, confirming that an actor or other script program still
executed despite the global program hooks. Blocking `CallProcess` is the
primary gameplay-script suppression. The menu boundary remains a narrow
fallback for any other retail failure path (native death or police arrest)
while a server mission runs.
| `C_mission::CreateActor` `0x53f7d0` | `64 A1 00 00 00 00 8B 4C 24 04 6A FF 68 D9 F5 61 00 50 33 C0 49 64 89 25` | `C_actor* __thiscall(C_mission*, int type)`, `ret 4`; IDA callers include mission `OpenBin` `0x544b02`, script actor creation, save/load, and retail `FreerideSetup` `0x60e9e9`. Type 2 allocates `C_player`. The actor constructor starts refcount one and registers the object in the global actor list; this factory alone does not register it in the mission actor lists. | On later initialization failure, call virtual `Release`. After registration, the mission owns the actor reference. The pointer becomes invalid when release reaches zero. |
| `C_actor::Init` `0x405dd0` | `8B 44 24 04 56 85 C0 8B F1 75 06 32 C0 5E C2 04 00 8B 48 08 85 C9 74 10` | `bool __thiscall(C_actor*, I3D_frame*)`, `ret 4`; IDA callers include actor-specific overrides. It requires a frame, checks existing frame ownership, assigns the frame and a mission-local actor ID, and enables actor state. `C_mission::OpenBin` calls this before `AddActor`. | The frame and actor must remain live through `GameInit`; release the newly created actor if initialization fails. |
| `C_mission::AddActor` `0x53fea0` | `53 55 56 57 8B F9 BB 04 00 00 00 8B 57 38 8D 4F 34 85 D2 74 23` | `void __thiscall(C_mission*, C_actor*, bool gameActor)`, `ret 8`; IDA callers include `OpenBin` `0x544b25` and retail `FreerideSetup` `0x60eed1`. It deduplicates the pointer in `m_Actors`; `gameActor` also inserts in `m_GameActors`. It does not increment actor refcount. | Mission vectors own the initial actor reference after insertion. Close or `DelActor` releases it. |
| `C_mission::DelActor` `0x540130` | `55 56 57 8B F9 8B 47 38 85 C0 74 24 8B 6F 3C 2B E8 C1 FD 02 74 1A 8B 74` | `bool __thiscall(C_mission*, C_actor*)`, `ret 4`; IDA callers include game actor removal and reload. It removes the actor from both mission vectors, clears the frame's actor-owner slot, then calls virtual `Release`. `DelActors` `0x540240` releases every remaining mission actor during `Close`. | Network handles must be invalidated before this call and before mission close; the actor may be destroyed before return. |
| `C_actor::Release` `0x4091d0` | `66 FF 49 0C 66 8B 41 0C 66 85 C0 75 0A 8B 01 6A 01 FF 50 40 66 33 C0 C3` | Virtual `unsigned short __thiscall(C_actor*)`; IDA resolves it from actor vtables. Decrements refcount at `+0x0c` and invokes the deleting destructor at zero. | No pointer may be reused after a zero result; mission close and actor removal can reach zero. |
| `C_human::Activate` `0x58ac90` | `83 EC 28 53 55 8B E9 33 DB 56 57 8B 45 68 88 5D 5E C6 45 5D 01 88 5D 5F` | `void __thiscall(C_human*)`, plain `ret` at `0x58b32e`. IDA callers include actor rebirth from mission frame buffer at `0x54639b` and human action paths at `0x49d8ce`, `0x49f36e`, `0x49fd5b`. It clears dead, sets alive and active, turns on the frame, reloads current properties from initial properties, resets inventory, animation, vehicle interaction, movement and collision state, creates collision structures/shadow, then grounds the human using a collision query. | Call only on an initialized human with a live model frame, game and collision scene after leaving a previous active lifecycle. A direct call can reset inventory and position and may duplicate collision/shadow resources if prior state was not deactivated. Never call after mission close or actor release. |

Retail `C_mission::Open` calls `Close` before loading a new scene, so no actor
from `00menu` survives the transition. `OpenBin` reads player actors from
mission data using `CreateActor`, `Init`, `SetSpawnRegistered`, `AddActor`,
and a frame-owner entry. `C_game::Init` sets its player pointer to the first
type-2 actor in the mission list; it does not create one. `C_game` clears that
pointer when its player actor is deleted. For retail freeride, WinMain calls
`G_LoadSaveClass::FreerideSetup` at `0x60e9d0` before `C_game::Init`; that
routine creates a `C_player` named Tommy and a car, places both at an `emeth`
spawn frame, then registers them in the mission. The mod's generic mission
flow does not call that freeride-specific routine, so runtime logging must
confirm whether the mission archive supplied any stock player and which
condition actually opened Game Over. Suppressing the native failure menu is
necessary even after a server-owned player is created because death and fall
are normal multiplayer states.

The initial actor state does not itself explain a freshly loaded Game Over:
`C_game::Init` calls `GameInit` on each selected mission actor, and
`C_actor::GameInit` clears dead and sets alive. `C_human::GameInit` copies the
initial health properties to current properties and resets inventory and
animation state. A failure menu shortly afterward therefore needs a later
fall/collision death trigger, a later actual death, or another game-state
transition. Log the player pointer, dead flag, death trigger and timer on
each transition before changing player creation or respawn behavior.

### Script-owned shopkeeper collision and facing

The client script human factory registers a temporary `C_entity` through
`C_game::AddTemporaryActor`, which invokes native `C_human::GameInit`. reM's
`C_human::GameInit` calls `CreateDynColls` at `0x575ca0`: it creates two
human spheres, sets each owner to the actor, and inserts both through
`g_collision::DynInsert`. `C_human::DeactivateColls` at `0x575f60` erases
them. The old script API called it unconditionally after creation, which is
why Yellow Pete had no collision. The script API now leaves them inserted
when its optional `solid` argument is true. `C_human::UpdateDynColl` at
`0x576040` reads the current human frame positions and calls `DynUpdate`
during native updates; a stationary shopkeeper needs no separate grid write.
Native `GameDone` and actor destruction remove the collision objects. The
script invalidates its handle in the same call that queues removal.
While a solid local human is active, the client Hit, Death, ForceDeath,
CarAirborne and SetActState hooks consume paths that would kill it on only
one client. Collision response itself stays native.

`I3D_frame::SetDir` uses the supplied vector as frame forward, matching
reM's `C_human` movement and hit-facing paths. The stock
`1dvere u peteho` anchor at `(61.13562, 6.12056, 113.52177)` places Pete
by the doorway, while Free Ride's `Box24` door hinge is at
`(60.58458, 6.09017, 113.82933)`. The vector from Pete to that hinge
points him into the door. The sample uses the opposite horizontal vector so
he faces arriving players. `C_actor::GameInit` reads the model frame's local
direction into its stored direction, so the native actor and displayed model
start with the same facing.

## Remote human locomotion

The native remote `C_entity` is network-controlled. reM's `C_actor::Tick`
calls `NetDirect` instead of `AI` for that flag, but still calls the virtual
`Update`; `C_human::Update` advances its animation machine and calls
`PersonAnim`. Replicated transforms currently update the actor's position and
direction only, leaving its animation state at the initialized idle value.
This makes a moving remote human slide in a fixed pose. The mod selects a
locomotion state from the interpolated horizontal velocity in the actor's
facing frame, then lets the native animation machine tick normally. An
interpolation hold selects idle so a frozen snapshot cannot keep running.

| Call or view | Original retail bytes and IDA evidence | Convention, effects, lifetime |
| --- | --- | --- |
| `C_human::PersonAnim` `0x573e50` | `83 EC 48 33 C0 53 55 56 8B F1 57 8B 7E 74 81 E7 FF 01 00 00 8B CF 89 7E 74` | `void __thiscall(C_human*, float stageBlend, bool setDuration, bool loaded)`, `ret 0ch` at `0x574702`. IDA reads and masks animation state at `+0x74` on entry and has native callers in `C_human::Update` at `0x572ef1`, `0x572fc3`, `0x57308c`, `0x573443`, `0x573bef` and `Do_StopAnim` at `0x57d301`. reM shows `PersonAnim(0.0f, true, false)` choosing the stock animation table, honoring weapon/aim overlays, and setting the animation machine. IDA confirms the state field offset; reM asserts the preceding animation ID at `+0x70` and state at `+0x74`. The temporary human exists only during the loaded mission; this is a direct call, not a hook. Native `GameDone` releases animation resources, and mission close invalidates the actor handle. |

reM's retail animation table at `0x62e488` maps idle/breath to state 1,
forward run/walk to 5/6, back walk to 7, left/right walk to 11/12,
left/right run to 13/14, forward diagonal run to 15/16, forward diagonal
walk to 17/18, and back diagonal walk to 19/20. The client uses these native
states only while the server replica is alive, after resolving its current
generation-scoped actor handle. It does not call human AI or make the remote
actor locally authoritative for movement.

## Server-triggered native human lifecycle

The client may receive a player replica only after `C_game::Init`. It creates
one temporary `C_player` for the local controller and temporary `C_entity`
actors for remotes, using the server-selected stock human `.i3d` model
(`Tommy.i3d` by default). A mission without a server spawn remains playerless.
The temporary actor is game-owned until `RemoveTemporaryActor` completes or
`C_game::Done` clears it. An actor handle is invalidated before a removal is
queued and again at the base destructor; the scene/model frame is released
after the human's native `GameDone` restores scene ownership.

The server replicates the model name at construction and in reliable field
updates. For a live change, the client calls `C_human::intern_ChangeModel`
at `0x587190` with `preserveRegistration=true`, then refreshes the selected
weapon model. reM shows this path reloads the existing model frame and rebuilds
the animation machine, pose bones, shot skeleton, collision, shadow and weapon
joints. Preserving registration keeps the game player, camera and car seat
references intact. The reload can return after tearing those resources down if
the model cache cannot open the file, so the client first opens it on a throwaway
frame and checks every bone that retail shot collision dereferences. Missing or
non-human models leave the current human intact and log a warning. A swap waits
until the human is alive, on foot and in idle or ordinary locomotion state so a
car or action animation cannot lose its active frame links. The server's choice
remains on the replica across respawns and mission changes.

| Call or view | Original bytes and IDA evidence | Convention, effects, lifetime |
| --- | --- | --- |
| `I3D_scene::FindFrame` LS3DF preferred `0x10049210` | `53 8B 5C 24 08 8B 8B 10 02 00 00 56 8B 74 24 14 57 8B 7C 24 14 56 57 E8` | `I3D_frame* __stdcall(scene*, const char*, unsigned flags)`, `ret 12`; scene vtable slot `+0x58` at preferred `0x1009c950`, immediately before audited `AddFrame`. Searches primary sector, optionally other roots; returns a borrowed frame or null. Used only while the loaded mission owns the scene. `ENUMF_ALL` is `0xffff` in reM. |
| `I3D_frame` world matrix | Retail `GetWorldPos` `0x47a730` begins `56 8B F1 F6 86 AC 00 00 00 20 75 05 E8 EF 54 19 00 8D 46 40 5E C3`; it checks `FRMFLAGS_WMAT_VALID` at `+0xac`, updates if needed, and returns translation at `+0x40`. IDA's `I3D_frame::Update` LS3DF `0x1001ca60` calls vtable slot `+0x44` to publish the own world matrix. | The SDK uses its already audited `Update` call before reading matrix rows, with `I3D_frame::m_mWorldMat` at `+0x10`, forward at `+0x30`, translation at `+0x40`. reM asserts these field offsets and the scene active camera/primary sector at `+0x17c/+0x210`. These are borrowed fields, never retained past mission close. |
| `G_Camera::SetPlayer` `0x5ed170` | `56 8B F1 E8 D8 62 00 00 8B 44 24 08 8B CE 89 46 08 E8 1A 5D 00 00 5E C2` | `void __thiscall(camera*, C_human*)`, `ret 4`. IDA callers include `C_game::Init` `0x5a1ea0`, reload `0x5be5da`, game human switching `0x5a0802` and save loading `0x606c88`. It ends the current camera mode, swaps `m_pPlayer` at `+0x08`, then begins the mode. Null is accepted, and reM's camera tick exits when there is no player. Embedded `G_Camera` begins at `C_game+0x4c` and is `0x98` bytes; no pointer is retained after disconnect/mission close. |
| `G_Camera::SetCar` `0x5ed190`, `G_Camera::Link` `0x5ed1e0` | reM `G_Camera.cpp` reconstructs both retail entry points. `SetCar(nullptr)` clears `m_pCar`, switches to the profile's pedestrian mode, disables free look and begins that mode. `Link(nullptr)` clears its borrowed linked frame and forces the same pedestrian mode. reM asserts `m_uMode` at camera offset `+0x10`. | `C_human::Death` sets fixed camera mode 6; `SetPlayer` does not reset the mode. After the new living actor is focused, the client calls these two methods once and logs the mode before and after reset. They require an initialized mission camera and run before normal camera tick. |
| `C_actor::~C_actor` `0x405d90` | `56 8B F1 6A 00 56 C7 06 48 32 62 00 A1 C8 D2 63 00 48 B9 00 79 64 00 A3` | `void __thiscall(actor*)`, plain `ret`. IDA callers include deleting destructors of `C_player` at `0x58e3e3`, `C_entity` at `0x4f1c8d`, and `C_car` at `0x41bd67`; reM shows it removes recording/global references and releases actor-owned frame only when `m_bRemoveFrame` is set. The hook invalidates network identity before the native body runs, then deletes a mod-owned human frame from the mission scene after native `GameDone` has transferred it there. It is installed after Framework initialization and removed before native shutdown. |
| `C_player::Init` virtual actor slot `+0x48`, `0x58f260` | `8B 44 24 04 56 50 8B F1 E8 E3 00 FE FF 84 C0 75 04 5E C2 04 00` | `bool __thiscall(player*, model*)`, `ret 4`. IDA raw player vtable at `0x625878+0x48` contains `0x58f260`; the function calls `C_human::Init`, returns false on failure, and sets initial player behavior on success. The frame must remain live until temporary registration invokes GameInit. |
| `C_player::GameInit` virtual actor slot `+0x64`, `0x594060` | `53 56 8B F1 33 DB 83 C8 FF 68 FF FF 00 00 66 C7 86 9C 0A 00 00 FF FF 89` | `void __thiscall(player*)`. IDA raw player vtable at `0x625878+0x64` contains `0x594060`; `C_game::AddTemporaryActor` calls this virtual slot. It resets player controls and alpha, then calls `C_human::GameInit` to install animation, physics, collision and sounds. The game pairs it with virtual GameDone during temporary removal or mission close. |

`C_game::m_pPlayer` at `+0xe4` and `m_pHuman` at `+0xe8` are distinct.
IDA's `C_game::Init` selects the first mission type-2 actor for both before
`GameInit`; `C_game::SetHuman` at `0x5a07e0` writes `m_pHuman` and calls
`G_Camera::SetPlayer`. For a late local temporary actor the SDK writes
`m_pPlayer` before `AddTemporaryActor` (because `C_human::GameInit` reads it),
then calls the native `C_game::SetHuman` after native `GameInit` completes. On
removal it clears the camera and both pointers before queuing the actor.
`C_game::InvalidateActor` at retail `0x5a...` clears `m_pPlayer` when a
temporary actor is removed, but reM shows it does not clear `m_pHuman` or
camera, so this explicit cleanup is required.

Server camera follow changes only `G_Camera::m_pPlayer` and `m_pCar` on the
observer. `C_game::m_pPlayer` and `m_pHuman` stay on the observer's own human,
so retail input, movement and death logic still address that player. reM
`C_game::Update` writes `G_Camera::m_fPitch` at camera `+0x28` from the local
`C_player` immediately before `G_Camera::Tick` (`0x5ed4c0`). The observer
therefore receives a server-validated view pitch and yaw from the followed
client. The camera-tick hook substitutes that view only for the pedestrian
spectator tick; it restores the camera mouse globals, 30-sample inertia ring
and LS3DF mouse delta afterward. reM's pedestrian mouse mode 2 computes
native camera orbit and collision from those angles, then writes the final
view heading to `C_actor::m_vDirection` at `+0x30`. The hook restores that
field after the tick so observer input never changes the remote actor's
replicated heading. The source view is sent every 50 ms and smoothed locally
  over 35 ms. Cars retain retail `SetCar` camera behavior.

reM `C_human` enters/exits a car with `SetCar` only for the game player, so a
remote human's replicated seat must drive the camera's car handoff explicitly.
When a followed human changes, `SetPlayer` cannot keep the old car link:
the client detaches the old car, switches player, then reattaches the new
car even when both players occupied the same vehicle. `SetCar` chooses the
saved car profile, and `LookAround(0)` clears an inherited side/back target
on the new handoff; it does not rewrite the user's saved camera mode.
Retail `SetCar` stores a borrowed car pointer at camera `+0x0c` and `End()`
dereferences it; the client detaches a followed car before its native removal
and a followed human before its native removal. Reconciliation runs after
seat updates and restores the observer's own ped/car on target despawn,
stream loss or `/follow off`; a selected target's next spawn can be followed
again. Both player and car replicas are always visible in this server.

## Native car seats, doors and exits

The server's `CarEntity` already carries occupant network IDs and spawn
generations for every seat plus a reliable latest seat outcome. The client
must resolve both actors through generation-checked native handles before
applying that state. reM's `C_human` layout and IDA field reads put
`m_pUsedActorEnter` at `+0x98`, `m_pUsedActorLeave` at `+0x9c`, and `m_iSeatID`
at `+0xac`. These fields describe different transition phases: an entering
human may own a seat while `m_pUsedActorLeave` is the car and
`m_pUsedActorEnter` remains null until the door animation completes. Direct
client field writes would skip collision, animation, camera and frame links;
the SDK uses native methods instead.

| Hook or call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_human::Use_Actor` `0x582180` | `81 EC F8 00 00 00 53 55 56 57 8B E9 E8 CF 83 FF FF 8B 9C 24 0C 01 00 00` | `void __thiscall(C_human*, C_actor* actor, int action, int seat, int animationSpeedState)`, `ret 16`; IDA callers include player use handling at `0x5950b8` and actor/script paths. reM identifies action 1 as enter and 2 as exit. Enter validates the car approach, selects a door animation, positions the human, reserves native car ownership and later links the frame. Exit checks free space; if the current side is blocked it tries the paired seat or forces an exit. A local-action hook observes the original result and reports the resolved network actors; it does not alter non-car actor use. | Human and car are mission-owned or temporary actors. The hook may act only while both generation-checked handles resolve and the server mission is ready. Remove it before mission/system teardown; never keep native pointers in network messages. |
| `C_human::Can_DropOutFromCar` `0x5805c0` | `8B 44 24 04 81 EC 94 00 00 00 85 C0 53 55 8B E9 7D 06 8B 85 AC 00 00 00` | `bool __thiscall(C_human*, int seat)`, `ret 4`; called twice from `Use_Actor` at `0x5827c0`/`0x582d08`. Negative seat uses the current `m_iSeatID`. It tests ground and collision clearance, temporarily excludes the human and car collision objects, then resets the collision owner. When both sides fail, retail invokes `ForceExitCar`; the mod lets this complete and reports the resulting exit. | Requires a live initialized human, car and collision scene. It retains no pointer and its scratch collision filter is restored before return. |
| `C_human::Can_EnterToCar` `0x5808c0` | `81 EC B0 00 00 00 53 55 8B E9 56 8B 85 98 00 00 00 85 C0 74 0E 5E 5D 32` | `bool __thiscall(C_human*, C_car*, int seat, bool testHierarchy)`, `ret 12`; called by `Use_Actor` at `0x582295`. It rejects a human already in a car, checks the seat/door and swept collision, and optionally tests scene hierarchy. The local action follows this retail check. | Live mission scene and collision system required; no retained pointers. |
| `C_human::Intern_UseCar` `0x57e020` | `83 EC 0C 55 8B 6C 24 14 56 57 8B 85 14 0D 00 00 8B F1 85 C0 75 04 33 D2` | `void __thiscall(C_human*, C_car*, int seat)`, `ret 8` in reM; IDA callers include mission spawn, scripted NPC, reload, and the native car-steal setup. It assigns `C_car::SetOwner`, sets `m_pUsedActorEnter` and seat index, installs seated animation, switches collision/shadow and inventory, links the human model to the car frame, and switches local camera/controls. Use for an authoritative late-join seat snap after both native handles are ready; it does not show an entry door animation. | Only on initialized live actors in the same mission, and only once when the target seat is not already bound to that human. Native exit/mission close must release ownership before actor destruction. |
| `C_human::Intern_UseCar(bool)` `0x5716d0` | `64 A1 00 00 00 00 6A FF 68 78 05 62 00 50 8A 44 24 10` | `void __thiscall(C_human*, bool enter)`, `ret 4`. `C_human::Update` calls it with `true` at `0x572fe2` when `m_iAnimBlendState` (+0x410) is 1 and the `Use_Actor` door animation ends. The enter branch sets `m_pUsedActorEnter` from `m_pUsedActorLeave`, calls `C_Vehicle::LockVehicle(false)` at `0x5717f1`, links the frame, sets work state 9, then clears `m_pUsedActorLeave` and +0x410. The mod calls it to finish a replayed remote entry that must be snapped or released, instead of `0x57e020`, which never unlocks. | Only when +0x410 is 1, `m_pUsedActorEnter` is null and `m_pUsedActorLeave` is the live car, so the lock is released exactly once. |
| `C_Vehicle::LockVehicle` `0x4cd600` | `8A 44 24 04 84 C0 8B 81 C0 00 00 00` | `void __thiscall(C_Vehicle*, bool)`, `ret 4`, on the car's `C_Vehicle` at +0x70. A counter at vehicle+0xc0; bit `0x10000000` of vehicle+0x1a4 is set while it is positive, and the vehicle tick then skips engine and motor force, so the driver's input does nothing. `Use_Actor` Enter takes it at `0x582576`; only `0x5716d0` or `C_car::Reset` releases it. A remote entry snapped with `0x57e020` mid-animation therefore left the controller's car undrivable. | Not called directly by the mod. |
| `C_car::SetOwner` `0x41d810` | `53 55 8B 6C 24 10 33 DB 56 3B EB 57 8B F1 0F 8C FD 03 00 00 8B 86 CC 21` | `bool __thiscall(C_car*, C_actor* owner, int seat, float massFactor)`, `ret 12`. IDA callers include `Intern_UseCar` at `0x57e086`, `Use_Actor` at `0x582568`/`0x582841`, exit and destruction paths. It validates the native in-point and existing owner, adjusts occupant mass/driver audio and material state, and stores or clears the owner. Failed assignment must not be treated as a successful seat bind. A null owner clears the seat; reM `C_game::InvalidateActor` does this only for a human whose `m_pUsedActorEnter` is the car, so a door reservation (`Use_Actor` sets `m_pUsedActorLeave` and calls `SetOwner` at `0x582568` before the seated link) survives the human's removal. The mod calls it with a null owner, the human's `m_iSeatID` and zero factor to clear such a reservation before removing that human, or on a car being removed when the reserved link cannot be completed. | Owner is borrowed and must outlive the seat assignment. Game exit, forced exit and mission teardown clear ownership. Call only while the car is live, before `RemoveTemporaryActor` of either actor; no pointer is retained. |
| `C_car::GetOwner` `0x41dec0` | `56 8B 74 24 08 85 F6 7C 43 8B 81 CC 21 00 00 85 C0 74 39 8B 91 D0 21 00` | `C_actor* __thiscall(C_car*, int seat)`, `ret 4`; widely used by player interaction and car AI, including native steal logic. Returns null for an invalid/unoccupied seat and a borrowed actor pointer otherwise. The hook can compare the returned pointer with the resolved local human after `Use_Actor`. | Do not cache the returned actor across native destruction, stream out or mission change. |
| `C_car::GetSeatProperty` `0x41dc30` | `83 EC 18 56 8B 74 24 20 85 F6 57 0F 8C 6B 02 00 00 8B 81 CC 21 00 00 85` | `bool __thiscall(C_car*, int seat, bool* left, bool* door, bool* rear, bool* open)`, `ret 20`. IDA callers include native exit clearance `0x580606`, entry check `0x580908`, `Use_Actor` `0x5822b9`/`0x5827ea`, and car initialization. It bounds checks the seat, rejects an absent seat record, and fills the four caller-owned output booleans. The SDK uses the return value only to avoid binding an unsupported native seat or testing an absent paired exit. | The car and its seat table must remain alive for the call. The output pointers are stack locals and are not retained; no unload side effect. |
| `C_human::ForceExitCar` `0x581220` | `6A FF 68 08 06 62 00 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 81 EC 94` | `void __thiscall(C_human*)`, plain `ret`; IDA callers include overturned-car and blocked-side exits at `0x580dc1`, `0x580f10` and `0x582d31`. It probes both AI exits, then falls back above the current seat if both are blocked; it detaches the frame, clears the owner and restores collision, camera and weapon state. The client reports the exit from the seat that native code actually released. | Call only while the human is seated in a live car. It clears car references; handle invalidation still precedes actor destruction. |
| `C_human::Do_ThrowCocotFromCar` `0x587d70` | `6A FF 68 A2 06 62 00 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 81 EC AC` | `bool __thiscall(C_human*, C_car*, int seat)`, `ret 8`; player occupied-seat handling calls it at `0x594e5f` and `0x594e70` after validating the target. It checks `Can_DropOutFromCar_Free`, plays the attacker's door/throw animation, ejects a live player/entity owner through `intern_ThrowMeFromCar`, or instantiates a thrown traffic NPC. An observer can report a steal-start action; the authoritative occupant change must wait for the eventual native seat owner. | Call only on live initialized actors in the current mission. It can create/remove temporary actors and change police state, so replaying it on a late join would be unsafe. Unhook before actor teardown. |

Native `Use_Actor` forces an alternate-placement exit when both sides are
blocked. The hook lets this native path finish and reports the released seat.
Late joins use the
array as the durable truth and `Intern_UseCar` to bind a human directly to
the correct native seat. Entry and steal animations are transient events and
cannot be reconstructed from the durable array alone.

The client reports `EnterBegin` as soon as retail reserves an empty seat, then
reports `Enter` after the human is actually linked. It reports `StealBegin`
only when retail starts the throw, and `Steal` after native ownership changes.
These ordered reliable events let connected clients play the door or throw
animation. The server authenticates the sender, player and spawn generations,
mission, seat index, sequence, state and approach distance before changing
the durable occupant array. A newly connected client uses that array to bind
the correct human without attempting to replay an old door animation. If
native `SetOwner` rejects the bind while an old exit finishes, reconciliation
retries after a short delay.

reM `C_human::Intern_UseCar` shows that it calls `SetOwner` without branching
on the result, then establishes the frame link, `m_pUsedActorEnter`, and seat
ID. `C_car::SetOwner` rejects a non-null owner when the seat already has any
owner, including that same human. A remote entry can therefore reserve the
correct native owner while its door animation fails to attach the human.
After the transient animation window, reconciliation calls the audited
`Intern_UseCar` for that same owner only if the seated link is still absent;
it verifies the link and owner afterward. It does not call `ForceExitCar` on
a reservation without a seated link.

`Do_ThrowCocotFromCar` can eject the current native owner while its
`StealBegin` animation is still running, before the server has accepted the
final `Steal` outcome. Reconciliation temporarily defers both the thief and
the displaced occupant for that seat, then uses the server occupant array to
repair either a completed or an aborted steal.

IDA's `Use_Actor` exit path at `0x582cba` chooses the paired seat by subtracting
one for odd seats and adding one for even seats. It enters the force-exit path
at `0x582d31` when that seat is absent, occupied, or blocked. Otherwise
`Do_ClimbInCarLR` (`0x581eb0`) moves native ownership and `m_iSeatID` to the
paired seat before its animation. The client reports a `Move` intent with the
new seat; the server validates the prior occupant and empty paired seat and
updates both durable slots atomically. The later retail exit reports the
new seat. Remote clients replay the native climb and the exit in order;
reconciliation holds both seats during the local transition.

After the local client reports a completed Enter, Steal or Exit it keeps the
transition pending until the replicated occupant array agrees, or for two
seconds, so reconciliation does not undo the native result during the round
trip. A replicated Exit result for a remote human who is still seated waits
briefly for its reliable event so `Replay` can play the retail exit.

reM `Intern_UseCar` calls `SetEngineOn(true)` for seat 0. After binding a
driver, reconciliation restores the replicated engine state.

Before a network car is removed, every human owner is ejected with
`ForceExitCar`; a door reservation is first completed with `Intern_UseCar`
so the exit also cancels the entry animation. Before a network human is
removed, an unfinished entry is completed to release the vehicle's boarding
lock, or its reservation is cleared with a null `C_car::SetOwner`. A seated
human remains in the car until queued native removal calls
`C_game::InvalidateActor`, which clears the seat owner before `GameDone` and
`Release`. Calling `ForceExitCar` here would place a collidable human next to
the moving car and can produce a collision impulse before removal completes.
The server records life-end and disconnect seat clears as `Cleared`, so client
reconciliation also waits for native actor removal if the seat update arrives
before the player deletion.

## Server-owned human damage and death

Retail `C_game::TickShoot` collision-tests a queued bullet, tests the hit
human's skeleton, and calls that actor's virtual `Hit` with the shooter,
normalized direction, impact point, damage, and body part. reM shows that
car impacts, fall damage, burning, and sniper paths also reach `C_human::Hit`.
That method changes health, limb health, alive/dead state, animation, effects,
and native ownership. Its random death selection makes independent client
execution divergent. A multiplayer hit must therefore be suppressed at the
native human boundary until the server accepts it. Only a collision reported
by the controller of a live server-owned shooter may request firearm damage;
the server validates the shot sequence, target life, geometry, and weapon.

For a network pedestrian struck by a car, reM `C_Vehicle::BodyCollision`
calls `C_car::Collision_Filter_Body` at `0x426340` before its rigid impulse.
The callback is `const g_collision_header* __fastcall(C_car*, const
g_collision_header*, const S_vector& hit, const S_vector& normal)`; a dynamic
collision stores its owner actor at `+0x44`. The car simulation owner skips the
rigid response for a remote network pedestrian above the retail damage speed,
after checking that contact velocity points into the collision normal. It
reports the contact to the server. The server checks the controller, life and
mission generations, seating, server car speed, nearby poses and impact rate
before applying damage. reM `C_human::Collision` uses
`(speed - 2.5) * 4.2` at speed `>= 3`, enters `C_human::Hit` with
`HIT_TYPE_SNIPER`, and uses a side jump at lower speed. Low speed contacts
therefore retain the native filter. A fatal accepted impact publishes the
server death animation to both clients; a nonfatal impact replays the native
Sniper reaction once from its matching state revision.
For a surviving pedestrian, `C_human::Hit` queues a torso pain step and sets
work state 6. `C_human::Movement` owns that step until it returns to normal
movement state 1; remote pose interpolation must keep updating the transform
without replacing the native animation during state 6.

| Hook or call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_human::Hit` `0x5762a0` | `8B 44 24 04 83 EC 34 53 55 33 DB 56 57 8B 7C 24 5C 3B C3 8B F1 75 08 3B` | `bool __thiscall(C_human*, E_hit_type, const S_vector& direction, const S_vector& position, const S_vector& normal, float damage, C_actor* attacker, unsigned bodyPart, I3D_frame*)`, `ret 32`. IDA vtable reference at `0x625774`; reM call sites include the bullet collision path, occupied-car `HitInCar`, human collision, burning and fall paths. It changes health and limb values, death state, animation, drops, blood/sound, police and score state. A client hook returns before those changes for a server-owned human during a mod mission. An authoritative replay calls the original once under a scoped bypass. | Human, attacker and frame are borrowed native objects valid only during their owning mission tick; the hook retains none. It is removed before native mission/system teardown. Generation-checked registry lookup is required before relating a hit to a network entity. |
| `C_player::Hit` `0x594210` | `56 8B F1 8A 86 DA 0A 00 00 84 C0 74 13 56 B9 10 6D 64 00 E8 78 92 FA FF` | `bool __thiscall(C_player*, same eight arguments)`, `ret 32`; its virtual slot overrides `C_human::Hit`. reM shows it checks police collision lock, calls `C_human::Hit`, then updates the retail life indicator and returns true. Suppressing the inner human hit leaves health unchanged; the mod HUD receives server health separately. | Native player is mission-owned or temporary. No pointer survives registry invalidation; no separate hook is needed unless its post-call HUD behavior proves harmful. |
| `C_human::GetAnimIDForDeath` `0x57a630` | `8B 44 24 04 53 56 8B F1 8B 4C 24 10 50 51 8A DA E8 39 56 09 00 D8 1D 90` | `int __fastcall(int bodyPart in ECX, bool crouched in DL, const S_vector& hitDirection, const S_vector& facing)`, two vector references on the stack. IDA calls from `C_human::Hit` at `0x5773cb` and `0x577ff0`, plus two unrelated animation paths at `0x4592af`/`0x459404`. reM shows this method chooses a death animation through `rnd`; it does not mutate the human. A scoped hook may return the server-chosen valid animation while replaying an authorized fatal hit, then delegate normally at all other times. | Function reads only caller-owned vectors and global RNG; it retains no pointer. Remove the hook before native teardown. A fixed server animation must not be applied to an unrelated retail call. |

`C_human::Movement` also has a fatal-fall branch that writes health and
alive state directly, calls `Death`, and sets the dead act state without going
through `Hit`. The guarded Death/actor-state boundary restores the living
state while a generation-bound fatal-fall candidate is checked by the server
against recently accepted vertical movement. `C_human::HitInCar` at
`0x578290` is another specialized path with its own random reactions; the
outer `Hit` delegates to it for seated humans. A seated human's collision is
deactivated by `Intern_UseCar`, so `TickShoot` reaches it only through the
car's hierarchy test and `C_car::Hit`, which forwards its own zero `flags`
as the body part. The hook reports such a hit on a human seated in a network
car with the in-car marker (7); the server accepts the marker only for a
seated target, and the authoritative replay passes zero again.

Further death boundaries needed for the first multiplayer guard:

| Hook or call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_human::Death` `0x570570` | `56 57 8B F1 33 FF 57 89 BE 20 04 00 00 89 BE 24 04 00 00 89 BE 28 04` | Virtual `void __thiscall(C_human*)`; IDA vtable entry `0x62576c` and script call at `0x51b439`. reM calls it from fatal fall, repeated death, car airborne fatality and forced death. It removes shadows/collisions, clears vehicle control and marks the human dead. Suppress for a protected live network human unless executing an authoritative replay. | Live human and mission camera/police resources required; no retained pointer. Remove before game teardown. |
| `C_actor::SetActState` `0x406da0` | `8B 41 1C 83 F8 02 74 1A 8B 54 24 04 3B C2 74 12 A1 8C 78 63 00 8B 40 24` | `void __thiscall(C_actor*, E_act_state)`, `ret 4`; IDA has many actor/script/physics callers including human death paths. State 2 is dead; original changes `m_eActorState` at `+0x1c` and marks the game actor-list state dirty. Guard only state 2 for a protected living human when no server death has arrived; restore the health/alive bytes already modified by the caller. Other actors and states delegate. | Actor and game must be live. Hook is removed before native shutdown and keeps no actor pointer. |
| `C_human::intern_ForceDeath` `0x5878d0` | `83 EC 24 56 57 8B F9 B9 09 00 00 00 E8 6F 0B E8 FF 8D 88 83 00 00 00 89` | `void __thiscall(C_human*)`, plain return. IDA callers at `0x5704e7`, `0x5b295b`, `0x5b4752`; reM creates a random final pose, sets health/alive, closes animation, invokes `Death`, sets dead act state, drops inventory and creates blood. The hook skips the whole call for a protected living human pending server authority. | Only live initialized human; no retained resources in the hook. Remove before mission teardown. |
| `C_human::EineMeineKleineAutoInLuft` `0x58a5a0` | `55 57 8B F9 8B AF 98 00 00 00 85 ED 0F 84 D8 01 00 00 83 7D 10 04 0F 85` | `void __thiscall(C_human*, C_car*)`, `ret 4`; IDA sole caller in `C_car::CarExplosion` at `0x422a8f`. reM shows this function changes the occupant model to burned, zeroes health/alive, invokes Death and sets dead act state before choosing a car death animation. Skip the call for a protected living human pending server authority so those side effects do not precede the server. | Both actors and frame are live during explosion. No pointer is retained. Unhook before car/mission teardown. |
| `C_human::Movement` `0x57a710` | `83 EC 2C 53 55 56 8B F1 57 8B 86 0C 04 00 00 48 83 F8 0A 0F 87 E3 0E 00` | `void __thiscall(C_human*, unsigned frameMs)`, `ret 4`; IDA calls from human movement/tick at `0x572b2b` and `0x572e77`, and from `C_human::Hit`. reM shows the work-state-1 fatal fall branch checks `m_fFallSpeed > 50`, then zeroes health/alive and calls `Death` and `SetActState(DEAD)`. A wrapper calls the original exactly once and marks only its dynamic call scope, so the Death hook can distinguish this direct fall path from other deaths and request server validation. | Human is borrowed for the duration of native tick. The marker is thread-local and cleared before returning. Unhook before native shutdown; no actor pointer is retained. |

Server-accepted nonfatal `DamageEvent` waits for its matching state revision,
then calls the original `C_human::Hit` once under the same scoped bypass with
a bounded reaction damage (at most 5). It restores the authoritative server
health immediately after the call. That call can still choose random blood,
scream and reaction details, so these details are not yet guaranteed equal
between clients. A fatal state calls the original once with `HIT_TYPE_DIRECT`
and enough damage to enter death, while the scoped animation selector returns
the server's ID in retail's valid standing death range 131–136. This is one
chosen death animation for the life; native corpse timing and all visual
effects still require multiplayer verification.

### Selective car damage presentation

The damage applier runs only after the native car has completed `Initialize`
and a generation-checked registry handle resolves in the active mission. It
does not call the native whole-state loader. It copies validated light, zone,
wheel, engine, gearbox, body and fuel-tank values into their typed SDK members.
The current simulation owner reports those native values; the server checks
controller GUID, mission generation, sequence, bounded part counts that remain
fixed after the first accepted report, numeric
bounds and native damage flag masks before advancing a durable revision. The
server replica carries that revision to new clients. Reapplying a revision is
avoided. Native allocations are owned by the car and all borrowed frame/part
pointers are discarded before stream-out, native destruction and unload.

| Retail presentation call | Original evidence | Convention, callers and side effects | Lifetime and unload |
| --- | --- | --- | --- |
| `C_Vehicle::DeformGlass` `0x4d6670` | Entry bytes `53 8B 5C 24 08 56 57 8D 04 5B 8B F1 8B 8E 18 02 00 00 8D 3C 83 C1 E7 02`. IDA xrefs `0x42282d`, `0x4238b2`, `0x424124`, `0x4d0700`, `0x4d60ed`. | `void __thiscall(C_Vehicle*, int zoneIndex, const S_vector& hitPosition)`, `ret 8`. reM shows it replaces cracked glass material when below threshold; at crack level zero it sets BROKEN and calls the car break-glass callback, which may make native glass shards and sound. Only a bounded local zone index is passed. | Requires initialized zone mesh/material and car callbacks. No argument retained. Called only while a live registry handle resolves; there is no hook to unload. |
| `I3D_frame::SetOn` virtual slot `+0x24` | IDA call at `0x4d072a` has `FF 50 24`. The same native `LoadVehicleState` path calls this after a saved non-body zone has BROKEN. | `__stdcall(frame*, bool)` via the loaded LS3DF frame vtable, so DLL relocation is respected. Turning a detached part frame off hides it; the SDK then calls its audited `Update` slot `+0x18`. | Only borrowed live zone/wheel frames from the initialized car; no pointer retained. The vtable is game-owned and no hook is installed. |
| `I3D_mesh_level::GetFGroup` slot `+0x14`, LS3DF preferred `0x10031180` | `8B 54 24 04 8B 42 18 8B 4C 24 08 3B C8 72 14`; retail `C_Vehicle::InitDeform` calls it at `0x4d53d5` after `mesh->GetLOD(0)`. | `I3D_face_group* __stdcall(level*, int index)`, `ret 8`; index zero obtains the borrowed glass face group. The DLL-loaded vtable supplies the relocated target; no pointer persists after the car is released. No hook to unload. |
| `I3D_face_group::SetMaterial` slot `+0x00`, LS3DF preferred `0x10030580` | `56 8B 74 24 0C 57 8B 7C 24 0C 8B 47 04 3B F0 74 13`; retail repair calls it at `0x4d53e6`. | `void __stdcall(group*, I3D_material*)`, `ret 8`; it releases the old material and increments the replacement's reference count. For a repaired glass zone, pass its corresponding original-zone material; a positive crack below threshold instead uses native `DeformGlass` to choose the cracked material. Both materials are car-owned and valid only while initialized. No hook to unload. |

The later mesh checkpoint path carries the retail version-9 packed displaced
vertices in reliable chunks of at most 64 records, with at most 8192 records
per car. The server assembles a complete revision, validates owner, mission,
zone/LOD and duplicate vertex IDs, then publishes the complete checkpoint.
Each client validates its own model mesh vertex counts for every zone/LOD
before any native write, restores the original vertex positions, applies the
packed displacements, and updates render bounds. New clients request the
latest revision after stream-in; incomplete revisions are never applied.
This bypasses `Deform`'s per-vertex random draws, which would make input-only
replay diverge. The whole `LoadVehicleState` path remains unused because it
resets seats and physics. Separate debris actors, exact projectile impact
ownership, complete native repair effects and a two-client mesh parity test
are still open.

### Wheel repair and detached debris lifetime

The selective wheel applier may clear the server-owned BROKEN, DAMAGED and
DESTROYED bits and turn the borrowed wheel frame back on. Retail wheel
collision and visual rotation read those bits again on subsequent ticks, so
this restores the main live paths without resetting the occupied car. It does
not reproduce the complete transient reset performed by native `Reset`:
contact, skid and powered bits, contact face, suspension droop/force and
velocity caches can persist until the next wheel physics update. Neither a
whole-vehicle `Reset` nor `C_car::_RepairPosition` is safe as a selective
network repair; both also reset drivetrain/control state and the latter
deactivates and repositions the car. A repaired wheel's first physics tick
still needs a two-client behavior check.

| Retail path | Original bytes and IDA callers | Convention, effects, lifetime and unload |
| --- | --- | --- |
| `C_Vehicle::Reset` `0x4c3860` | `83 EC 60 53 55 8B E9 56 57 8B 85 4C 03 00 00 50 8B 08 FF 51 18`; IDA caller in the car reset path at `0x422c14`. | `void __thiscall(C_Vehicle*, float speed, bool initialize)`; reM shows it clears wheel transient flags, collision face, cached forces/velocities, gear, steering and horn. Only `initialize=true` clears DESTROYED/SPARE, turns the frame on and invokes `InitDamage` and `InitDeform(false)`. Requires a fully initialized car; no hook or retained pointer. The SDK deliberately does not call it on a live network car. |
| `C_car::_RepairPosition` `0x41e690` | `83 EC 34 56 8B F1 57 6A 00 8D 46 24 6A FF C7 44 24 14 00 00 00 00`; reM script `CAR_REPAIR` path. | `void __thiscall(C_car*, bool resetCar)`, `ret 4`; deactivates, resets and repositions the car before re-enabling the engine. This would disturb seats and motion. It is not called by network repair, has no hook, and is valid only while the game owns the actor. |
| `C_car::SetTransparency` `0x4233e0` | `6A FF 68 98 DC 61 00 64 A1 00 00 00 00 50`; reM car path enumerates `ENUMF_VISUAL` children and passes the argument to each object/single-mesh visual. reM's `I3D_object` render path multiplies material alpha by this value; racing recovery fades from `0.2` to `1`. | `void __thiscall(C_car*, float opacity)`; despite the native name, `0` is invisible and `1` is opaque. `Vehicle.setOpacity(opacity)` passes the value directly. It skips already broken deform zones and destroyed wheels, and clients reapply it when a damage revision changes. The call borrows the initialized car and visual pointers for its duration only. |
| `C_Vehicle::InitDamage` `0x4d54e0` | `53 56 8B F1 57 33 FF 8B 8E F0 01 00 00`; retail `Reset(true)` calls it, and reM shows it restores engine/gearbox/wheel health and light/projector state. | `void __thiscall(C_Vehicle*)`; safe only after the car's native `GameInit`. It does not change seats, pose or speed. It does not clear `m_uDamageFlags`, `m_bEngineDestroyed`, or persistent wheel flags, so live repair clears those separately. No hook or retained pointer. |
| `C_Vehicle::m_uDamageFlags` `+0xf8` | Retail `DamageVehicle` at `0x4d6733` reads `[ecx+0xf8]` before its engine/gearbox one-shot damage checks; reM `GameInit` clears the field. | Clearing this field after `InitDamage` permits subsequent engine/gearbox damage. The SDK writes only its proven 32-bit offset on an initialized vehicle; no pointer persists. |
| `C_Vehicle::InitDeform` `0x4d51f0` | `83 EC 10 53 56 8B F1 8B 86 18 02 00 00 85 C0 0F 84 C0 02 00 00`; IDA callers at `0x4c3ea1`, `0x4c54cf`, `0x4c57ef`. | `void __thiscall(C_Vehicle*, bool initialize)`, `ret 4`; for `false`, restores all original zone mesh positions/materials, shows every zone frame, clears zone flags and invokes the car repair callback to remove scratches/fire holes. It operates on the whole initialized car, so cannot repair just one server zone while preserving other dents. No hook or retained pointer. |
| `C_car::Prepare_DropOut[_Wheel]` `0x426ec0` / `0x426dd0` and `Drop_Out` `0x427010` | Entries `83 EC 24 8D 04 52 56 8B B1 88 02 00 00` / `83 EC 2C 8B 81 A8 0C 00 00 56 57` / `6A FF 68 BB DC 61 00 64 A1 00 00 00 00`; IDA callers include native explosion `0x421d60` and collision `0x423600`. | `bool __fastcall(C_car*, int partIndex, const S_vector& velocity, const S_vector* extra)` for either prepare method; `bool __fastcall(C_car*, I3D_frame*, void* parameters, int type, int index)` for `Drop_Out`. They duplicate the part into a new temporary `C_DropOut` actor, register it with the game, then mark the source part destroyed/broken and hide its frame. The actor has its own lifetime and is not a member of the car's durable zone/wheel arrays. The mod's `Drop_Out` hook delegates once for the local controller, suppresses an unowned network-car detachment, or delegates under an authoritative replay scope. Its parameter/frame pointers are borrowed only during the call. Disable and remove the hook before native system teardown. |
| `C_game::AddTemporaryActor` `0x5a77c0` | `51 53 55 56 57 8B F9 8B 4C 24 18 BB 04 00 00 00`; IDA callers include the `Drop_Out` registration at `0x4270ea` and many unrelated actor spawn paths. | `void __thiscall(C_game*, C_actor*)`, `ret 4`; checks duplicate temporary actor pointers, invokes `GameInit`, and inserts the actor into the active/inactive and temporary arrays. The hook captures the new type-17 `C_DropOut` only while a thread-local `Drop_Out` scope is active. The game owns it after registration; no independent delete is allowed. The hook delegates once and is removed before system teardown. |
| `C_game::RemoveTemporaryActor` `0x5a79a0`, `_RemoveTemporaryActor` `0x5a7ec0` | `8B 91 34 01 00 00 83 EC 08 85 D2 56 8D B1 30 01 00 00` / `8B 91 24 01 00 00 53 56 57 85 D2 74 7E`; IDA removal callers span actor updates, script paths and game cleanup. | `void __thiscall(C_game*, C_actor*)`, `ret 4` for either. The public method queues a dead actor; the internal method removes it from game arrays, invalidates native references, calls `GameDone`, then releases it. A network debris cleanup may request removal only for its own generation-checked live `C_DropOut`, and must never free the actor itself. The hook/SDK must not retain the pointer after the base actor destructor. |
| `C_DropOut::ChangeState` `0x443dc0`, `GameDone` `0x443d30` | `53 56 57 8B D9 E8 F6 6F 01 00 8B 43 1C` / `56 8B F1 E8 F8 68 01 00 F6 46 04 03`; virtual state slot at `0x623d2c`. | `void __thiscall(C_DropOut*)` for both. The temporary actor becomes inactive or dead independently; on death it asks `C_game::RemoveTemporaryActor`, and `GameDone` releases its duplicated frame. A car repair does not identify or remove the corresponding loose actor. No mod hook or retained pointer exists. |

The current durable zone/wheel flags and mesh checkpoint restore the attached
car's final appearance for a late join. The separate debris replica now
tracks each accepted native `C_DropOut` by its network car, mission generation,
part kind/index and unique network ID; clients recreate it through the audited
native `Drop_Out` path and the controller reports its pose until native
removal. Server bounds, controller ownership, car proximity, sequence and
mission are checked before creation. This is a first loose-actor lifecycle
path, not proof of matching transient physics under jitter or packet loss.
The SDK now restores the original glass face-group material from the matching
original deform zone when a pane is repaired to or above its crack threshold;
for a damaged checkpoint it applies the cracked material without replaying the
break callback. That selective material operation leaves other dented zones
intact. Broken-glass shards and scratches already emitted by retail remain
separate effects and are not undone by the material change.
The loose actor is removed when its native owner ends it, the car despawns or
the mission changes. A server repair that reattaches the source part does not
yet destroy already detached debris; that requires a specific server repair
transition and a native removal rule. The collision-recording dropout vector
is for replay tracks, may be inactive in live play, and cannot serve as the
actor identity mapping.

### Car water, fall volume and occupant death

Retail has no sink routine. reM `C_car::Collision_Filter_Body` on material 31
creates particle 37, plays sound 236 and calls `WFall_Player` for the local
driver. The stock mission program that handles that fall event is suppressed
in a server-managed mission. The hook runs the original callback first, then
reports the same native body contact to the server from the current simulation
controller. Material 40 reports the fall-volume outcome separately. A falling
speed below -85 is the retail invalid-vehicle condition, not proof of a map
boundary; it reports the existing terminal state 3 when no earlier collision
was seen. The server commits the terminal state, fires `vehicleTerminal`, and
kills current-generation occupants through combat. The native in-car death
path plays their seat death animation. The sample gamemode fades on local
`playerDeath`, respawns through its server death handler, and explicitly
destroys terminal cars later. The native simulation controller continues
physics after water contact and observers receive that pose; no server-authored
descent or automatic sink-despawn runs. The local camera is locked at its
pre-death pose when a seated player enters water and is restored after retail
`C_human::Death` selects its default fatal camera. A respawn or mission close
restores ordinary camera control.

On foot, `C_human::HandleFatalMovementCollision` already supplies its own
particle, sound and fixed fatal camera mode. The existing drowning report
confirms death on the server; the gamemode chooses fade and respawn. Before
terminal state, the act-state hook suppresses native state-2 requests for
tracked network cars. `RemoveTemporaryActor` can still despawn a deactivated
car when the gamemode destroys it.

A seated player's authoritative death is replayed as a `Generic` hit:
`C_human::Hit` routes a seated human to `HitInCar`, which ignores `Direct`
damage and picks the retail driver, passenger or exit death animation itself.
Seat reconciliation never force-exits a dead occupant; the body stays in the
seat, as in retail, until the dead human is removed at respawn.

### HUD overlay: chat panel and nametags

The chat and nametags draw in the scene callback (`0x5bd8d0`, message 2)
after the original, with the retail HUD font and untextured quads.

| Native call | Evidence and convention | Ownership and lifetime |
| --- | --- | --- |
| `G_IndicatorsClass::OutText` `0x603880` | `float __thiscall(indicators, unsigned char* text, float x, float y, float width, float height, unsigned color, unsigned options, unsigned font, unsigned char* end)`. Screen pixels, color `0xAARRGGBB`. Options per reM: shadow 1, centered 2, right 4, vertical 8, scale to fit 0x10. The built-in shadow leaves a stray underline after `DX_ALPHASTATE` is selected, so the mod draws its own offset shadow pass. | Text is read during the call only. |
| `G_IndicatorsClass::TextSize` `0x6036d0` | `float __thiscall(indicators, text, float characterHeight, unsigned font, unsigned char* end)`, `ret 0x10`. `OutText` measures with the line height times the font's character height scale; font definitions start the indicators object, `0x101c` bytes each (`0x6036eb`–`0x603704`), with the height scale at `+0x08`, width scale `+0x0c` and spacing `+0x14`. | Pure query. |
| `IGraph` singleton `0x647ee0`: `SetTexture` `+0x0c`, `SetState` `+0x44`, `DrawPrimitiveList` `+0x58` | `G_IndicatorsClass::DrawAll` `0x5fb060` calls `SetState(DX_ALPHASTATE, 1)` at `0x5fb079`, and draws the widescreen bars with `SetTexture(0)` then `DrawPrimitiveList(3 = triangle list, primitives, TLVERTEX*, 1 = TL stream)` at `0x5fb50a`–`0x5fb529`. reM `TLVERTEX` is `0x20` bytes (xyz, rhw, diffuse, specular, uv). | Vertices live on the caller's stack for the call. |
| `I3D_scene::TransformPoints`, scene vtable `+0x74` (LS3DF `0x10049e60`) | Verified in `LS3DF.dll`'s scene vtable next to `AddFrame` `+0x5c`. Projects through the active camera's clip matrix to screen pixels and writes `w = 1 / clip w`; a point behind the camera has non-positive `w`. | Pure query on the live mission scene. |
| `I3D_frame::FindChildFrame`, frame vtable `+0x38` | 45 retail calls in `0x570000`–`0x590000` pass `ENUMF_ALL` (`0xffff`) through this slot. Nametags look up the `"neck"` bone that `C_human` binds its aim pose to. | The bone belongs to the human's model; the mod caches it per registry generation, so a replaced human is looked up again. |
| `I3D_frame::GetWorldMatrix` `0x47acd0` | `test byte [frame+0xac], 0x20; jne; call 0x60fc30; lea eax, [frame+0x10]`. Rebuilds a dirty world matrix through its parents before returning it. Calling `I3D_frame::Update` alone left the cached translation at zero for the `emeth_*` Free Ride start dummies; `GetWorldMatrix` returned their real coordinates. | All SDK world position and direction reads use this call, including the selector's start frames, the neck and the camera. A ped seated in a moving car therefore does not expose last frame's matrix. |
| `g_collision::TestLineHStatic` `0x5c74d0` | `__thiscall(0x647f48, const S_vector& start, const S_vector& direction, S_vector* hit, S_vector* normal, unsigned flags)`, `ret 0x14`; police line of sight calls it with null outputs and flags 0. Tests the static world only. | Hides a nametag behind walls. |

### Melee

| Native call | Evidence and convention | Ownership and lifetime |
| --- | --- | --- |
| `C_human::Do_Shoot` `0x583590` | `53 56 8B F1 33 DB 39 9E F8 06 00 00 0F 85 4B 09 00 00`; `bool __thiscall(C_human*, bool pressed, const S_vector* target)`, `ret 8`. `C_player::AI` calls it every frame with the fire state. For melee item types 0/5/6/7 (`+0x1e8`) a press stamps `m_iMeleeAttackTime` (`+0x220`) and advances the combo index (`+0x228`, reset after 1200 ms); a release plays a quick attack (hold 500 ms or less) or a heavy finisher (index becomes -1). reM asserts `m_bMeleeActive` `+0x1ec`, `m_iWeaponChangeTime` `+0x234`, `m_iCanWork` `+0x40c`. | The hook sends the local press and a measured release; a cancelled press sends a cancel. On the remote human, the client calls the original for press and held frames, then seeds the accepted combo and press duration before calling the original for release. The bat windup therefore starts during the hold, before the swing. No pointer is retained. |
| `C_human::Can_SpecialStroke` `0x584010` | `83 EC 40 53 8B D9 8B 0D BC D2 63 00 55 56 57 85 C9`; `C_human* __fastcall(C_human*, bool)`, plain `ret`. The boolean is the second register argument, with no stack argument or dummy `edx` parameter. A found target receives `Do_HardHead`/`Do_Castrate`, which subtract health directly and bypass `C_human::Hit`. | Hooked to return null in every mod mission, so the heavy fist attack is the plain finisher on every client and health stays server-owned. Matching the native register signature prevents a corrupt return on combo swings. |
| Melee hit | `C_human::AI` `0x570740` sweeps the swing points after the strike note and calls the victim's `Hit` at `0x5714ca` with hit type 1, the swing segment, body part `rnd(2)+5` and the retail per-item damage (`0x570df7`–`0x570e42`: fists 4, knuckleduster 7, knife 6, bat 10; a heavy blow doubles it; a heavy knife or bat blow from behind is lethal). | The attacker's suppressed hit becomes a Melee HitReport. The server accepts one hit per attack within 1.8 s, within 2.6 units, in front of the attacker, and applies the retail damage itself. |
| `C_game::m_uGameTime` `game+0x2b0c` | reM asserts the offset. | Read to time a replayed melee release. |

### Grenades and Molotovs

The retail `C_human::Do_Shoot` (`0x583590`) handles throwable item type 4.
The first `true` call starts the `hod vrchem mix75.i3d` windup and stores game
time at `+0x218`; held calls refresh the shoot target. A `false` call clears
the active flag at `+0x21c`: charges of 400 ms or less cancel, while longer
charges cap at 2000 ms, scale the throw impulse, and select animation state
`HUMAN_ANIM_THROW_GRENADE` (163). Animation notify 41 later calls
`C_game::NewGrenade`. The local hook sends start, release or cancel. After
server validation, remote humans replay press, held frames and release once
per accepted throw. The remote animation notify still removes the thrown item
and updates the weapon model, but its `NewGrenade` call is suppressed based on
the notify's actual human owner. The accepted `Throw` event owns the single
display projectile, and the server's detonation removes it. Owner attribution
also keeps nearby remote throws from being mistaken for local throws.

The same notify removes the entire selected inventory item, even when its
`ammoLoaded` field is greater than one. The network inventory represents a
stack of grenades with one item ID and a quantity in `ammoLoaded`: after an
accepted throw, the server decrements that quantity and selects empty hands.
Any remaining quantity is reconciled into a pocket, ready for the player to
select again. Native item consumption is never interpreted as a weapon drop;
the separate `DropOutItems` hook reports actual player drops. Until the remote
notify finishes, inventory reconciliation waits so the server's empty-hand
state cannot cancel the pending native release animation.

| Native call | Evidence and convention | Ownership and lifetime |
| --- | --- | --- |
| `C_game::NewGrenade` hook | `C_grenade* __thiscall(C_game*, int type, S_vector& position, S_vector& impulse, bool activate, I3D_frame*)`, `ret 0x14`. The item selects type 0 for the grenade (15, 5000 ms fuse) and 3 for the Molotov (bursts on contact). | The notify owner identifies a local grenade before it is reported; the local projectile pointer is kept until its destructor hook. For a replicated remote human, the hook releases the duplicated model and returns null. The notify ignores the return value. |
| `C_grenade::AI` `0x4452c0` | Vtable `0x623d70` slot 13, `81 EC 98 00 00 00 55 56 57`, `void __thiscall(C_grenade*, unsigned)`, `ret 4`. Detonates through `NewExplosion` (`0x5aae10`, `ret 0x20`, called as `(this, frame position, 15, 400, true, false, true, 18)`) or `NewSmallFires` (`0x5abb90`, `ret 8`) and `NewFire` (`0x5abe10`, `ret 0x18`, 5000 ms, 2.5, 50). | A thread-local marks the ticking grenade so the explosion or fire hook can report the detonation of a local throw. |
| `C_grenade` flags | reM asserts the inherited `C_bottle::m_bIgnoreDynamicCollision` `+0x134`, `m_bTimedDetonation` `+0x138`, `m_iDetonationTime` `+0x13c`, `m_bDetonateOnCollision` `+0x140`, size `0x160`. | A remote throw's display copy has both detonation triggers cleared and ignores dynamic actors, so an interpolated remote ped cannot deflect it at launch. The server's detonation removes the display copy. |
| Scalar deleting destructor `0x445290` | Vtable slot 16, `__thiscall(C_grenade*, unsigned flags)`, `ret 4`; calls `0x4452b0`. | Drops the grenade from the local and remote maps before native destruction. |

The thrower's native explosion or fire decides where it goes off. The server
checks the report against the accepted throw (fuse window, flight bounded by
the throw speed and gravity) and applies retail damage from its own player
positions: explosions `400 x (1 - d / 15)`, fires `50 x remaining seconds`
within 2.5 units every 500 ms. Seated players take neither, as `HitInCar`
ignores both hit types. Other clients replay the native explosion or fire;
their human hits stay suppressed, and car hits run only on each car's
simulation controller, so every car is damaged once. A throw with no report
within 8 s is dropped without damage.

### Weapon world items

| Native call | Evidence and convention | Ownership and lifetime |
| --- | --- | --- |
| `C_human::DropOutItems` `0x57faa0` | `6A FF 68 D3 05 62 00 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 64`; `bool __thiscall(C_human*, std::vector<S_GameItem>*)`, `ret 4`. Every retail drop goes through it: `Do_WeaponDrop` (Backspace, death through `C_human::Hit` and `intern_ForceDeath`), holstering overflow, car entry and scripts. It opens the item model and registers a `C_drop_in_weapon`. | Hooked in a mod mission to create nothing and return true, so `Do_WeaponDrop` still changes the weapon; the server spawns the pickup from its own inventory. |
| `C_using_object` registry `game+0x29ec` | `Do_AB_OwnerNULL` passes it at `0x5947b2`. `AddObject` `0x55dd60` and `DelObject` `0x55df10` are `__thiscall(registry, S_using_object*)`, `ret 4`. reM asserts `S_using_object` size `0x38` with the item record at `+0x28`; `S_GameItem` is `0x10` bytes with its back pointer at `+0x0c`. A `FRAME_BOUND` (2) record's frame is invalidated and released by `DelObject`; without `OWNED` (1) the game never frees the record. | Each replicated pickup owns one record (stable heap address) and one model frame. `DelObject` runs when the pickup disappears and in `NotifyMissionClosing`, before `C_game::Done`'s `ClearObjects`. |
| `C_using_object::FindNearObjects` `0x55e180` | `6A FF 68 B8 02 62 00 64 A1 00 00 00 00 50 64 89 25`; `__thiscall(registry, const S_vector&, const S_vector&, std::vector<S_GameItem>*, I3D_frame*)`, `ret 0x10`; sole caller `0x5947b8`, results nearest first. | The hook empties the list when the nearest item is a replicated pickup and sends a request instead, so the retail menu never takes it locally; otherwise it removes replicated items from the list. |
| `g_pItems` `0x6d4c14` | `DropOutItems` reads it at `0x57fb82`; records are `0xbc` bytes (reM) and the model name is inline at `+0x24` (`lea ebp,[eax+0x24]` at `0x57fba3`). | Read-only. |
| `I3D_scene::SetFrameSectorPos` scene vtable `+0xbc` | `DropOutItems` files the dropped model into its sector through this slot at `0x57ff66`. | Called once when a pickup model is placed. |

## Web UI presentation and input (CEF over Direct3D 8)

The CEF front end (`docs/ui.md`) borrows the engine's Direct3D 8 device and
composites one off-screen view into the finished frame. All LS3DF addresses
below are preferred-base evidence from the installed `LS3DF.dll` (image base
`0x10000000`, SHA-256
`e211c85009440d6c032f2074d79184f074a9a046574648745c2594b4cef178a6`). At
runtime they come from the loaded module base or from the live IGraph vtable
(`0x1009d500`, read through the `0x647ee0` singleton), never from the
preferred address. That vtable was dumped from the file, and every slot used
here matches reM's `IGraph` declaration order.

| Hook or call | Original retail bytes | Convention, callers and effects | Lifetime and unload |
| --- | --- | --- | --- |
| `IGraph::Present` vtable `+0x3c`, LS3DF `0x1006d4c0` | `83 EC 34 53 56 E8 76 EE 00 00 A0 CC 59 1C 10 84 C0 0F 84 86 00 00 00 FF` | `void __stdcall(IGraph*)`, `ret 4` (at `0x1006d631`). The menu loop (`GM_Menu::ExecuteMenu`, after `Scene::Render`) and the game loops call it once per frame. It ticks the sound stream, optionally draws the FPS overlay in its own `BeginScene`/`EndScene` pair, then calls `IDirect3DDevice8::Present` and handles `D3DERR_DEVICELOST` by `TestCooperativeLevel`/`ResetDevice`. The hook draws nothing unless the web UI is active, the device reports ready and the page has acknowledged the current screen. When all three hold, it opens its own `BeginScene`/`EndScene` pair, draws the view quad inside a `D3DSBT_ALL` state block, restores that block. It always calls the original. The first bytes contain a rel32 call, which MinHook relocates. | The hook target is read from the live vtable. The hook is installed in `WebUiService::Install` after CEF starts and removed in `Application::PreShutdown`, before the framework shuts CEF down and before `CloseSystem`. It keeps no device or graph pointer. |
| `g_pD3DDevice`, LS3DF `0x101c597c` | `IGraph::BeginScene` `0x1006d470`: `A0 B3 59 1C 10 84 C0 75 18 A1 7C 59 1C 10 ...` then `jmp [ecx+0x88]` | The engine's `IDirect3DDevice8*`, created by `IGraph::Init` (`CreateDevice` with mixed or software vertex processing and `D3DCREATE_MULTITHREADED`, not a pure device, so state blocks are allowed). `BeginScene`/`EndScene` jump through device slots `0x88`/`0x8c` (34/35), which matches the DirectX 8.1 declaration order the framework binding (`d3d8_api.h`) uses. The mod uses slots 3, 16, 20, 34, 35, 40, 50, 54, 56, 57, 61, 63, 72, 76 and 88. | Read once when the Framework instance initializes (first mission tick), after `InitSystem` created it. `Reset` keeps the object, and the view texture is `D3DPOOL_MANAGED`, so nothing is recreated after a lost device. `IGraph::Close` releases the device after `CloseSystem`, when every view is gone. |
| `IGraph::GetMainHWND` vtable `+0x10`, LS3DF `0x1006c800` | `A1 58 54 1C 10 C2 04 00` | Returns `g_mainHWND` (`0x101c5458`), the window passed to `CreateDevice` in windowed mode. The mod subclasses it with `SetWindowLongPtrA(GWLP_WNDPROC)`. The subclass passes every message on unchanged to the previous procedure (LS3DF `MsgProc`, itself hooked for `WM_CLOSE`). It is the single source of in-game UI toggles: a fresh `WM_KEYDOWN` (previous-state bit 30 clear, and not already down in its own per-scan-code state) of T, `/` or Esc opens the chat or toggles the pause screen, and that key stays latched, so its repeats, `WM_CHAR` and key-up are swallowed. While the web UI owns the keyboard, it forwards `WM_KEYDOWN/UP`, `WM_SYSKEYDOWN/UP` and `WM_CHAR` to the focused CEF view. `WM_ACTIVATE`/`WM_KILLFOCUS` deactivation releases every key held in the view, clears the key state and latches, and closes the chat and pause screen. | Removed in `PreShutdown`, and only if the window procedure is still ours. If another subclass chained on top, ours stays in the chain as a pass-through with no service. |
| `IGraph::Scrn_sx`/`Scrn_sy` `+0x70/+0x74`, `IGraph::TestKey` `+0xac` (`0x10071a30`), `Mouse_rz` `+0xe8` (`0x10071fb0`), `GetMouseButtons` `+0xf8` (`0x100720c0`) | `A0 E8 55 1C 10 84 C0 A1 DC 55 1C 10` / `0F B6 44 24 08 8A 80 00 53 1C 10 C0` / `A1 F8 56 1C 10 C2 04 00` / `8B 0D FC 56 1C 10 33 C0 84 C9 79 05` | `__stdcall(IGraph*[, uint8_t dik])` queries. `TestKey` reads the DirectInput state byte and works in both keyboard modes (the menu loop uses it for held-key repeat). `Mouse_rz` is `DIMOUSESTATE::lZ` for the frame. `GetMouseButtons` returns bit 1 left, 2 right, 4 middle. They are used for the F1 player list and menu wheel and click forwarding. | Pure reads of LS3DF globals refreshed by the loop's `UpdateKeyboardData`/`UpdateMouseData`. They are called only on the game thread while the graph is alive. |
| `IGraph::KeyboardInit` `+0x8c` and `g_keyboardFlags` LS3DF `0x101c52bc` | `KeyboardInit` `0x100716a0` stores its argument with `89 0D BC 52 1C 10` and selects `DISCL_FOREGROUND \| DISCL_EXCLUSIVE` for bit 0, else `DISCL_NONEXCLUSIVE` (reM `IGraph.cpp`). `ReadKey` `0x10071960` begins `F6 05 BC 52 1C 10 02` | COM-style `int __stdcall(IGraph*, uint32_t flags)`. `InitSystem` calls `KeyboardInit(1)`. Under Wine an exclusive keyboard swallows `WM_KEYDOWN`/`WM_CHAR`, and the web UI takes every page key and every in-game toggle from those messages. So while the page is active, the service's update clears bit 0 once (`KeyboardInit(flags & ~1)`) and never enables buffered mode: `ReadKey` keeps its DirectInput edge semantics for the game, K and F10. | The call recreates the DirectInput keyboard, so it runs on the game thread between frames, never inside `ReadKey`. The original flags are restored when the page deactivates or the web UI shuts down. |
| `G_IndicatorsClass::DrawCursor` `0x604770` | `81 EC 80 00 00 00 D9 84 24 84 00 00 00 D8 05 A4 34 62 00 8B 84 24 84 00` | `void __thiscall(indicators*, float x, float y)`, `ret 8`. Its only caller is `GM_Menu::Draw` at `0x5ea8a8` (reM `GM_Menu.cpp`: it draws for the active menu at `g_iMenuCursorDrawX/Y`). The hook skips the draw only while `Menu::SetNativeControlsSuppressed(true)` is in force, because the page draws its own cursor then. | Created with the other menu hooks before `MH_EnableHook(MH_ALL_HOOKS)` and removed in `UninstallMenuHooks`. It retains nothing. |
| `g_iMenuMouseX/Y` `0x6bd8a0/0x6bd8a4` | Read and written by `GM_Menu::Tick` `0x5ea8c0`: `A1 A0 D8 6B 00` at `0x5ea8f3`, `8B 1D A4 D8 6B 00` at `0x5ea8fd`, `A3 A0 D8 6B 00` at `0x5ea90e` | The menu integrates `Mouse_rx/ry` into these ints and clamps them to `Scrn_sx/sy`. The web UI reads them in the Present hook, after the menu tick, and forwards them as CEF mouse moves. | Game statics. Read only while `g_pActiveMenu` (`0x6bd890`) is the main menu. |
| Main menu suppression through component draw bit `0x10` | See "Retail main menu under the web UI". | While the page is active every retail main menu component is hidden, so it is neither drawn nor hit-tested, and `OnClick` ignores every item. The menu scene, camera and loop keep running. | Reapplied on `OnCreate`. Lifting the suppression (page stops answering, web UI shutdown) restores exactly the components it hid. |

## Script world effects and client scripting natives

Every Game.exe address below was checked against the retail image; LS3DF
methods are called through the live object's vtable, with the preferred-base
implementation given as evidence only. All are called on the game thread from
the application's per-frame update, and only while a mission is loaded
(`WorldService::IsReady`).

### Weather, frames and city music

| Native | Evidence and convention | Ownership and lifetime |
| --- | --- | --- |
| `I3D_scene::SetWeatherSystemParam` scene vtable `+0xe8`, LS3DF `0x1004bce0` | `8B 44 24 08 81 EC D8 00`; `void __stdcall(scene, WS_PARAM, unsigned)`, `ret 0xc`. `WEATHER_SETPARAM` calls it at `0x4743c6`. Floats travel as bit patterns; ids 0-14 follow the name table at `0x624240` (ON, SECTORS, DUMMIES, COLORH, COLORL, SPEED, LEN, WIDTH, MAX_DIST, MAX_HEIGHT, MAX_CNT, DIR_X/Y/Z, MODE). | Weather lives in the one persistent scene. `C_mission::Open` resets it, turns it off and applies `scene2.bin`, so it is reapplied after every load. |
| `I3D_scene::GetWeatherSystemParam` `+0xec`, LS3DF `0x1004c4d0` | `8B 44 24 08 83 F8 0F 0F`; `unsigned __stdcall(scene, WS_PARAM)`, `ret 8`; read by `OsefujPocasi` at `0x5b9571`. Returns exactly what Set takes (reM `I3D_scene.cpp`), so the values read after the load restore `default`. | Read once per mission generation. |
| `I3D_scene::WeatherSystemReset` `+0xf0`, LS3DF `0x1004b3b0` | `83 EC 58 53 56 8B 74 24`; `void __stdcall(scene)`, `ret 4`; `WEATHER_RESET` calls it at `0x474375`. Seeds the rain preset, or with MODE (flag `0x40`) the slow short wide preset used for snow; keeps ON, sets SECTORS and DUMMIES, 1000 particles, empties the particle list. | The client clears DUMMIES afterwards, because most missions have no weather volumes. |
| `C_game::OsefujPocasi` `0x5b9540` | `56 8B F1 57 8A 46 40 84`; `void __thiscall(C_game*, bool save, WS_PARAM)`, `ret 8`. With save it stores the scene's ON in `C_game+0x3630` or MAX_CNT in `+0x3634` (`0x5b9598`, `0x5b9577`), then reapplies both scaled by actor detail. `WEATHER_SETPARAM` calls it after ON and MAX_CNT. | Called after every ON or MAX_CNT write so the detail scaling and `C_mission::UpdateSettings` keep the script's weather. `+0x3630/+0x3634` hold `C_game::Init`'s unscaled mission values until the first save and restore `default`. |
| `C_game::CityMusicPaused` `0x5ae1b0` | `8A 44 24 04 84 C0 88 81 C0 2F 00 00`; `void __thiscall(C_game*, bool)`, `ret 4`; `CITYMUSIC_OFF/ON` push 1/0 at `0x477456/0x47745a`. Pausing fades the current `city_music` show out over 5 s. | `C_game::Init` clears the flag, so a disabled city music is paused again after every load. |
| `C_game::SetNightMode` `0x47b590` | `void __thiscall(C_game*, bool)`, `ret 4`; reM shows it writes `m_bNightMode` at `+0x2d44`, the same field `GAME_NIGHTMISSION` writes in `C_program_process_000_132.inl`. The flag affects native night behavior such as car lights and rail lights, not sky or mission time. | The client records the mission's flag before applying the replicated override; clearing the override restores that value. A late joiner gets the same server override. |
| `C_game::SetTrafficVisible` `0x5a8470` | `SETCITYTRAFFICVISIBLE` in `C_program_process_133_264.inl` calls this with the script's boolean. It switches traffic generators, traffic car simulation and police freeze together. | The multiplayer world disables stock traffic after native game initialization because its randomized NPC simulation is not replicated. No scripting API enables it. |
| `I3D_scene::FindFrame` `+0x58`, LS3DF `0x10049210` | `FINDFRAME` passes flags `0x4ffff` at `0x46d927` (all types, primary plus backdrop sector). | Raw pointer without a reference. Frames are looked up by name on every change and never stored, because a breakable frame can be released mid-mission. |
| `I3D_frame::SetOn` frame vtable `+0x24`, LS3DF `0x1001b400` | `8A 44 24 08 84 C0 8B 44 24 04 8B 88 AC 00 00 00`: bit 0 of the flags at `+0xac` (`FRMFLAGS_ON`), which the client reads to remember the scene's own state. A sound frame (type 4) uses `I3D_sound::SetOn(on, true)` `+0x88` instead, as `FRM_SETON` does at `0x46dcb2`. | The remembered states are restored on `World.resetFrames` and forgotten at mission close; the next load reopens `scene.i3d`. |
| `I3D_object::SetTransparency` vtable `+0x84`, LS3DF `0x10037dd0` | The original I3D_object vtable at `0x1009c348` contains this method at slot 33. `FRM_SETALPHA` in reM `C_program_process_265_397.inl` accepts only `FRAME_VISUAL` of type lit object, billboard, morph, singlemesh, singlemorph, lens flare or object, clamps alpha to 0–1, then calls it. The current scalar is `I3D_object+0x214`. | `World.setFrameOpacity` uses the same type gate and virtual call on a named scene frame. It remembers the original scalar for `World.resetFrameOpacity`, re-resolves the name on each change, and clears overrides at mission change. |

### Sounds

| Native | Evidence and convention | Ownership and lifetime |
| --- | --- | --- |
| `I3D_driver::CreateFrame(FRAME_SOUND = 4)` driver vtable `+0x50` | `Play3DSound` pushes 4 and calls `[ecx+0x50]` at `0x5aca81`. | The caller owns the reference. |
| `C_I3D_sound_cache::Open` `0x408d90` on `sSoundCache` `0x647da8` | `6A FF 68 28 DA 61 00 64 A1`; `int __thiscall(cache, I3D_sound*, const char*, unsigned, void*, void*, void*)`, `ret 0x18`; 0 is success. It tries `"Sounds\\" + name` in the directory and archives. | The cache keeps its own copy per file and is cleared at mission close; the voice is a separate object. |
| `I3D_sound` vtable (LS3DF `0x1009ce78`) | All `__stdcall`: `+0x00` Release, `+0x18` Update, `+0x24` SetOn(bool), `+0x2c` LinkTo, `+0x54` IsPlaying (`ret 8`), `+0x58` SetSoundType, `+0x5c` SetRange(min, max, 0.4, 0.3) (`ret 0x14`), `+0x60` SetCone, `+0x64` SetOutVol, `+0x68` SetVolume, `+0x6c` SetLoop, `+0x88` SetOn(on, update) (`ret 0xc`). Proven by `Play3DSound`'s calls `0x5acab4`..`0x5acb7d`, `PLAYSOUNDEX` at `0x47213e/0x472148` and `SOUND_SETVOLUME` at `0x477751`. | A replicated sound is built like `C_fire`'s loop: create, open, position, link to the primary sector and file into its sector (scene `+0xbc`), point type, range `radius/2..radius`, omnidirectional, loop, update, on. It is released with SetOn(false, true), LinkTo(null), Release when its replica goes, is disabled, changes wave or radius, its mission generation stops matching, and in the mission-closing callback before `C_mission::Close`. No pointer outlives that. |
| `C_game::Play3DSound` `0x5aca70` | `A1 D8 7E 64 00 83 EC 18 56 57 8B F9`; `int __thiscall(C_game*, I3D_frame* parent, const char*, I3D_SOUNDTYPE, S_vector by value, float min, float max, float volume, bool fadeIn, float fadeSpeed, bool loop)`, `ret 0x30`; -1 on failure. | The game owns the sound: `C_game::Tick` releases it once it stops and `C_game::ClearEffects` releases the rest at `Done`. Used for the `PlaySound` RPC and local script sounds (point type 1; ambient type 3 with range 20..20 at the camera, as `ENDOFMISSION` does). |
| `C_game::Stop3DSound` `0x5acc00` | `8B 81 F8 02 00 00 53 56 57`; `void __thiscall(C_game*, int id, bool fade, float speed)`, `ret 0xc`. | An unknown id is a no-op. |

### Explosions and fires

`World.createExplosion` replays through `C_game::NewExplosion` `0x5aae10`
(see "Grenades and Molotovs") with the script's radius and damage and
`affectWorld = damage > 0`. reM `C_game::NewExplosion` skips every actor hit,
traffic, glass and crash-object damage when `affectWorld` is false and keeps
the particles, smoke, sound and light, which is the visual explosion.
`World.createFire` uses `C_game::NewFire` `0x5abe10` with the script's
lifetime, radius and damage and `damageActors = damage > 0`; `C_fire::Tick`
hits nothing without it. The source actor and parent frame are null, so the
combat hooks do not report these as grenade detonations. A message for another
mission generation is dropped.

### HUD, fade and camera

Scripted camera locks temporarily enable the native wide city cache. This
loads city segments around the active camera, including before a player has
spawned. The service saves the original cache flag and camera clipping range
once, restores both on unlock (even if spawning has changed the native camera
mode), and releases the override before mission close. SDK access uses named,
layout-checked fields and live virtual methods; no offset-based pointer reads
are needed for these settings.

| Native | Evidence and convention | Ownership and lifetime |
| --- | --- | --- |
| `C_cache_base_block::Tick` `0x4020b0` | `83 EC 3C 53 55 56 8B F1 33 DB 57`; `__thiscall(cache*, unsigned frameTime)`. At `0x402222`, `8B 15 8C 78 63 00 8B 42 24 8B 88 14 2F 00 00 C1 E9 09 F6 C1 01` reads the live mission's game flags and tests bit 9. IDA confirms float constants 200/230 for activation/deactivation and 550/580 when the bit is set. reM names it `C_GAME_FLAG_CACHE_RANGE`; `C_game::m_uGameFlags` is a `uint32_t` at `+0x2f14`, asserted in the typed SDK layout. | Retail continues ticking and owning the cache. No cache pointer is retained or code patched. Only the wide-range flag is restored, preserving other game flags. |
| `I3D_camera` projection fields | reM `I3D_camera.h` asserts FOV/near/far at `0x140/0x144/0x148`; the SDK mirrors them in `NativeCameraFrame`. Existing audited `SetRange` remains the only clipping-plane writer. | The active scene owns the camera. Only float values are saved across frames. |

Client-local visual frames use the same `I3D_driver::CreateFrame(FRAME_MODEL = 9,
FRAME_DUMMY = 6)` and `C_I3D_model_cache::Open` calls that reM's
`MODEL_CREATE` implements in `C_program_process_000_132.inl`. The frame is
linked to the mission's primary sector, moved with `SetWorldPos` and
`I3D_scene::SetFrameSectorPos`, and unlinked and released before mission close.
`I3D_frame`'s inline `SetRot` and `SetScale` update its quaternion at `+0x9c`,
scale at `+0x90`, and transform flags at `+0xac` (reM `I3D_frame.h` asserts
all three offsets); the client SDK mirrors these writes for full rotation and
per-axis scale. `I3D_scene::TransformPoints` at vtable `+0x74` projects a
world point; `g_collision::TestLineHStatic` checks static geometry between
the camera and a label. Local frames are non-interactive: no actor or
`C_using_object` record is made. `Scene.createModel` accepts a bare `.i3d`
filename and caps local frames at 128 per client.

The client `Draw` queue renders after the native HUD scene callback using
`IGraph::DrawPrimitiveList` for flat shapes and `G_IndicatorsClass::OutText`
for text. Native indicators have four stock font definitions, indexed 0–3;
custom typefaces belong in a resource-owned CEF view using CSS `@font-face`.
The queue is reset before each scripting `render` event so commands do not
survive a stopped resource or unloaded mission.

All `G_IndicatorsClass` calls are `__thiscall` on the object at `0x6bf980`.

| Native | Evidence and convention | Ownership and lifetime |
| --- | --- | --- |
| `ConsoleAddText` `0x5f9d50` | `53 8B 5C 24 08 56 8B F1`; `(unsigned char*, unsigned rgb)`, `ret 8`. Five lines, five seconds each. | Copies the text. |
| `RaceFlashText` `0x5fafc0` | `56 8B F1 8B 44 24 0C 68 80 00 00 00`; `(unsigned char*, float seconds)`, `ret 8`; sets flag `0x400000` and copies at most 127 characters. Drawn in the general HUD block (not only in races). | Copies the text. |
| `AddFlag` `0x47a080`, `RemoveFlag` `0x47a0a0`, `TestFlag` `0x47a0c0` | `8B 44 24 04 8B 91 A4 40 00 00` then `0B D0` / `F7 D0` / `85 D0`; flags at `+0x40a4`. Timer `0x8`, compass `0x80`. | `C_game::Init` sets the flags to 4 and `C_game::Done` to 0, so no HUD element outlives a mission. |
| `TimerSetTime` `0x5f7500`, `TimerSetInterval` `0x5f7540`, `TimerGetInterval` `0x5f7620` | `(h, m, s)` `ret 0xc`; `(seconds)` `ret 4`; `(unsigned*)` `ret 4`. `TIMERON` calls them. `Tick` counts the interval down only while flag 8 is set and then calls `C_game::Timer_TimeOut`, which ignores a null timer program. | The client polls the interval to raise `countdownEnd`. |
| `C_game::ScoreSetOn` `0x5b9fa0`, `C_game::ScoreSet` `0x5b9fe0` | `8A 44 24 04 84 C0 88 81 3E 36 00 00` / `8A 91 3E 36 00 00 8B 44 24 04`; `__thiscall`, `ret 4`. The freeride score is the bonus counter. `ScoreSetOn` writes visibility at `C_game+0x363e` and reads the current `int32` at `+0x3640` when showing it; `ScoreSet` writes the latter even while hidden. | `C_game::Init` turns it off. |
| `CompassSetDestination` `0x5fa020` | `8B 44 24 04 89 81 78 42 00 00 C2 04 00`: a raw store at `+0x4278` without a reference. `DrawAll` reads its world position while flag `0x80` is set. | The target is a dummy frame (`CreateFrame(FRAME_DUMMY = 6)`) the client owns. Before releasing it, the flag is removed and the destination set to null, including in the mission-closing callback; `C_game::Init` clears only the flag. |
| `FadeInOut` `0x5fa370` | `8B 44 24 08 8B 54 24 0C 53`; `(bool toColor, unsigned ms, unsigned rgb)`, `ret 0xc`; `ZATMYSE` calls it. `true` fades alpha 0 to 1 and holds, `false` 1 to 0 and clears flag `0x8000`; 0 ms is instant. | `C_game::Done` clears the flag. |
| `G_Camera::SetSwing` `0x5ed210` at `C_game+0x4c` (`GetCamera` `0x47b510`: `8D 41 4C C3`) | `8A 44 24 04 83 EC 18 84 C0`; `(bool, float intensity, I3D_frame* target)`, `ret 0xc`; `CAMERA_SETSWING` passes the script percentage times 0.01. It is a slow roll of camera, backdrop and target, not a shake; retail has no camera shake. | The camera keeps the target raw. The client passes its own dummy frame, disables with it (the only way the backdrop's roll is reset), then with null, then releases it, also in the mission-closing callback. |
| `G_IndicatorsClass::ParheliaSetFov` `0x604890` | `56 8B 74 24 08 85 F6 57`; `void __thiscall(indicators*, I3D_camera*, float radians)`, `ret 8`. Retail `CAMERA_SETFOV` converts degrees to radians and calls it so double/triple screen modes adjust FOV and aspect ratio together. The active camera's inline `GetFOV` reads `+0x140`; reM asserts that offset. | The active camera belongs to the scene. The script accepts 1–179 degrees and reads back its effective current FOV; native camera modes may update it later. No camera pointer is retained. |
| `I3D_camera::SetRange`, LS3DF vtable `+0x58` (`0x1000b7b0`) | `D9 44 24 0C D8 64 24 08`; `void __stdcall(camera*, float near, float far)`, `ret 0xc` on the virtual call. Retail `CAMERA_SETRANGE` uses the active camera. reM shows the setter recalculates projection matrices and the near/far fields at `+0x144/+0x148`. | The script requires finite near 0.01–10 and far above near up to 5000, then calls the active camera's virtual setter. No camera pointer is retained. |
| `G_Camera::LockAt` `0x5f39f0`, `Unlock` `0x5f3fd0` | `56 8B F1 83 7E 10 16 74 10` / `83 79 10 16 75 0D`; `void __thiscall(camera*, const S_vector& position, const S_vector& direction)` and `void __thiscall(camera*, bool restore)`. Retail `CAMERA_LOCK` copies a frame's world pose into the locked camera mode; `CAMERA_UNLOCK` passes `false`, then calls `C_game::RecomputeLightCache` `0x5b5d40`. During the first six startup ticks, lock also calls `SetCameraRotRepair` `0x5ba010`, using the tick count at `C_game+0x2b14`. | Script lock accepts value vectors only and normalizes the nonzero direction. The service unlocks its own lock before mission close. Unlock refreshes the light cache; neither argument nor a frame pointer is retained. |

`Camera.lock(position, direction, roll?)` accepts an optional finite bank in radians.
`G_Camera::LockAt` sets roll to zero, so a nonzero bank is applied afterwards via
the existing typed `NativeFrame::SetDirection` and `Update` calls on the active
scene camera. The already-audited `I3D_frame::SetDir` (`0x1001ab50`, entry bytes
`81 EC D8 00 00 00 53 56 8B B4 24 E8 00 00 00`) is
`void __stdcall(frame*, const S_vector*, float roll)`. IDA and reM agree that its
third argument builds the Z-axis quaternion in radians, composed as roll * pitch *
yaw. The frame and direction are borrowed for the call; no pointer is retained.
Omitting roll keeps the original level-camera behavior. No new offsets or native
addresses are introduced by the roll support.

## Stock mission doors

Stock scene doors are `C_door` actors (type 6), separate from car seat doors.
reM `C_door.h` identifies four native states: open 0, closed 1, opening 2 and
closing 3. `C_door::SetState` at `0x439610` takes a target state, optional
activating actor and a whole-chain flag. It climbs to the parent/root door
through `m_pNextDoor` at `+0x118` and propagates to its linked leaf through
`m_pPairedDoor` at `+0x114`. The root and leaf can each reverse the swing with
their byte at `+0x11e`. An opening request is rejected by the target door's
lock byte at `+0x125`.

The client hooks `SetState` and reports only a transition whose activating
actor is its local player and whose root actually entered the requested state.
The server checks mission and life generations, the player/door distance and
the door's lock/use state before updating the replicated target. A first
nearby use registers the named root frame; scripts can register known doors
earlier with `Door.create(frameName, hingePosition)`. Names identify doors
within one mission generation. The server removes them on mission change.

On each client, a door replica resolves the frame by name and reads the actor
owner from the frame buffer; it never retains either pointer. New state uses
`C_door::SetState` for native swing, collision and sound. An initial snapshot
or partial pose uses `C_door::SetOpenAngle` at `0x43b810` for both leaves,
followed by the matching rest state and use prompt. `EnableUsingObject` at
`0x43add0` controls the native prompt independently of locking. The scene
sector position also follows reM's reversible opening rule: a bidirectional
door is filed one world unit along or against its stored open direction,
according to the synchronized side, and returns to its hinge when closed.
This keeps both leaves visible at sector boundaries. Scene lookups and the
state cache are cleared at mission close. Reconnects receive the
same server replica fields, including lock, use state, side of each leaf and
target fraction.

## Nonblocking inventory and interaction menus

reM's `GM_Menu::ExecuteMenu` (`0x5eba40`, `__fastcall`, menu in ECX,
`tickMission` in EDX, one stack boolean) owns a synchronous input/render loop.
The player calls it with `tickMission = false` for inventory and action choices.
Only the camera advances there; the normal mission/client update cannot return
until the menu closes. The existing execute hook now intercepts `GM_Inventory`
and `GM_ItemPickUp` during a server mission, copies a choice snapshot, destroys
the uncreated menu, and immediately returns the native cancellation result.

| Native | Evidence and convention | Ownership and lifetime |
| --- | --- | --- |
| `GM_Inventory`, vtable `0x627584` | reM `GM_Inventory.h` and constructor `0x5e2a90`: size `0x38`, inventory at `+0x30`, dropped vector at `+0x34`. SDK layout assertions cover both. | Native caller owns the inventory and output vector. They are used only within the current execute-hook call. |
| `GM_ItemPickUp`, vtable `0x625920` | reM `GM_ItemPickUp.h`: size `0x54`; inventory/inventories/game items/dropped/removed at `+0x30/+0x34/+0x38/+0x3c/+0x40`; address vector at `+0x44`. VC6 vectors have their begin/end/capacity at `+4/+8/+0xc`. | The player builds the candidate and output vectors on its stack. No menu or vector pointer survives the hook. |
| `G_Inventory::Select` `0x607bc0` | `8B 44 24 08 83 EC 14 83 F8 04`; `bool __thiscall(inventory*, unsigned index, unsigned slot, dropped*)`, `ret 0xc`. Slot groups are hand 0, small weapons 1, coat 2, ordinary items 3 and pickup 4. | Uses freshly reconstructed native menu arguments. The original player caller still updates weapon models and drops. |
| `G_Inventory::Remove` `0x6095e0` | `83 EC 08 8B 44 24 0C 53 55 8B E9`; `bool __thiscall(inventory*, item*, dropped*)`, `ret 8`. | The selected item address is rebuilt from the live inventory before the call. |
| `GM_ItemPickUp::OnClick` `0x5e3480` | `56 57 8B 7C 24 0C 8B F1`; `int __thiscall(menu*, unsigned componentId)`, `ret 4`. Component `256 + index` reads the address vector. Actions (`itemId == 1`) return `169 + index`; taking an item returns 168; cancel is 167. | Borrows a temporary address array synchronously. The SDK restores the menu's original empty vector and the global menu-loop result before `Destroy`. It never calls `Create` or enters the retail menu loop. |
| `G_TextDatabase::GetText` `0x60fb40` | `8B 51 04 B8 00 10 00 00`; `const char* __thiscall(database*, unsigned id)`, `ret 4`, database object `0x6d8714`. Item names use `3500 + itemId`; action text uses the record's ammo-loaded field. | Text is copied and converted from the game's ANSI code page to UTF-8 before returning to the caller. |

The executable's `BuildEnabledItemIdList` (`0x609bb0`) collects item **addresses**,
despite reM's current ID-oriented name/decompilation. LEA instructions at
`0x609be0`, `0x609bff` and `0x609c58` verify this. The service preserves retail's
selected/weapon/coat/ordinary-item order. Ordinary inventory rows with item flag
`0x100` cannot be selected or dropped, matching `GM_Inventory::OnCreate`.
Ammo is shown only for the firearm flag `0x20`.

A web response queues an inventory or interaction input for the next normal
player tick (controls 24 and 10; alternate interaction 11 and fire 12/13 are
suppressed during capture/release). Native player AI rebuilds candidates, then
the service revalidates the chosen item, target and player/mission/spawn context
before running the native operation. Nested corpse menus retain only copied
choice identities. The service also uses generation-checked registry handles
for replicated targets. Cancel, death, stream-out, session teardown and input
focus changes discard pending menus; no mission reentrancy or background game
calls are introduced. The existing `Use_Actor` hook rejects a replay's actor use
until its choice has matched, so an unavailable pump cannot turn a pending
refuel choice into retail's fallback exit-car action.
