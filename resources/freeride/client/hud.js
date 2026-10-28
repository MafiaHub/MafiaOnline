// The Free Ride HUD: one transparent web view for toasts, the kill feed,
// activity cards, the race panel and the F2 city menu. F1 stays the mod's own
// player list and Tab the retail map. The server decides everything shown here; this script only relays its events,
// reads the replicated state bags and arbitrates the single native compass.
const HUD_PAGE = "fw://resources/freeride/client/hud.html";
const HUD_RESOURCE = "freeride";
const MENU_REFRESH_MS = 3000;
const COMPASS_CHECK_MS = 500;

const hud = {
    view: null,
    pageReady: false,
    queue: [],
    menuOpen: false,
    menuTab: "city",
    menuTimer: null,
    carNames: null,
    pickerOpen: false,
    lastPlayerState: "",
    compass: new Map(),
    compassKey: null,
    compassCheckedAt: 0,
};

function post(name, payload) {
    if (hud.view === null || !hud.pageReady) {
        hud.queue.push([name, payload]);
        if (hud.queue.length > 60) hud.queue.shift();
        return;
    }
    Web.emit(hud.view, name, payload ?? {});
}

function openView() {
    if (hud.view !== null) return;
    try {
        hud.view = Web.createView(HUD_PAGE, {visible: true, focus: false, zIndex: 70});
    } catch (error) {
        console.warn(`[freeride] HUD view could not open: ${error}`);
        return;
    }
    hud.pageReady = false;
    const view = hud.view;
    Web.on(view, "hud:command", (request) => {
        if (view !== hud.view || typeof request?.text !== "string") return;
        Events.emitServer("fr:menu:command", {text: request.text});
        setTimeout(requestMenu, 350);
    });
    Web.on(view, "hud:tab", (request) => {
        if (view !== hud.view) return;
        hud.menuTab = typeof request?.tab === "string" ? request.tab : "city";
        requestMenu();
    });
    Web.on(view, "hud:close", () => { if (view === hud.view) closeMenu(); });
    Web.on(view, "hud:car", () => {
        if (view !== hud.view) return;
        closeMenu();
        Events.emitServer("fr:menu:command", {text: "/car"});
    });
}

function closeView() {
    closeMenu();
    if (hud.view !== null) Web.destroyView(hud.view);
    hud.view = null;
    hud.pageReady = false;
    hud.queue = [];
}

function requestMenu() {
    if (hud.menuOpen) Events.emitServer("fr:menu:request", {tab: hud.menuTab});
}

function openMenu(tab) {
    if (!LocalPlayer?.spawned || hud.pickerOpen) return;
    openView();
    if (tab) hud.menuTab = tab;
    hud.menuOpen = true;
    post("menu:open", {tab: hud.menuTab});
    if (hud.view !== null) Web.focusView(hud.view, true);
    requestMenu();
    clearInterval(hud.menuTimer);
    hud.menuTimer = setInterval(requestMenu, MENU_REFRESH_MS);
    Sound.play("00_kompas.wav", {volume: 0.35});
}

function closeMenu() {
    if (!hud.menuOpen) return;
    hud.menuOpen = false;
    clearInterval(hud.menuTimer);
    hud.menuTimer = null;
    post("menu:close");
    if (hud.view !== null) Web.focusView(hud.view, false);
}

// Several systems want the compass; the highest priority with a target the
// client can resolve wins.
function resolveTarget(target) {
    if (target.position) return target.position;
    if (typeof target.playerId === "number") return World.getPlayers().find((player) => player.id === target.playerId) ?? null;
    if (typeof target.vehicleId === "number") return World.getVehicles().find((vehicle) => vehicle.id === target.vehicleId) ?? null;
    return null;
}

