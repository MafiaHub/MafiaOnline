// Markers drawn by this client: race checkpoints, event beacons and crates,
// door arrows, turf rings, job stations, bounty labels over wanted players,
// and blips on the city map (hold TAB) for those and for crewmates. Groups
// come from the server; nothing drawn here can claim a reward. An item may
// carry `range` (hidden farther away), `ring` (a ground circle of that
// radius), `beacon`, `model`, `primary`, `near` (how close its label shows),
// `hint` (a second label line shown up close) and `map`: false keeps it off
// the city map, an object sets its blip
// ({icon, label, size, edge, flash}). Without `map`, objectives, beacons and
// markers seen from afar get a plain blip; the close-up ones stay off.
const MARKER_RESOURCE = "freeride";
const BEACON_HEIGHT = 40;
const BEACON_RANGE = 3000;
// World labels show only near and in sight; the city map covers the rest.
const LABEL_NEAR = 60;
const PRIMARY_LABEL_NEAR = 250;
const MAP_FROM_RANGE = 150;
const BOUNTY_RANGE = 350;
const MODEL_RETRY_MS = 2000;
const BLIP_SYNC_MS = 500;
const RING_SEGMENTS = 32;
const HINT_NEAR = 15;
const ARROW_MODEL = "sipka.i3d";

const groups = new Map();
const models = new Map();
// Blip id -> the options it was last set with, so only changes are sent.
const blips = new Map();
let blipsSyncedAt = 0;

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

function streamedVehicle(item) {
    return typeof item.vehicleId === "number" ? World.getVehicles().find((candidate) => candidate.id === item.vehicleId) ?? null : null;
}

// A vehicle item follows the vehicle while it is streamed here, and falls
// back to the rough position the server sent with it otherwise.
function itemPosition(item) {
    return streamedVehicle(item)?.getWorldTransform()?.position ?? item.position ?? null;
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

function circle(center, radius, height, thickness, color) {
    let previous = null;
    for (let step = 0; step <= RING_SEGMENTS; step++) {
        const angle = step / RING_SEGMENTS * Math.PI * 2;
        const point = Scene.projectWorld({x: center.x + Math.cos(angle) * radius, y: center.y + height, z: center.z + Math.sin(angle) * radius}, false);
        const visible = point && point.x !== null;
        if (visible && previous) Draw.line(previous.x, previous.y, point.x, point.y, thickness, color);
        previous = visible ? point : null;
    }
}

function drawRing(center, radius, color) {
    circle(center, radius, 0.4, 3, argb(color, 0.6));
}

// A dark plate centred on (x, y) with the item's colour down its edge: the
// title, and a second line under it when there is one.
function drawPlate(x, y, title, subtitle, color, alpha, size) {
    const width = Math.max(title.length * size * 0.56, subtitle ? subtitle.length * 12 * 0.56 : 0) + 28;
    const height = size + 14 + (subtitle ? 16 : 0);
    const left = x - width / 2;
    const top = y - height / 2;
    Draw.rect(left, top, width, height, argb("#101418", 0.72 * alpha));
    Draw.rect(left, top, 4, height, argb(color, alpha));
    Draw.rect(left + 4, top, width - 4, 1, argb(color, 0.45 * alpha));
    Draw.text(title, x + 2, top + 6 + size, size, argb(color, alpha), 3);
    if (subtitle) Draw.text(subtitle, x + 2, top + size + 22, 12, argb("#f3e9d6", 0.85 * alpha), 3);
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
            const near = item.near ?? (item.primary ? PRIMARY_LABEL_NEAR : LABEL_NEAR);
            if (!item.label || distance > near) continue;
            // Not through walls: a label behind a building waits for the corner.
            // It sinks toward eye level up close, so it stays in view there.
            const lift = Math.min(item.vehicleId ? 2.6 : item.model ? 3.4 : 3, 1.6 + distance * 0.25);
            const label = Scene.projectWorld({x: position.x, y: position.y + lift, z: position.z}, true);
            if (!label?.visible) continue;
            // Fades in over the last fifth of its reach instead of popping up.
            const alpha = Math.min(1, (near - distance) / (near * 0.2));
            const subtitle = item.hint && distance < HINT_NEAR ? item.hint : item.primary ? `${Math.round(distance)} m` : null;
            drawPlate(label.x, label.y, item.label, subtitle, item.color, alpha, item.primary ? 17 : 14);
        }
    }
}

