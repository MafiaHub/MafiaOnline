// A world-space wardrobe: the mission's own Free Ride start frames place the
// preview and camera. Only the server can turn the choice into a player life.
const FREE_RIDE_FRAMES = ["emeth_1", "emeth_2", "emeth_3", "emeth_4"];
const SELECTOR_MISSIONS = new Set(["freeride", "freeride_extended", "freekrajina"]);
const SELECTOR_PAGE = "fw://resources/freeride/client/selector.html";
const SELECTOR_RESOURCE = "freeride";
const NAVIGATION_SOUND = "00_kompas.wav";
const READY_RETRY_MS = 500;
const CAMERA_RECHECK_MS = 200;
const BEGIN_RETRY_MS = 1_000;
const SPAWN_TIMEOUT_MS = 12_000;

const picker = {
    active: false,
    ready: false,
    pending: false,
    accepted: false,
    generation: 0,
    anchors: [],
    locations: [],
    characters: [],
    locationIndex: 0,
    modelIndex: 0,
    preview: null,
    view: null,
    pageReady: false,
    nextReadyAt: 0,
    pendingSince: 0,
    cameraSide: 0,
    cameraChangedAt: 0,
    reportedReady: false,
    nextBeginAt: 0,
    missingStartsReported: false,
    message: "Finding your place in Lost Heaven…",
};

function plain(vector) {
    return {x: vector.x, y: vector.y, z: vector.z};
}

function playTick() {
    Sound.play(NAVIGATION_SOUND, {volume: 0.65});
}

function freePreview() {
    picker.preview?.destroy();
    picker.preview = null;
}

function sendToPage() {
    if (!picker.pageReady || picker.view === null) return;
    const location = picker.locations[picker.locationIndex];
    const model = picker.characters[picker.modelIndex];
    Web.emit(picker.view, "selector:state", {
        location: location ? {name: location.name, subtitle: location.subtitle} : null,
        locationIndex: picker.locationIndex,
        locationCount: picker.locations.length,
        model: model ? {name: model.name, role: model.role} : null,
        modelIndex: picker.modelIndex,
        modelCount: picker.characters.length,
        ready: picker.ready,
        pending: picker.pending,
        cameraSide: picker.cameraSide,
        message: picker.message,
    });
}

function cameraFor(location, side) {
    const {position, direction} = location;
    const length = Math.hypot(direction.x, direction.z) || 1;
    const forward = {x: direction.x / length, z: direction.z / length};
    const directions = [forward, {x: -forward.z, z: forward.x},
        {x: -forward.x, z: -forward.z}, {x: forward.z, z: -forward.x}];
    const toward = directions[side];
    const eye = {x: position.x + toward.x * 4.0, y: position.y + 2.1, z: position.z + toward.z * 4.0};
    const subject = {x: position.x, y: position.y + 1.25, z: position.z};
    Camera.lock(eye, {x: subject.x - eye.x, y: subject.y - eye.y, z: subject.z - eye.z});
    Camera.setFov(54);
    picker.cameraChangedAt = Date.now();
}

function showChoice() {
    const location = picker.locations[picker.locationIndex];
    if (!location) return;
    freePreview();

    // Native humans require more than a file that merely opens. The scripting
    // probe uses the same full-skeleton check as the player's model swap.
    let attempts = picker.characters.length;
    while (attempts-- > 0) {
        const model = picker.characters[picker.modelIndex];
        picker.preview = Scene.createHumanFrame(model.file, location.position, location.direction);
        if (picker.preview !== null) break;
        picker.modelIndex = (picker.modelIndex + 1) % picker.characters.length;
    }
    if (picker.preview === null) {
        picker.message = "The costumes could not be loaded.";
        sendToPage();
        return;
    }

    picker.cameraSide = 0;
    cameraFor(location, picker.cameraSide);
    picker.message = "Choose a district and a character, then enter the city.";
    sendToPage();
}

function begin() {
    if (picker.active || !SELECTOR_MISSIONS.has(World.getMission()) || LocalPlayer?.spawned) return;
    const anchors = FREE_RIDE_FRAMES.map((frame) => {
        const worldFrame = World.findWorldFrame(frame);
        const position = worldFrame?.getWorldPosition();
        const direction = worldFrame?.getWorldDirection();
        return position && direction ? {frame, position: plain(position), direction: plain(direction)} : null;
    }).filter(Boolean);
    if (!anchors.some((anchor) => anchor.frame === FREE_RIDE_FRAMES[0]) || anchors.length < 2) {
        picker.nextBeginAt = Date.now() + BEGIN_RETRY_MS;
        if (!picker.missingStartsReported) {
            picker.missingStartsReported = true;
            console.warn("[freeride] Waiting for Free Ride start frames");
        }
        return;
    }

    picker.active = true;
    picker.ready = false;
    picker.pending = false;
    picker.accepted = false;
    picker.generation = World.getMissionGeneration();
    picker.anchors = anchors;
    picker.locations = [];
    picker.characters = [];
    picker.locationIndex = 0;
    picker.modelIndex = 0;
    picker.message = "Finding your place in Lost Heaven…";
    picker.pageReady = false;
    picker.reportedReady = false;
    picker.missingStartsReported = false;
    picker.view = Web.createView(SELECTOR_PAGE, {visible: true, focus: false, zIndex: 90});
    picker.nextReadyAt = 0;
    Hud.hideWatch();
    Hud.hideScore();
    Camera.setFov(54);
    // Keep the map visible while the server confirms its native spawn hint.
    cameraFor(anchors.find((anchor) => anchor.frame === "emeth_3") ?? anchors[0], 0);
}

