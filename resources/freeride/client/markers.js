// World markers drawn by this client: race checkpoints, event beacons and
// crates, door arrows, turf rings, bounty labels over wanted players and
// crewmates across the city. Groups come from the server; nothing drawn here
// can claim a reward. An item may carry `range` (hidden farther away),
// `ring` (a ground circle of that radius), `beacon`, `model` and `primary`.
const MARKER_RESOURCE = "freeride";
const BEACON_HEIGHT = 40;
const BEACON_RANGE = 3000;
const LABEL_RANGE = 900;
const BOUNTY_RANGE = 350;
const CREW_RANGE = 1500;
const CREW_NEAR = 35;
const MODEL_RETRY_MS = 2000;
const RING_SEGMENTS = 32;
const ARROW_MODEL = "sipka.i3d";

const groups = new Map();
const models = new Map();

function argb(hex, alpha = 1) {
    const rgb = typeof hex === "string" && /^#[0-9a-f]{6}$/i.test(hex) ? parseInt(hex.slice(1), 16) : 0xf3e9d6;
    return Math.round(alpha * 255) * 0x1000000 + rgb;
}

function flatDistance(a, b) {
    return Math.hypot(a.x - b.x, a.z - b.z);
}

function eyePosition() {
    return LocalPlayer?.getWorldPosition() ?? null;
}

function itemPosition(item) {
    if (item.position) return item.position;
    if (typeof item.vehicleId !== "number") return null;
    const vehicle = World.getVehicles().find((candidate) => candidate.id === item.vehicleId);
    return vehicle?.getWorldTransform()?.position ?? null;
}

function releaseModels(group) {
    for (const [key, record] of models) {
        if (group && !key.startsWith(`${group}/`)) continue;
        record.frame?.destroy();
        models.delete(key);
    }
}

function drawBeacon(base, color, distance) {
    const bottom = Scene.projectWorld(base, false);
    const top = Scene.projectWorld({x: base.x, y: base.y + BEACON_HEIGHT, z: base.z}, false);
    if (!bottom || !top || bottom.x === null || top.x === null) return;
    const width = Math.max(2, Math.min(10, 10 - distance / 120));
    Draw.line(bottom.x, bottom.y, top.x, top.y, width + 6, argb(color, 0.12));
    Draw.line(bottom.x, bottom.y, top.x, top.y, width, argb(color, 0.55));
}

function hideModel(group, item) {
    models.get(`${group}/${item.id}`)?.frame?.setVisible(false);
}

function drawRing(center, radius, color) {
    let previous = null;
    for (let step = 0; step <= RING_SEGMENTS; step++) {
        const angle = step / RING_SEGMENTS * Math.PI * 2;
        const point = Scene.projectWorld({x: center.x + Math.cos(angle) * radius, y: center.y + 0.4, z: center.z + Math.sin(angle) * radius}, false);
        const visible = point && point.x !== null;
        if (visible && previous) Draw.line(previous.x, previous.y, point.x, point.y, 3, argb(color, 0.6));
        previous = visible ? point : null;
    }
}

function drawModel(group, item, position, now) {
    const key = `${group}/${item.id}`;
    let record = models.get(key);
    if (!record) {
        record = {frame: null, nextAttempt: 0, model: item.model};
        models.set(key, record);
    }
    if (record.frame === null) {
        if (now < record.nextAttempt) return;
        record.nextAttempt = now + MODEL_RETRY_MS;
        record.frame = Scene.createModelFrame(item.model) ?? Scene.createModelFrame("9money.i3d");
        if (record.frame === null) return;
    }
    record.frame.setVisible(true);
    const spin = now * 0.001;
    if (record.model === ARROW_MODEL) {
        // sipka.4ds is a flat arrow standing in its x-y plane with the tip
        // 1.13 m out along +x. LS3D turns a frame by the conjugate of the
        // stored quaternion, so this stores conj(spin about y * quarter turn
        // about z), which tips it point down and spins it about the vertical,
        // as retail mission arrows do. Its tip then hovers 0.3 m up.
        const c = Math.cos(spin / 2) * Math.SQRT1_2;
        const s = Math.sin(spin / 2) * Math.SQRT1_2;
        record.frame.setWorldPosition({x: position.x, y: position.y + 1.45 + 0.12 * Math.sin(now * 0.003), z: position.z});
        record.frame.setRotation({w: c, x: s, y: -s, z: c});
        return;
    }
    record.frame.setWorldPosition({x: position.x, y: position.y + 0.25 + 0.12 * Math.sin(now * 0.003), z: position.z});
    record.frame.setRotation({w: Math.cos(spin / 2), x: 0, y: Math.sin(spin / 2), z: 0});
}

function drawItems(eye, now) {
    for (const [group, items] of groups) {
        for (const item of items) {
            const position = itemPosition(item);
            if (!position) continue;
            const distance = flatDistance(eye, position);
            if (item.range && distance > item.range) {
                if (item.model) hideModel(group, item);
                continue;
            }
            if (item.model) drawModel(group, item, position, now);
            if (item.ring && distance < item.ring * 6) drawRing(position, item.ring, item.color);
            if (item.beacon && distance < BEACON_RANGE) drawBeacon(position, item.color, distance);
            if (distance > (item.primary ? BEACON_RANGE : LABEL_RANGE)) continue;
            const label = Scene.projectWorld({x: position.x, y: position.y + (item.vehicleId ? 2.6 : 3), z: position.z}, false);
            if (!label?.onScreen || label.x === null) continue;
            const size = item.primary ? 18 : 14;
            Draw.text(item.label, label.x, label.y, size, argb(item.color), 3);
            Draw.text(`${Math.round(distance)} m`, label.x, label.y + size + 2, 12, argb("#f3e9d6", 0.8), 3);
        }
    }
}

function drawPeople(eye) {
    const ownCrew = LocalPlayer?.state.get("fr.crew")?.tag ?? null;
    for (const player of World.getPlayers()) {
        if (player.isLocal || !player.alive) continue;
        const position = player.getWorldPosition();
        if (!position) continue;
        const distance = flatDistance(eye, position);
        const head = {x: position.x, y: position.y + 2.35, z: position.z};
        const bounty = player.state.get("fr.bounty");
        if (bounty && distance < BOUNTY_RANGE) {
            Draw.worldText(`$${bounty.toLocaleString("en-US")} BOUNTY`, {x: head.x, y: head.y + 0.35, z: head.z}, 15, argb("#d0574a"), 3, true);
        }
        const crew = player.state.get("fr.crew");
        if (ownCrew && crew?.tag === ownCrew && distance > CREW_NEAR && distance < CREW_RANGE) {
            Draw.worldText(`[${crew.tag}] ${player.nickname} · ${Math.round(distance)} m`, head, 13, argb(crew.color, 0.9), 3, false);
        }
    }
}

Events.on("fr:markers", (request) => {
    if (typeof request?.group !== "string" || !Array.isArray(request.items)) return;
    releaseModels(request.group);
    if (request.items.length) groups.set(request.group, request.items);
    else groups.delete(request.group);
});

Events.on("missionUnload", () => {
    groups.clear();
    models.clear();
});
Events.on("resourceStop", (name) => {
    if (name !== MARKER_RESOURCE) return;
    releaseModels(null);
    groups.clear();
});

Events.on("render", () => {
    if (!World.isReady() || !LocalPlayer?.spawned) return;
    const eye = eyePosition();
    if (!eye) return;
    drawItems(eye, Date.now());
    drawPeople(eye);
});