function updateCompass(force) {
    const now = Date.now();
    if (!force && now - hud.compassCheckedAt < COMPASS_CHECK_MS) return;
    hud.compassCheckedAt = now;
    let best = null;
    for (const [source, entry] of hud.compass) {
        const resolved = resolveTarget(entry.target);
        if (resolved && (!best || entry.priority > best.priority)) best = {source, priority: entry.priority, resolved, key: `${source}:${JSON.stringify(entry.target)}`};
    }
    const key = best?.key ?? null;
    if (key === hud.compassKey) return;
    hud.compassKey = key;
    if (best) Hud.setCompassTarget(best.resolved);
    else Hud.clearCompassTarget();
}

function relay(serverEvent, pageEvent = serverEvent.slice(3)) {
    Events.on(serverEvent, (data) => post(pageEvent, data));
}

relay("fr:toast");
relay("fr:feed");
relay("fr:card");
relay("fr:xp");
relay("fr:voice");

relay("fr:race:state", "race");

Events.on("fr:profile", (profile) => post("profile", profile));
Events.on("fr:levelup", (data) => {
    post("levelup", data);
    Sound.play("beep50.wav", {volume: 0.6});
    Hud.announce(data.promoted ? data.rank.toUpperCase() : `LEVEL ${data.level}`, 3);
});
Events.on("fr:menu:open", (request) => openMenu(request?.tab));
// The export list names models by file; the stock catalog has their names.
Events.on("fr:menu:data", (data) => {
    if (hud.carNames === null || !hud.carNames.size) {
        hud.carNames = new Map(Scene.getCarCatalog().map((entry) => [entry.model.toLowerCase(), entry.name]));
    }
    for (const entry of data?.export?.models ?? []) entry.name = hud.carNames.get(entry.model) ?? entry.model.replace(/00\.i3d$/i, "");
    post("menu:data", data);
});
Events.on("fr:compass", (request) => {
    if (typeof request?.source !== "string") return;
    if (request.target) hud.compass.set(request.source, {priority: request.priority ?? 0, target: request.target});
    else hud.compass.delete(request.source);
    updateCompass(true);
});
Events.on("voiceStart", () => { if (LocalPlayer) post("voice", {id: LocalPlayer.id, name: LocalPlayer.nickname, talking: true}); });
Events.on("voiceStop", () => { if (LocalPlayer) post("voice", {id: LocalPlayer.id, name: LocalPlayer.nickname, talking: false}); });
Events.onLocal("fr:car:picker", ({open}) => {
    hud.pickerOpen = Boolean(open);
    if (hud.pickerOpen) closeMenu();
    post("visibility", {hidden: hud.pickerOpen});
});

Key.bind("f2", () => {
    if (hud.menuOpen) closeMenu();
    else openMenu();
});

Events.on("browserDocumentReady", ({viewId}) => {
    if (viewId !== hud.view) return;
    hud.pageReady = true;
    Events.emitServer("fr:hud:ready", {});
    const queued = hud.queue;
    hud.queue = [];
    for (const [name, payload] of queued) Web.emit(hud.view, name, payload ?? {});
    hud.lastPlayerState = "";
});

Events.on("missionReady", () => {
    openView();
    hud.compassKey = null;
    updateCompass(true);
});
Events.on("missionUnload", () => {
    closeMenu();
    hud.compass.clear();
    hud.compassKey = null;
    post("reset");
});
Events.on("resourceStart", (name) => { if (name === HUD_RESOURCE && World.isReady()) openView(); });
Events.on("resourceStop", (name) => { if (name === HUD_RESOURCE) closeView(); });

Events.on("render", () => {
    if (hud.view === null) return;
    const player = LocalPlayer;
    const state = `${Boolean(player?.spawned)}:${Boolean(player?.alive)}`;
    if (state !== hud.lastPlayerState && hud.pageReady) {
        hud.lastPlayerState = state;
        post("player", {spawned: Boolean(player?.spawned), alive: Boolean(player?.alive), id: player?.id ?? null});
        if (!player?.alive) closeMenu();
    }
    updateCompass(false);
});
