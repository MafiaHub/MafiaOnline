// Entering Lost Heaven: a camera flies over the city behind the login card.
// The server builds the flight (lib/flyover.js: a centripetal Catmull-Rom
// loop checked against the map's skyline, resampled at constant arc length
// with its bank angle) and this script plays it back, interpolating between
// the samples. After the very first registration the camera stops at the
// player's starting district for a one-time look; returning players go
// straight in. Only the server can turn any of it into a player life.
const FREE_RIDE_FRAMES = ["emeth_1", "emeth_2", "emeth_3", "emeth_4"];
const SELECTOR_MISSIONS = new Set(["freeride", "freeride_extended", "freekrajina"]);
const SELECTOR_PAGE = "fw://resources/freeride/client/selector.html";
const SELECTOR_RESOURCE = "freeride";
const NAVIGATION_SOUND = "00_kompas.wav";
const HELLO_RETRY_MS = 500;
const CAMERA_RECHECK_MS = 200;
const BEGIN_RETRY_MS = 1_000;
const SPAWN_TIMEOUT_MS = 12_000;
const FLYOVER_FOV = 62;
const LOOK_FOV = 54;

const picker = {
    active: false,
    open: false,
    phase: "connecting",
    generation: 0,
    anchors: [],
    view: null,
    pageReady: false,
    nextHelloAt: 0,
    nextBeginAt: 0,
    missingStartsReported: false,
    serverId: null,
    nickname: "",
    name: null,
    authenticated: null,
    message: "Finding your way into Lost Heaven…",
    error: null,
    pending: false,
    pendingSince: 0,
    flight: null,
    flightStart: 0,
    characters: [],
    fallbackModel: null,
    modelIndex: 0,
    lookAnchor: null,
    preview: null,
    cameraSide: 0,
    cameraChangedAt: 0,
};

function plain(vector) {
    return {x: vector.x, y: vector.y, z: vector.z};
}

function playTick() {
    Sound.play(NAVIGATION_SOUND, {volume: 0.65});
}

// ---- the flight --------------------------------------------------------

// The server's table: every `step` metres a position, a look target and a
// roll, flat as [x, y, z, tx, ty, tz, roll, ...], closing on itself.
function flightFrom(path) {
    if (!path || !Array.isArray(path.samples) || path.samples.length < 7 * 8 || path.samples.length % 7 !== 0) return null;
    if (!path.samples.every(Number.isFinite) || !(path.step > 0) || !(path.speed > 0)) return null;
    return {samples: path.samples, count: path.samples.length / 7, step: path.step, speed: path.speed};
}

function uniform(p0, p1, p2, p3, t) {
    return 0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (3 * p1 - p0 - 3 * p2 + p3) * t * t * t);
}

// The camera at `distance` metres along the loop. The samples are evenly
// spaced in arc length, so a uniform Catmull-Rom between them keeps the
// speed constant and the motion smooth.
function flightAt(flight, distance) {
    const {samples, count, step} = flight;
    const length = count * step;
    const along = ((distance % length) + length) % length / step;
    const i = Math.floor(along);
    const t = along - i;
    const value = (field) => uniform(samples[((i - 1 + count) % count) * 7 + field], samples[i * 7 + field],
        samples[((i + 1) % count) * 7 + field], samples[((i + 2) % count) * 7 + field], t);
    const position = {x: value(0), y: value(1), z: value(2)};
    const target = {x: value(3), y: value(4), z: value(5)};
    return {position, direction: {x: target.x - position.x, y: target.y - position.y, z: target.z - position.z}, roll: value(6)};
}

function flyFrame() {
    if (!picker.flight) return;
    const pose = flightAt(picker.flight, (Date.now() - picker.flightStart) / 1000 * picker.flight.speed);
    Camera.lock(pose.position, pose.direction, pose.roll);
}

function startFlight(path) {
    picker.flight = flightFrom(path);
    if (!picker.flight) return;
    // Every session begins somewhere else along the loop.
    picker.flightStart = Date.now() - Math.random() * picker.flight.count * picker.flight.step / picker.flight.speed * 1000;
    Camera.setFov(FLYOVER_FOV);
}

// ---- the page ----------------------------------------------------------

function sendToPage() {
    if (!picker.pageReady || picker.view === null) return;
    const model = picker.characters[picker.modelIndex];
    Web.emit(picker.view, "login:state", {
        phase: picker.phase,
        open: picker.open,
        serverId: picker.serverId,
        nickname: picker.nickname,
        name: picker.name,
        authenticated: picker.authenticated,
        pending: picker.pending,
        message: picker.message,
        error: picker.error,
        model: model ? {name: model.name, role: model.role} : null,
        modelIndex: picker.modelIndex,
        modelCount: picker.characters.length,
    });
}

