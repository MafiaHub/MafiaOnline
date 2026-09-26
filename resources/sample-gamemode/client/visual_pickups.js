// GTA SA style pickup presentation. This model has no collision or native
// use-object; the server grants the reward after checking its own position.
const visualPickups = new Map();
const MODEL_RETRY_MS = 2_000;
const LABEL_DISTANCE = 30;

function removeVisual(record) {
    record.frame?.destroy();
    record.frame = null;
}

function applyPickup(state) {
    if (!state || typeof state.id !== "string" || !state.position) return;
    const existing = visualPickups.get(state.id);
    if (existing) removeVisual(existing);
    visualPickups.set(state.id, {...state, frame: null, nextAttempt: 0, nextModelAttempt: 0});
}

Events.on("sample:visualPickupState", applyPickup);
Events.on("missionUnload", () => {
    for (const record of visualPickups.values()) removeVisual(record);
    visualPickups.clear();
});
Events.on("resourceStop", (name) => {
    if (name !== "mafia1online-sample") return;
    for (const record of visualPickups.values()) removeVisual(record);
    visualPickups.clear();
});

Events.on("render", () => {
    const player = LocalPlayer;
    const now = Date.now();
    for (const record of visualPickups.values()) {
        if (record.missionGeneration !== World.getMissionGeneration() || !record.active || !player?.spawned || !player.alive) {
            removeVisual(record);
            continue;
        }
        if (record.frame === null) {
            if (now < record.nextModelAttempt) continue;
            record.nextModelAttempt = now + MODEL_RETRY_MS;
            record.frame = Scene.createModelFrame(record.model);
            if (record.frame === null) continue;
            record.frame.setScale(0.7);
        }
        const angle = now * 0.0015;
        const height = record.position.y + 0.55 + 0.18 * Math.sin(now * 0.003);
        record.frame.setWorldPosition({x: record.position.x, y: height, z: record.position.z});
        record.frame.setRotation({w: Math.cos(angle / 2), x: 0, y: Math.sin(angle / 2), z: 0});

        const a = player.position;
        const b = record.position;
        const distance = Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
        if (distance < LABEL_DISTANCE) {
            const labelPoint = {x: b.x, y: height + 0.7, z: b.z};
            const projection = Scene.projectWorld(labelPoint, true);
            if (projection?.visible) {
                Draw.worldText(record.label, labelPoint, 16, 0xffffd070, 3, true);
                Draw.circle(projection.x, projection.y + 12, 3, 0xaaffd070);
            }
        }

        if (now >= record.nextAttempt && distance < 2.1) {
            record.nextAttempt = now + 1000;
            Events.emitServer("sample:visualPickupTryCollect", {
                id: record.id,
                missionGeneration: record.missionGeneration,
            });
        }
    }
});
