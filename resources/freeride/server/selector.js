// Entering the city. Each map's districts are listed in lib/maps.js. Most are
// the mission's own emeth_* frames: the client reports them and the server
// checks each against that map's stock position, so no client can choose its
// own spawn point. Districts without a frame in the loaded scene (Lost Heaven
// Extended's countryside) are placed by the server alone. Each waiting player
// is held in a private virtual world so another player's ped never walks
// into their preview. A returning player can also pick up where they left
// off: the server keeps their last street position, health, weapons and
// wanted level in their record.
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const maps = require("./lib/maps.js");
const {MAIN_WORLD, inFreeRide, distance, plain, headingOf, emit, onClient, onStart, onStop} = require("./lib/util.js");

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
const RESUME = "resume";
// Often enough that a dropped connection loses only a few steps. Not on
// disconnect: life.js has already ended the player's activity by then.
const RESUME_SAVE_MS = 5000;
let resumeTimer = null;

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
    const stock = maps.current()?.frames[frame];
    if (!stock || !report || report.frame !== frame) return null;
    const position = point(report.position);
    const direction = point(report.direction);
    if (!position || !direction || Math.hypot(direction.x, direction.z) < 0.25 || distance(position, stock) > MAX_ANCHOR_DELTA) return null;
    return {position: stock, direction};
}

function anchorHeading(anchor) {
    return Math.atan2(anchor.direction.x, anchor.direction.z);
}

// The last place a player stood on this map's streets, saved while they
// play. A race, a duel, a job or a cell is not a place to come back to, so
// the previous spot is kept through them.
function saveResume(player) {
    if (!inFreeRide() || !player.spawned || !player.alive || player.missionGeneration !== World.getMissionGeneration() ||
        player.virtualWorld !== MAIN_WORLD || life.activity(player)) return;
    const position = plain(player.position);
    if (![position.x, position.y, position.z].every(Number.isFinite)) return;
    const {weapons, selected} = life.loadoutOf(player);
    profiles.setField(player, "resume", {mission: World.getMission(), position, heading: headingOf(player.rotation), health: player.health,
        weapons, selected, wanted: Police.getRecord(player)?.level ?? 0, at: Date.now()});
}

function resumeOf(player) {
    const record = profiles.get(player);
    const resume = record?.resume;
    if (!resume || resume.mission !== World.getMission() || (record.jailUntil ?? 0) > Date.now()) return null;
    return resume;
}

function nearestDistrict(selection, position) {
    let best = null;
    for (const district of maps.current()?.districts ?? []) {
        const anchor = selection.anchors.get(district.id);
        if (anchor && (!best || distance(anchor.position, position) < distance(best.anchor.position, position))) best = {district, anchor};
    }
    return best?.district ?? null;
}

function sendOpen(player, selection) {
    const record = profiles.get(player);
    const locations = (maps.current()?.districts ?? []).filter((district) => selection.anchors.has(district.id)).map((district) => ({
        id: district.id, name: district.name, subtitle: district.subtitle, frame: district.frame,
        ...selection.anchors.get(district.id),
    }));
    const resume = selection.anchors.has(RESUME) && resumeOf(player);
    if (resume) {
        const near = nearestDistrict(selection, resume.position);
        locations.unshift({id: RESUME, name: "Where you left off", subtitle: near ? `Near ${near.name}` : "Back on the street", ...selection.anchors.get(RESUME)});
    }
    emit(player, "fr:selector:open", {
        generation: selection.generation, mission: World.getMission(), locations, characters: CHARACTERS,
        preferred: {locationId: resume ? RESUME : record?.district ?? null, model: record?.model ?? null},
    });
}

Events.on("playerConnect", isolatePreview);
Events.on("playerMissionReady", isolatePreview);
Events.on("missionChange", () => {
    selections.clear();
    if (inFreeRide()) isolateWaitingPlayers();
    else leavePreviewWorlds();
});
onStart(() => {
    isolateWaitingPlayers();
    resumeTimer = setInterval(() => World.getPlayers().forEach(saveResume), RESUME_SAVE_MS);
});
onStop(() => {
    clearInterval(resumeTimer);
    for (const player of World.getPlayers()) saveResume(player);
    selections.clear();
    leavePreviewWorlds();
});

