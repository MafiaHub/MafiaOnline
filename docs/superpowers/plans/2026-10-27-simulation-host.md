# Simulation Host Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A stripped Mafia client that joins the dedicated server as a trusted, invisible peer and runs the native world, with a free-fly debug view (full or wireframe) or headless ticking, ready to own NPCs.

**Architecture:** The client gains a `Simulation` role chosen by `client.json`. It authenticates with a shared token in the connection ticket; the server gives it a private viewer entity instead of a player. Its game loop replaces retail `GameLoopSingle1` with one that ticks the mission, optionally renders, and never opens menus.

**Tech Stack:** C++20, MinHook, MafiaHub Framework replication, retail Mafia 1.2 (32-bit), D3D8.

**Spec:** [docs/npc_multiplayer_audit.md](../../npc_multiplayer_audit.md)

## Global Constraints

- Build only through `builds\build.bat <target> <32|64>`; client is 32-bit, server 64-bit.
- AGENTS.md rule 1: no null checks on fixed retail addresses; rule 2: never write outside the repository.
- Every native address and offset is verified in IDA against the retail image before use.
- New entity type changes netcode: client and server must be rebuilt together (MAJOR).
- Players never see the simulation host: no avatar, no nametag, not in player lists or scripts.

## Review Focus

- Simulation token missing on either side: the peer must join as an ordinary player, never as simulator.
- A second simulation host connects: it is refused, the first keeps running.
- Simulation host disconnects mid-mission: server logs it and keeps players unaffected.
- Window closed or server changes mission while the custom loop runs: loop exits through the same path as retail.
- Headless mode: the mission keeps ticking at the configured rate with the window hidden.

---

### Task 1: Shared simulator entity and server attachment

**Files:**
- Create: `code/shared/features/simulation/simulator_entity.h`
- Create: `code/server/src/features/simulation/simulation_service.{h,cpp}`
- Modify: `code/shared/register_entities.cpp`, `code/server/src/core/server.{h,cpp}`, `code/server/src/main.cpp`, `config/server.example.json`

**Interfaces:**
- `SimulatorEntity` (`kTypeName = "Mafia1Online::Simulator"`), viewer of the simulation connection, `streaming.targetGUID` = that connection.
- `SimulationService::Init(std::string token)`, `bool TryAttach(MafiaNet::PeerGuid guid)`, `bool IsSimulator(MafiaNet::PeerGuid) const`, `void OnDisconnect(MafiaNet::PeerGuid)`, `std::optional<MafiaNet::PeerGuid> Host() const`.
- Ticket format: `"simulation:" + token`; server config key `simulationToken` (string, optional, default empty = disabled, not replicated).

- [x] Server `OnPlayerConnect` asks `TryAttach` first; on success it skips player creation, readiness, car and debris assignment and the `playerConnect` script event. `OnPlayerDisconnect` mirrors this.
- [x] Build `Mafia1OnlineServer 64`; expected: success.
- [ ] Commit `Server: Admit a trusted simulation host`.

### Task 2: Client simulation role and stripped boot

**Files:**
- Create: `code/client/src/features/simulation/simulation_config.{h,cpp}`
- Modify: `code/client/src/main.cpp`, `code/client/src/core/application.{h,cpp}`, `code/client/src/core/application_module.cpp`, `code/client/src/features/display/system_init_hooks.cpp`, `config/client.example.json`

**Interfaces:**
- `Simulation::Config { bool enabled; std::string token; RenderMode render; uint32_t tickHz; }`, `RenderMode { Full, Wireframe, Headless }`, `const Config &Simulation::GetConfig()`, `bool Simulation::IsHost()`.
- Read from `client.json` key `simulation`; host/port/nickname still come from `quickJoin`.

- [x] Simulation role: framework `useRenderer = false`, sound disabled in `InitSystemHook`, ticket set in `CurrentState`; `PostInit` skips web UI, gameplay menus, nametags overlay and sounds; `PostUpdate` skips chat, HUD and map.
- [ ] Commit `Client: Add the simulation host role`.

### Task 3: Simulation game loop, render modes and free camera

**Files:**
- Create: `code/client/src/features/simulation/simulation_loop.{h,cpp}`, `code/client/src/features/simulation/free_camera.{h,cpp}`
- Modify: `code/sdk/include/mafia1/sdk/graphics/native_graph.h` (IGraph slots +0x2c, +0x100, +0xfc), `code/client/src/features/world/world_service.cpp`

**Interfaces:**
- `void Simulation::RunLoop(FreeCamera &camera)`, replaces `SDK::Core::System::RunGameLoop()` when `IsHost()`.
- `FreeCamera::Update(float seconds, HWND window)` then `FreeCamera::Apply(SDK::Scene::NativeScene &scene)`.

- [x] Loop: process window messages, `GetTimer(16, 83)`, `C_mission::Tick`, then by mode: Full renders and presents; Wireframe sets `D3DRS_FILLMODE` to wireframe around the render; Headless hides the window and sleeps to `tickHz`. Exit on `C_game` state 1 or uninitialised.
- [x] Free camera: WASD move, Q/E down/up, Shift x5, right mouse look, F1 toggles Full/Wireframe; window title shows mode, FPS, tick ms, camera position.
- [x] No local avatar: mission loading suppresses the stock player and the server creates no `PlayerEntity` for the host, so `C_game::m_pPlayer` stays null (verified in the first live run).
- [ ] Commit `Client: Run the simulation host loop with a debug camera`.

### Task 4: Harness and live verification

**Files:**
- Modify: `tools/run_two_clients.ahk` (`--simulation [full|wireframe|headless]`)

- [x] The harness writes a runtime server config with a random `simulationToken`, starts the server, the simulation host and the player clients.
- [x] Verify: server log shows the simulator attached and no player created for it; player client shows no extra avatar; screenshots of full and wireframe; headless keeps ticking with the window hidden.
- [ ] Commit `Tools: Start the simulation host from the harness`.

**Verified 2026-10-27 (live, Windows):** server log `Simulation host ... attached as viewer 3`, no player created for it;
the player client creates only its own replica; wireframe and full views render the city and the remote player
(Tab focuses players, F1 toggles, WASD/Q/E move); headless runs 30.2 Hz at 0.2-0.3 ms per tick, about 2% of one
core, window hidden. Found during the run: the host has no native player at all (`C_game::m_pPlayer` null), and the
client only created remote players while its own player was spawned; the host now observes every spawned player.

---

## Next plans (written when this one lands)

1. **Pedestrians on the host:** generator data and `check.bin` parser in shared code; all generators near any player active; native spawn/retire over all players; render-stamp and camera sites patched for the host; per-pedestrian panic origin; debug overlay of pedestrians and observers.
2. **Pedestrian replication:** `PedestrianEntity` owned by the simulation host; passive replicas on players driven by snapshots (skip native `Tick` in `TickElements`); hits and car impacts reported to the server and applied on the host.