function drawPeople(eye) {
    for (const player of World.getPlayers()) {
        if (player.isLocal || !player.alive) continue;
        const position = player.getWorldPosition();
        if (!position) continue;
        const bounty = player.state.get("fr.bounty");
        if (bounty && flatDistance(eye, position) < BOUNTY_RANGE) {
            Draw.worldText(`$${bounty.toLocaleString("en-US")} BOUNTY`, {x: position.x, y: position.y + 2.7, z: position.z}, 15, argb("#d0574a"), 3, true);
        }
    }
}

function rgb(hex) {
    return typeof hex === "string" && /^#[0-9a-f]{6}$/i.test(hex) ? parseInt(hex.slice(1), 16) : 0xf3e9d6;
}

function titleCase(label) {
    return String(label ?? "").split(" · ")[0].toLowerCase().replace(/(^|\s)\S/g, (letter) => letter.toUpperCase());
}

function blipOf(item) {
    if (item.map === false) return null;
    const spec = typeof item.map === "object" && item.map ? item.map : null;
    if (!spec && !item.primary && !item.beacon && item.range && item.range < MAP_FROM_RANGE) return null;
    const options = {color: rgb(item.color), icon: spec?.icon ?? (item.primary ? "diamond" : "dot"), size: spec?.size ?? (item.primary ? 6 : 4),
        edge: spec?.edge ?? Boolean(item.primary), flash: Boolean(spec?.flash)};
    const label = spec?.label ?? (item.primary ? titleCase(item.label) : null);
    if (label) options.label = label;
    return options;
}

// What the city map should show: every group's items, then crewmates.
function wantedBlips() {
    const wanted = new Map();
    for (const [group, items] of groups) {
        for (const item of items) {
            const options = blipOf(item);
            if (!options) continue;
            const target = streamedVehicle(item) ?? item.position ?? null;
            if (target) wanted.set(`${group}/${item.id}`, {target, options});
        }
    }
    const ownCrew = LocalPlayer?.state.get("fr.crew")?.tag ?? null;
    if (ownCrew) {
        for (const player of World.getPlayers()) {
            const crew = player.state.get("fr.crew");
            if (player.isLocal || crew?.tag !== ownCrew) continue;
            wanted.set(`crew/${player.id}`, {target: player, options: {color: rgb(crew.color), icon: "dot", size: 4, label: player.nickname}});
        }
    }
    return wanted;
}

function syncBlips() {
    const wanted = wantedBlips();
    for (const id of [...blips.keys()]) {
        if (wanted.has(id)) continue;
        CityMap.removeBlip(id);
        blips.delete(id);
    }
    for (const [id, {target, options}] of wanted) {
        // An entity blip follows it natively; a point is resent when it moves.
        const key = JSON.stringify([options, typeof target.id === "number" ? `#${target.id}` : target]);
        if (blips.get(id) === key) continue;
        if (CityMap.setBlip(id, target, options)) blips.set(id, key);
    }
}

Events.on("fr:markers", (request) => {
    if (typeof request?.group !== "string" || !Array.isArray(request.items)) return;
    releaseModels(request.group);
    if (request.items.length) groups.set(request.group, request.items);
    else groups.delete(request.group);
    blipsSyncedAt = 0;
});

Events.on("missionUnload", () => {
    groups.clear();
    models.clear();
    // The mission's blips went with it.
    blips.clear();
});
Events.on("resourceStop", (name) => {
    if (name !== MARKER_RESOURCE) return;
    releaseModels(null);
    groups.clear();
});

Events.on("render", () => {
    if (!World.isReady() || !LocalPlayer?.spawned) return;
    const now = Date.now();
    // Vehicles and crewmates stream in and out.
    if (now - blipsSyncedAt >= BLIP_SYNC_MS) {
        blipsSyncedAt = now;
        syncBlips();
    }
    // The map covers the screen; nothing in the world shows over it.
    if (CityMap.isOpen()) {
        for (const [group, items] of groups) for (const item of items) if (item.model) hideModel(group, item);
        return;
    }
    const eye = eyePosition();
    if (!eye) return;
    drawItems(eye, now);
    drawPeople(eye);
});