onClient("fr:selector:ready", (player, report) => {
    isolatePreview(player);
    if (!reportedReady.has(player.id)) {
        reportedReady.add(player.id);
        console.log(`[freeride] Selector report from ${player.nickname}: frames ${report?.anchors?.map((anchor) => anchor?.frame).join(",")}`);
    }
    if (!inFreeRide() || !World.isReady() || player.spawned ||
        report?.generation !== World.getMissionGeneration() || !Array.isArray(report.anchors)) return;

    const anchors = new Map();
    for (const district of maps.current().districts) {
        const anchor = district.frame
            ? validAnchor(report.anchors.find((candidate) => candidate?.frame === district.frame), district.frame)
            : {position: district.position, direction: district.direction};
        if (anchor) anchors.set(district.id, anchor);
    }
    if (anchors.size < 2) return;
    const resume = resumeOf(player);
    if (resume) anchors.set(RESUME, {position: resume.position, direction: {x: Math.sin(resume.heading), y: 0, z: Math.cos(resume.heading)}});

    const previous = selections.get(player.id);
    const selection = {generation: report.generation, anchors, choice: previous?.choice ?? null};
    selections.set(player.id, selection);
    sendOpen(player, selection);
});

onClient("fr:selector:spawn", (player, request) => {
    const selection = selections.get(player.id);
    if (!inFreeRide() || !selection || !request || player.spawned || !World.isReady() ||
        selection.generation !== World.getMissionGeneration() || request.generation !== selection.generation) return;

    const resume = request.locationId === RESUME && selection.anchors.has(RESUME) ? resumeOf(player) : null;
    const district = resume ? nearestDistrict(selection, resume.position) : maps.current().districts.find((entry) => entry.id === request.locationId);
    const anchor = resume ? selection.anchors.get(RESUME) : district && selection.anchors.get(district.id);
    const character = CHARACTERS.find((entry) => entry.file === request.model);
    if (!anchor || !character) return;

    if (!player.setModel(character.file) || !player.spawn(anchor.position, anchorHeading(anchor))) {
        emit(player, "fr:selector:error", {message: "That entrance is unavailable. Try again."});
        return;
    }
    player.setVirtualWorld(MAIN_WORLD);
    previewWorlds.delete(player.id);
    // Dead players come back at their usual district, not the resume spot.
    const usual = profiles.get(player)?.district;
    const home = resume ? (selection.anchors.has(usual) ? usual : district?.id ?? RESUME) : district.id;
    selection.choice = {locationId: home, model: character.file};
    if (!resume) profiles.setField(player, "district", district.id);
    profiles.setField(player, "model", character.file);
    if (resume) {
        // The spawn handlers gave the starting kit; this life carries on the last one.
        life.equip(player, resume.weapons ?? [], resume.selected);
        if (resume.health > 0 && resume.health < 100) player.setHealth(resume.health);
        if (resume.wanted > 0) Police.setWanted(player, resume.wanted);
    }
    const where = resume ? "Where you left off" : district.name;
    emit(player, "fr:selector:accepted", {...selection.choice, district: where, character: character.name});
    console.log(`[freeride] ${player.nickname} entered ${where} as ${character.name}`);
});

// Dead players come back where they first entered the city; without a
// choice (another mission, or after a resource restart) life.js falls back
// to the native spawn hint.
life.setRespawnProvider((player) => {
    if (!inFreeRide()) return null;
    const selection = selections.get(player.id);
    const anchor = selection?.choice && selection.generation === World.getMissionGeneration()
        ? selection.anchors.get(selection.choice.locationId) : null;
    return anchor ? {position: anchor.position, heading: anchorHeading(anchor)} : null;
});

Events.on("playerDisconnect", (player) => {
    selections.delete(player.id);
    reportedReady.delete(player.id);
    previewWorlds.delete(player.id);
});