function bindPage(view) {
    Web.on(view, "login:submit", (request) => {
        if (view !== picker.view || picker.phase !== "auth" || picker.pending || !request) return;
        const mode = request.mode === "register" ? "register" : "login";
        if (typeof request.name !== "string" || typeof request.password !== "string") return;
        picker.pending = true;
        picker.pendingSince = Date.now();
        picker.error = null;
        picker.message = mode === "register" ? "Writing your name in the book…" : "Checking the book…";
        sendToPage();
        Events.emitServer("fr:login:submit", {generation: picker.generation, mode, name: request.name, password: request.password,
            remember: Boolean(request.remember), logoutOthers: request.logoutOthers === true});
    });
    Web.on(view, "login:token", (request) => {
        if (view !== picker.view || picker.phase !== "auth" || picker.pending || typeof request?.token !== "string") return;
        picker.pending = true;
        picker.pendingSince = Date.now();
        picker.error = null;
        picker.message = "Welcome back. The city is opening…";
        sendToPage();
        Events.emitServer("fr:login:token", {generation: picker.generation, token: request.token});
    });
    // Log out of a remembered account: the server throws the token away.
    Web.on(view, "login:forget", (request) => {
        if (view === picker.view && typeof request?.token === "string") Events.emitServer("fr:login:forget", {token: request.token});
    });
    Web.on(view, "login:look:move", (request) => {
        if (view === picker.view) moveModel(request?.step === -1 ? -1 : 1);
    });
    Web.on(view, "login:look:camera", () => {
        if (view === picker.view) rotateCamera();
    });
    Web.on(view, "login:look:confirm", () => {
        if (view === picker.view) confirmLook();
    });
}

// ---- the one-time look -------------------------------------------------

function freePreview() {
    picker.preview?.destroy();
    picker.preview = null;
}

function cameraFor(anchor, side) {
    const {position, direction} = anchor;
    const length = Math.hypot(direction.x, direction.z) || 1;
    const forward = {x: direction.x / length, z: direction.z / length};
    const directions = [forward, {x: -forward.z, z: forward.x},
        {x: -forward.x, z: -forward.z}, {x: forward.z, z: -forward.x}];
    const toward = directions[side];
    const eye = {x: position.x + toward.x * 4.0, y: position.y + 2.1, z: position.z + toward.z * 4.0};
    const subject = {x: position.x, y: position.y + 1.25, z: position.z};
    Camera.lock(eye, {x: subject.x - eye.x, y: subject.y - eye.y, z: subject.z - eye.z});
    Camera.setFov(LOOK_FOV);
    picker.cameraChangedAt = Date.now();
}

function showLook() {
    const anchor = picker.lookAnchor;
    if (!anchor) return;
    freePreview();
    // Native humans require more than a file that merely opens. The scripting
    // probe uses the same full-skeleton check as the player's model swap.
    let attempts = picker.characters.length;
    while (attempts-- > 0) {
        const model = picker.characters[picker.modelIndex];
        picker.preview = Scene.createHumanFrame(model.file, anchor.position, anchor.direction);
        if (picker.preview !== null) break;
        picker.modelIndex = (picker.modelIndex + 1) % picker.characters.length;
    }
    if (picker.preview === null) {
        skipLook();
        return;
    }
    picker.cameraSide = 0;
    cameraFor(anchor, picker.cameraSide);
    picker.message = "How will Lost Heaven first see you? What you become is up to you.";
    sendToPage();
}

function moveModel(step) {
    if (picker.phase !== "look" || picker.pending || picker.characters.length < 2) return;
    picker.modelIndex = (picker.modelIndex + step + picker.characters.length) % picker.characters.length;
    playTick();
    showLook();
}

function rotateCamera() {
    if (picker.phase !== "look" || picker.pending || !picker.lookAnchor) return;
    picker.cameraSide = (picker.cameraSide + 1) % 4;
    cameraFor(picker.lookAnchor, picker.cameraSide);
    playTick();
    sendToPage();
}

function confirmLook() {
    if (picker.phase !== "look" || picker.pending || picker.preview === null) return;
    picker.pending = true;
    picker.pendingSince = Date.now();
    picker.message = "Your story begins…";
    playTick();
    sendToPage();
    Events.emitServer("fr:login:look", {generation: picker.generation, model: picker.characters[picker.modelIndex].file});
}

// Without a look this client can show, the player starts as the server's
// first character instead of being stuck here.
function skipLook() {
    freePreview();
    picker.phase = "entering";
    picker.pending = true;
    picker.pendingSince = Date.now();
    picker.message = "Your story begins…";
    sendToPage();
    if (picker.fallbackModel) Events.emitServer("fr:login:look", {generation: picker.generation, model: picker.fallbackModel});
}

function beginLook(result) {
    const anchor = result.anchor;
    const valid = anchor && [anchor.position, anchor.direction].every((point) => point && [point.x, point.y, point.z].every(Number.isFinite));
    const offered = Array.isArray(result.characters) ? result.characters.filter((model) => typeof model?.file === "string") : [];
    picker.fallbackModel = offered[0]?.file ?? picker.fallbackModel;
    picker.characters = offered.filter((model) => Scene.canUseHumanModel(model.file));
    if (!valid || !picker.characters.length) {
        skipLook();
        return;
    }
    picker.phase = "look";
    picker.flight = null;
    picker.lookAnchor = anchor;
    picker.modelIndex = 0;
    showLook();
}

