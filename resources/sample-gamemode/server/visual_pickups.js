// A separate demo from the built-in replicated weapon Pickup entity. The
// server keeps the definition and decides collection; clients draw only.
const VISUAL_PICKUP_ID = "supply-cache";
const RESPAWN_MS = 15000;
const COLLECT_RADIUS = 2.5;
let pickup = null;
let respawnTimer = null;

function publish(player = null) {
    if (!pickup) return;
    const state = {
        id: pickup.id,
        missionGeneration: pickup.missionGeneration,
        position: pickup.position,
        model: "9money.i3d",
        label: "SUPPLIES  +12 COLT ROUNDS",
        active: pickup.active,
    };
    if (player) player.emit("sample:visualPickupState", JSON.stringify(state));
    else Events.emitAllClients("sample:visualPickupState", state);
}

function ensurePickup(player) {
    if (!World.isReady() || !player.spawned || !player.alive) return;
    const generation = World.getMissionGeneration();
    if (pickup?.missionGeneration === generation) return;
    const {x, y, z} = player.position;
    pickup = {
        id: VISUAL_PICKUP_ID,
        missionGeneration: generation,
        position: {x: x + 3, y, z: z + 1},
        active: true,
    };
    publish();
}

Events.on("playerSpawn", ensurePickup);
Events.on("playerRespawn", ensurePickup);
Events.on("playerMissionReady", (player) => {
    if (pickup?.missionGeneration === World.getMissionGeneration()) publish(player);
});
Events.on("missionChange", () => {
    if (respawnTimer) clearTimeout(respawnTimer);
    respawnTimer = null;
    pickup = null;
});
Events.on("resourceStop", (name) => {
    if (name !== "mafia1online-sample") return;
    if (respawnTimer) clearTimeout(respawnTimer);
    respawnTimer = null;
    pickup = null;
});

Events.onClient("sample:visualPickupTryCollect", (player, request) => {
    if (!pickup?.active || !request || request.id !== pickup.id ||
        request.missionGeneration !== pickup.missionGeneration ||
        pickup.missionGeneration !== World.getMissionGeneration() ||
        !World.isReady() || !player.spawned || !player.alive ||
        player.missionGeneration !== pickup.missionGeneration) return;

    const a = player.position;
    const b = pickup.position;
    if (Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z) > COLLECT_RADIUS) return;

    // The award happens only here, after the server's position/life check.
    if (!player.hasWeapon(6) || !player.giveAmmo(6, 12)) {
        player.sendMessage("The supply cache needs a Colt. Yellow Pete can sell you one.");
        return;
    }
    pickup.active = false;
    publish();
    player.sendMessage("Collected supply cache: +12 Colt rounds");
    const generation = pickup.missionGeneration;
    respawnTimer = setTimeout(() => {
        respawnTimer = null;
        if (!pickup || pickup.missionGeneration !== generation || World.getMissionGeneration() !== generation) return;
        pickup.active = true;
        publish();
    }, RESPAWN_MS);
});
