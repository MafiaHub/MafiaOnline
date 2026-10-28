// Entering the city. Retail Free Ride's emeth_* frames are loaded in both the
// day and night missions; the client reports them and the server checks them
// against its native spawn hint before a district may be chosen. Each waiting
// player is held in a private virtual world so another player's ped never
// walks into their preview.
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const {MAIN_WORLD, inFreeRide, distance, emit, onStart, onStop} = require("./lib/util.js");

// emeth_1 is also the native start the spawn hint names, a short walk from
// the stock Salieri_save frame. The positions are the frames in each
// mission's stock scene2.bin; a client report must land within a few metres
// of them, so no client can choose its own spawn point.
const STOCK_FRAMES = {
    freeride: {emeth_1: {x: -1984.884, y: -5.032, z: 23.145}, emeth_3: {x: -189.258, y: 18.106, z: -690.477}, emeth_4: {x: 529.439, y: -5.477, z: 86.797}},
    freeridenoc: {emeth_1: {x: -1970.751, y: -4.846, z: 22.701}, emeth_3: {x: -187.805, y: 18.052, z: -683.178}, emeth_4: {x: 529.356, y: -5.514, z: 85.892}},
};
const DISTRICTS = [
    {id: "little-italy", frame: "emeth_1", name: "Little Italy", subtitle: "A short walk from Salieri's bar"},
    {id: "downtown", frame: "emeth_3", name: "Downtown", subtitle: "Where the deals are made"},
    {id: "hoboken", frame: "emeth_4", name: "Hoboken", subtitle: "The rough side of town"},
];

// Stock models from a2.dta; each client also drops any that fail the full
// native skeleton check before offering them.
const CHARACTERS = [
    {name: "Tommy Angelo", file: "Tommy.i3d", role: "The Driver"},
    {name: "Paulie", file: "Paulie.i3d", role: "The Hothead"},
    {name: "Sam", file: "Sam.i3d", role: "The Muscle"},
    {name: "Frank Colletti", file: "frank.i3d", role: "The Consigliere"},
    {name: "Luigi", file: "Luigi.i3d", role: "The Barkeep"},
    {name: "Lucas Bertone", file: "Lucas.i3d", role: "The Car Man"},
    {name: "Ralph", file: "Ralph.i3d", role: "The Mechanic"},
    {name: "Sergio Morello", file: "Sergio.i3d", role: "The Enforcer"},
    {name: "Michelle", file: "Michelle.i3d", role: "The Informant"},
    {name: "Sarah", file: "Sarah1.i3d", role: "The Bartender's Daughter"},
    {name: "Morello Soldier", file: "Gangster01.i3d", role: "The Rival"},
    {name: "Street Tough", file: "Hoolig02.i3d", role: "The Outsider"},
    {name: "Officer", file: "Pol01.i3d", role: "The Law"},
];

const selections = new Map();
const reportedReady = new Set();
const previewWorlds = new Map();
const FIRST_PREVIEW_WORLD = 0x40000000;
let nextPreviewWorld = FIRST_PREVIEW_WORLD;
const MAX_ANCHOR_DELTA = 3;

function isolatePreview(player) {
    if (!inFreeRide() || player.spawned) return;
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

function point(value) {
    if (!value || ![value.x, value.y, value.z].every(Number.isFinite)) return null;
    return {x: value.x, y: value.y, z: value.z};
}

// The client supplies the facing; the position must match the stock frame.
function validAnchor(report, frame) {
    const stock = STOCK_FRAMES[World.getMission()]?.[frame];
    if (!stock || !report || report.frame !== frame) return null;
    const position = point(report.position);
    const direction = point(report.direction);
    if (!position || !direction || Math.hypot(direction.x, direction.z) < 0.25 || distance(position, stock) > MAX_ANCHOR_DELTA) return null;
    return {position: stock, direction};
}

function headingOf(anchor) {
    return Math.atan2(anchor.direction.x, anchor.direction.z);
}

function sendOpen(player, selection) {
    const record = profiles.get(player);
    const locations = DISTRICTS.filter((district) => selection.anchors.has(district.id)).map((district) => ({
        id: district.id, name: district.name, subtitle: district.subtitle, frame: district.frame,
        ...selection.anchors.get(district.id),
    }));
    emit(player, "fr:selector:open", {
        generation: selection.generation, mission: World.getMission(), locations, characters: CHARACTERS,
        preferred: {locationId: record?.district ?? null, model: record?.model ?? null},
    });
}

Events.on("playerConnect", isolatePreview);
Events.on("playerMissionReady", isolatePreview);
Events.on("missionChange", () => {
    selections.clear();
    if (inFreeRide()) isolateWaitingPlayers();
    else leavePreviewWorlds();
});
onStart(isolateWaitingPlayers);
onStop(() => {
    selections.clear();
    leavePreviewWorlds();
});

Events.onClient("fr:selector:ready", (player, report) => {
    isolatePreview(player);
    if (!reportedReady.has(player.id)) {
        reportedReady.add(player.id);
        console.log(`[freeride] Selector report from ${player.nickname}: frames ${report?.anchors?.map((anchor) => anchor?.frame).join(",")}`);
    }
    if (!inFreeRide() || !World.isReady() || player.spawned ||
        report?.generation !== World.getMissionGeneration() || !Array.isArray(report.anchors)) return;

    const anchors = new Map();
    for (const district of DISTRICTS) {
        const anchor = validAnchor(report.anchors.find((candidate) => candidate?.frame === district.frame), district.frame);
        if (anchor) anchors.set(district.id, anchor);
    }
    if (anchors.size < 2) return;

    const previous = selections.get(player.id);
    const selection = {generation: report.generation, anchors, choice: previous?.choice ?? null};
    selections.set(player.id, selection);
    sendOpen(player, selection);
});

Events.onClient("fr:selector:spawn", (player, request) => {
    const selection = selections.get(player.id);
    if (!inFreeRide() || !selection || !request || player.spawned || !World.isReady() ||
        selection.generation !== World.getMissionGeneration() || request.generation !== selection.generation) return;

    const district = DISTRICTS.find((entry) => entry.id === request.locationId);
    const anchor = district && selection.anchors.get(district.id);
    const character = CHARACTERS.find((entry) => entry.file === request.model);
    if (!anchor || !character) return;

    if (!player.setModel(character.file) || !player.spawn(anchor.position, headingOf(anchor))) {
        emit(player, "fr:selector:error", {message: "That entrance is unavailable. Try again."});
        return;
    }
    player.setVirtualWorld(MAIN_WORLD);
    previewWorlds.delete(player.id);
    selection.choice = {locationId: district.id, model: character.file};
    profiles.setField(player, "district", district.id);
    profiles.setField(player, "model", character.file);
    emit(player, "fr:selector:accepted", {...selection.choice, district: district.name, character: character.name});
    console.log(`[freeride] ${player.nickname} entered ${district.name} as ${character.name}`);
});

// Dead players come back where they first entered the city; without a
// choice (another mission, or after a resource restart) life.js falls back
// to the native spawn hint.
life.setRespawnProvider((player) => {
    if (!inFreeRide()) return null;
    const selection = selections.get(player.id);
    const anchor = selection?.choice && selection.generation === World.getMissionGeneration()
        ? selection.anchors.get(selection.choice.locationId) : null;
    return anchor ? {position: anchor.position, heading: headingOf(anchor)} : null;
});

Events.on("playerDisconnect", (player) => {
    selections.delete(player.id);
    reportedReady.delete(player.id);
    previewWorlds.delete(player.id);
});