// ---- lifecycle ---------------------------------------------------------

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

    Object.assign(picker, {
        active: true, open: false, phase: "connecting", generation: World.getMissionGeneration(), anchors,
        pageReady: false, nextHelloAt: 0, missingStartsReported: false, authenticated: null, error: null,
        pending: false, flight: null, characters: [], lookAnchor: null, message: "Finding your way into Lost Heaven…",
    });
    picker.view = Web.createView(SELECTOR_PAGE, {visible: true, focus: false, zIndex: 90});
    bindPage(picker.view);
    Hud.hideWatch();
    Hud.hideScore();
    // Until the server sends the flight, hold the map on a start frame.
    cameraFor(anchors.find((anchor) => anchor.frame === "emeth_3") ?? anchors[0], 0);
}

function end() {
    if (!picker.active) return;
    freePreview();
    if (picker.view !== null) Web.destroyView(picker.view);
    picker.view = null;
    picker.pageReady = false;
    picker.active = false;
    picker.open = false;
    picker.flight = null;
    picker.characters = [];
    picker.lookAnchor = null;
    Camera.unlock();
}

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
    Web.focusView(picker.view, true);
    sendToPage();
});

Events.on("fr:login:open", (state) => {
    if (!picker.active || picker.open || state?.generation !== picker.generation || state.mission !== World.getMission()) return;
    picker.open = true;
    picker.serverId = typeof state.serverId === "string" ? state.serverId : null;
    picker.nickname = typeof state.nickname === "string" ? state.nickname : "";
    picker.authenticated = typeof state.authenticated === "string" ? state.authenticated : null;
    picker.fallbackModel = typeof state.fallbackModel === "string" ? state.fallbackModel : null;
    startFlight(state.flight);
    if (picker.authenticated) {
        // Already logged in (a new map): the server sends the next step.
        picker.phase = "entering";
        picker.name = picker.authenticated;
        picker.message = `Welcome back, ${picker.authenticated}. The city is opening…`;
    } else {
        picker.phase = "auth";
        picker.message = "";
    }
    sendToPage();
});

Events.on("fr:login:result", (result) => {
    if (!picker.active || !result) return;
    picker.pending = false;
    if (!result.ok) {
        picker.error = {field: typeof result.field === "string" ? result.field : "form",
            message: typeof result.message === "string" ? result.message : "That did not work. Try again.", code: typeof result.code === "string" ? result.code : null};
        picker.message = "";
        sendToPage();
        return;
    }
    picker.error = null;
    picker.name = typeof result.name === "string" ? result.name : picker.name;
    picker.authenticated = picker.name;
    // The page keeps the replacement token for the next start.
    if (picker.pageReady && picker.view !== null) {
        Web.emit(picker.view, "login:remember", typeof result.token === "string" && picker.serverId
            ? {serverId: picker.serverId, name: picker.name, token: result.token, expires: result.expires}
            : {serverId: picker.serverId, name: picker.name, token: null});
    }
    if (result.needsLook) {
        beginLook(result);
    } else {
        picker.phase = "entering";
        picker.pending = true;
        picker.pendingSince = Date.now();
        picker.message = `Welcome back, ${picker.name}. The city is opening…`;
        sendToPage();
    }
});

Events.on("fr:login:accepted", () => {
    if (!picker.active) return;
    picker.phase = "entering";
    picker.pending = true;
    picker.pendingSince = Date.now();
    picker.message = "The city is opening. One moment…";
    sendToPage();
});

Events.on("fr:login:error", (error) => {
    if (!picker.active) return;
    picker.pending = false;
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
    // Until the server answers, and after a new map while logged in, the
    // client keeps reporting its start frames; the server needs them to
    // place anyone.
    if ((!picker.open || picker.phase === "entering") && now >= picker.nextHelloAt) {
        Events.emitServer("fr:login:hello", {generation: picker.generation, anchors: picker.anchors, opened: picker.open});
        picker.nextHelloAt = now + (picker.open ? HELLO_RETRY_MS * 4 : HELLO_RETRY_MS);
    }
    if (picker.pending && now - picker.pendingSince > SPAWN_TIMEOUT_MS) {
        picker.pending = false;
        picker.pendingSince = Number.POSITIVE_INFINITY;
        picker.message = picker.phase === "entering" ? "Your place is ready. Waiting for your player…" : "The city did not answer. Try again.";
        sendToPage();
    }
    if (picker.phase === "look") {
        const anchor = picker.lookAnchor;
        if (anchor && picker.cameraSide < 3 && now - picker.cameraChangedAt >= CAMERA_RECHECK_MS) {
            const head = {x: anchor.position.x, y: anchor.position.y + 1.25, z: anchor.position.z};
            if (!Scene.projectWorld(head, true)?.visible) {
                picker.cameraSide += 1;
                cameraFor(anchor, picker.cameraSide);
            } else picker.cameraChangedAt = Number.POSITIVE_INFINITY;
        }
        return;
    }
    flyFrame();
});