function end() {
    if (!picker.active) return;
    freePreview();
    if (picker.view !== null) Web.destroyView(picker.view);
    picker.view = null;
    picker.pageReady = false;
    picker.active = false;
    picker.ready = false;
    picker.pending = false;
    picker.accepted = false;
    picker.locations = [];
    picker.characters = [];
    Camera.unlock();
}

function moveLocation(step) {
    if (!picker.ready || picker.pending || picker.locations.length < 2) return;
    picker.locationIndex = (picker.locationIndex + step + picker.locations.length) % picker.locations.length;
    playTick();
    showChoice();
}

function moveModel(step) {
    if (!picker.ready || picker.pending || picker.characters.length < 2) return;
    picker.modelIndex = (picker.modelIndex + step + picker.characters.length) % picker.characters.length;
    playTick();
    showChoice();
}

function rotateCamera() {
    if (!picker.ready || picker.pending || !picker.locations.length) return;
    picker.cameraSide = (picker.cameraSide + 1) % 4;
    cameraFor(picker.locations[picker.locationIndex], picker.cameraSide);
    playTick();
    sendToPage();
}

function enterCity() {
    if (!picker.ready || picker.pending || picker.preview === null) return;
    const location = picker.locations[picker.locationIndex];
    const model = picker.characters[picker.modelIndex];
    picker.pending = true;
    picker.accepted = false;
    picker.pendingSince = Date.now();
    picker.message = "Your story begins…";
    playTick();
    sendToPage();
    Events.emitServer("fr:selector:spawn", {generation: picker.generation, locationId: location.id, model: model.file});
}

Key.bind("up", () => moveLocation(-1));
Key.bind("down", () => moveLocation(1));
Key.bind("left", () => moveModel(-1));
Key.bind("right", () => moveModel(1));
Key.bind("enter", enterCity);
Key.bind("c", rotateCamera);

Events.on("missionReady", begin);
Events.on("resourceStart", (name) => {
    if (name === SELECTOR_RESOURCE && World.isReady()) begin();
});
Events.on("missionUnload", end);
Events.on("resourceStop", (name) => {
    if (name === SELECTOR_RESOURCE) end();
});
Events.on("playerSpawn", (player) => {
    if (player.isLocal) end();
});
Events.on("browserDocumentReady", (event) => {
    if (!picker.active || event.viewId !== picker.view) return;
    picker.pageReady = true;
    sendToPage();
});

Events.on("fr:selector:open", (state) => {
    if (!picker.active || state?.generation !== picker.generation || state.mission !== World.getMission() ||
        !Array.isArray(state.locations) || !Array.isArray(state.characters)) return;
    picker.locations = state.locations;
    picker.characters = state.characters.filter((model) => Scene.canUseHumanModel(model.file));
    if (picker.locations.length < 2 || !picker.characters.length) {
        picker.message = "The city has no starting places or characters for this mission.";
        sendToPage();
        return;
    }
    // Returning players start on the district and character they last chose.
    picker.ready = true;
    picker.locationIndex = Math.max(0, picker.locations.findIndex((location) => location.id === state.preferred?.locationId));
    picker.modelIndex = Math.max(0, picker.characters.findIndex((model) => model.file === state.preferred?.model));
    showChoice();
});

Events.on("fr:selector:accepted", () => {
    if (picker.active && picker.pending) {
        picker.accepted = true;
        picker.message = "The city is opening. One moment…";
    }
    sendToPage();
});
Events.on("fr:selector:error", (error) => {
    picker.pending = false;
    picker.accepted = false;
    picker.message = typeof error?.message === "string" ? error.message : "That entrance is unavailable. Try again.";
    sendToPage();
});

Events.on("render", () => {
    const now = Date.now();
    if (!picker.active) {
        if (World.isReady() && SELECTOR_MISSIONS.has(World.getMission()) && !LocalPlayer?.spawned && now >= picker.nextBeginAt) begin();
        return;
    }
    if (LocalPlayer?.spawned) {
        end();
        return;
    }
    if (!picker.ready && now >= picker.nextReadyAt) {
        if (!picker.reportedReady) {
            picker.reportedReady = true;
            console.log(`[freeride] Requesting selector choices for mission generation ${picker.generation}`);
        }
        Events.emitServer("fr:selector:ready", {generation: picker.generation, anchors: picker.anchors});
        picker.nextReadyAt = now + READY_RETRY_MS;
    }
    if (picker.pending && now - picker.pendingSince > SPAWN_TIMEOUT_MS) {
        if (picker.accepted) {
            picker.message = "Your place is ready. Waiting for your player…";
            picker.pendingSince = Number.POSITIVE_INFINITY;
        } else {
            picker.pending = false;
            picker.message = "The city did not answer. Press Enter to try again.";
        }
        sendToPage();
    }
    const location = picker.locations[picker.locationIndex];
    if (location && picker.cameraSide < 3 && now - picker.cameraChangedAt >= CAMERA_RECHECK_MS) {
        const head = {x: location.position.x, y: location.position.y + 1.25, z: location.position.z};
        if (!Scene.projectWorld(head, true)?.visible) {
            picker.cameraSide += 1;
            cameraFor(location, picker.cameraSide);
        }
        else picker.cameraChangedAt = Number.POSITIVE_INFINITY;
    }
});
