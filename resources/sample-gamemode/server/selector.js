// Retail Free Ride's emeth_* frames are loaded in both selected missions.
// The stage keeps to two city starts, while emeth_1 proves the client's native
// frame report agrees with the game's own spawn hint.
const CITY_DISTRICTS = [
    {id: "downtown", frame: "emeth_3", name: "Downtown", subtitle: "Where the deals are made", models: [
        {name: "Tommy Angelo", file: "Tommy.i3d", role: "The Driver"},
        {name: "Paulie", file: "Paulie.i3d", role: "The Hothead"},
        {name: "Officer", file: "Pol01.i3d", role: "The Law"},
    ]},
    {id: "hoboken", frame: "emeth_4", name: "Hoboken", subtitle: "The rough side of town", models: [
        {name: "Tommy Angelo", file: "Tommy.i3d", role: "The Driver"},
        {name: "Street Tough", file: "Hoolig02.i3d", role: "The Outsider"},
        {name: "Paulie", file: "Paulie.i3d", role: "The Hothead"},
    ]},
];

function districts() {
    const mission = World.getMission();
    return mission === "freeridenoc" || mission === "freeride" ? CITY_DISTRICTS : null;
}

const selections = new Map();
const reportedReady = new Set();
const previewWorlds = new Map();
const MAIN_WORLD = 0;
const FIRST_PREVIEW_WORLD = 0x40000000;
let nextPreviewWorld = FIRST_PREVIEW_WORLD;
const RESPAWN_DELAY_MS = 5000;
const MAX_ANCHOR_DELTA = 12;
const MIN_DISTRICT_DISTANCE = 100;
const MAX_SCENE_COORDINATE = 50_000;

function isolatePreview(player) {
    if (!districts() || player.spawned) return;
    let world = previewWorlds.get(player.id);
    if (world === undefined) {
        world = nextPreviewWorld++;
        previewWorlds.set(player.id, world);
    }
    if (player.virtualWorld !== world) player.setVirtualWorld(world);
}

function isolateWaitingPlayers() {
    for (const player of World.getPlayers()) isolatePreview(player);
}

function leavePreviewWorlds() {
    for (const player of World.getPlayers()) {
        if (previewWorlds.has(player.id)) player.setVirtualWorld(MAIN_WORLD);
    }
    previewWorlds.clear();
}

Events.on("playerConnect", isolatePreview);
Events.on("playerMissionReady", isolatePreview);
Events.on("missionChange", () => {
    selections.clear();
    if (districts()) isolateWaitingPlayers();
    else leavePreviewWorlds();
});
Events.on("resourceStart", (name) => { if (name === "mafia1online-sample") isolateWaitingPlayers(); });

function point(value) {
    if (!value || ![value.x, value.y, value.z].every(Number.isFinite)) return null;
    if (Math.max(Math.abs(value.x), Math.abs(value.y), Math.abs(value.z)) > MAX_SCENE_COORDINATE) return null;
    return {x: value.x, y: value.y, z: value.z};
}

function distance(a, b) {
    return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

function validAnchor(report, district) {
    if (!report || report.frame !== district.frame) return null;
    const position = point(report.position);
    const direction = point(report.direction);
    if (!position || !direction || Math.hypot(direction.x, direction.z) < 0.25) return null;
    return {position, direction};
}

function sendOpen(player, selection) {
    const locations = districts().filter((district) => selection.anchors.has(district.id)).map((district) => ({
        id: district.id,
        name: district.name,
        subtitle: district.subtitle,
        frame: district.frame,
        models: district.models,
        ...selection.anchors.get(district.id),
    }));
    player.emit("sample:selector:open", JSON.stringify({generation: selection.generation, mission: World.getMission(), locations}));
}

Events.onClient("sample:selector:ready", (player, report) => {
    isolatePreview(player);
    const choices = districts();
    if (!reportedReady.has(player.id)) {
        reportedReady.add(player.id);
        const suggested = player.getSuggestedSpawn();
        console.log(`[mafia1online-sample] Selector report from ${player.nickname}: generation ${report?.generation}, mission ${World.getMission()}, allReady ${World.isReady()}, suggested ${suggested ? `${suggested.x},${suggested.y},${suggested.z}` : "none"}, frames ${report?.anchors?.map((anchor) => anchor.frame).join(",")}`);
    }
    if (!choices || !World.isReady() || player.spawned ||
        report?.generation !== World.getMissionGeneration() || !Array.isArray(report.anchors)) return;

    const suggested = player.getSuggestedSpawn();
    if (!suggested) return; // The native pose report may still be in flight.

    const anchors = new Map();
    for (const district of choices) {
        const anchor = validAnchor(report.anchors.find((candidate) => candidate?.frame === district.frame), district);
        if (anchor) anchors.set(district.id, anchor);
    }
    const nativeStart = validAnchor(report.anchors.find((candidate) => candidate?.frame === "emeth_1"), {frame: "emeth_1"});
    if (!nativeStart || distance(nativeStart.position, suggested) > MAX_ANCHOR_DELTA) return;
    const first = anchors.get(choices[0].id);
    const second = anchors.get(choices[1].id);
    if (first && second && distance(first.position, second.position) < MIN_DISTRICT_DISTANCE) return;
    if (anchors.size < 2) return;

    const previous = selections.get(player.id);
    const selection = {generation: report.generation, anchors, choice: previous?.choice ?? null};
    selections.set(player.id, selection);
    sendOpen(player, selection);
});

Events.onClient("sample:selector:spawn", (player, request) => {
    const selection = selections.get(player.id);
    const choices = districts();
    if (!choices || !selection || !request || player.spawned || !World.isReady() ||
        selection.generation !== World.getMissionGeneration() ||
        request.generation !== selection.generation) return;

    const district = choices.find((entry) => entry.id === request.locationId);
    const anchor = district && selection.anchors.get(district.id);
    const model = district?.models.find((entry) => entry.file === request.model);
    if (!anchor || !model) return;

    const heading = Math.atan2(anchor.direction.x, anchor.direction.z);
    if (!player.setModel(model.file) || !player.spawn(anchor.position, heading)) {
        player.emit("sample:selector:error", "That entrance is unavailable. Try again.");
        return;
    }
    player.setVirtualWorld(MAIN_WORLD);
    previewWorlds.delete(player.id);
    selection.choice = {locationId: district.id, model: model.file};
    player.emit("sample:selector:accepted", JSON.stringify(selection.choice));
    console.log(`[mafia1online-sample] ${player.nickname} entered ${district.name} as ${model.name}`);
});

Events.on("playerDeath", (player, _killer, life) => {
    if (!districts()) return;
    const selection = selections.get(player.id);
    const choice = selection?.choice;
    const anchor = choice && selection.anchors.get(choice.locationId);
    if (!anchor) return;
    const generation = selection.generation;
    setTimeout(() => {
        if (World.getMissionGeneration() !== generation || player.alive ||
            player.spawnGeneration !== life.spawnGeneration || !World.isReady()) return;
        player.respawn(anchor.position, Math.atan2(anchor.direction.x, anchor.direction.z));
    }, RESPAWN_DELAY_MS);
});

Events.on("playerDisconnect", (player) => { selections.delete(player.id); reportedReady.delete(player.id); previewWorlds.delete(player.id); });
Events.on("resourceStop", (name) => {
    if (name === "mafia1online-sample") { selections.clear(); leavePreviewWorlds(); }
});
