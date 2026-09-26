// A lightweight field guide stays available after the selector closes. All
// activity progress comes from the server; this view only presents it.
const GUIDE_PAGE = "fw://resources/mafia1online-sample/client/guide.html";
const GUIDE_RESOURCE = "mafia1online-sample";
const GUIDE_SECTIONS = 3;
const GUIDE_WELCOME_MS = 18_000;
const GUIDE_TOUR_HINT_MS = 8_000;

const cityGuide = {
    view: null,
    pageReady: false,
    expanded: false,
    autoCloseAt: 0,
    section: 0,
    character: null,
    courier: false,
    tour: null,
    tourUpdatedAt: 0,
    lastSecond: -1,
};

function tourSecondsLeft() {
    if (!cityGuide.tour?.active) return 0;
    const elapsed = (Date.now() - cityGuide.tourUpdatedAt) / 1000;
    return Math.max(0, Math.ceil(cityGuide.tour.secondsRemaining - elapsed));
}

function guideState() {
    const player = LocalPlayer;
    return {
        expanded: cityGuide.expanded,
        section: cityGuide.section,
        character: cityGuide.character,
        courier: cityGuide.courier,
        driver: Boolean(player?.spawned && player.alive && player.getSeat() === 0),
        tour: cityGuide.tour ? {...cityGuide.tour, secondsRemaining: tourSecondsLeft()} : null,
    };
}

function sendGuideState() {
    if (cityGuide.view !== null && cityGuide.pageReady) Web.emit(cityGuide.view, "guide:state", guideState());
}

function closeGuide() {
    if (cityGuide.view !== null) Web.destroyView(cityGuide.view);
    cityGuide.view = null;
    cityGuide.pageReady = false;
    cityGuide.expanded = false;
    cityGuide.autoCloseAt = 0;
    cityGuide.lastSecond = -1;
}

function showGuide() {
    if (cityGuide.view !== null) return;
    try {
        cityGuide.view = Web.createView(GUIDE_PAGE, {visible: true, focus: false, zIndex: 75});
    }
    catch (error) {
        console.warn(`[mafia1online-sample] Free Ride guide could not open: ${error}`);
        Hud.showMessage("Use /help for Free Ride activities", 0xe8c57d);
        return;
    }
    cityGuide.pageReady = false;
}

Key.bind("f7", () => {
    if (!LocalPlayer?.spawned || !LocalPlayer.alive) return;
    showGuide();
    cityGuide.expanded = !cityGuide.expanded;
    cityGuide.autoCloseAt = 0;
    sendGuideState();
});

Key.bind("f8", () => {
    if (!LocalPlayer?.spawned || !LocalPlayer.alive) return;
    showGuide();
    cityGuide.expanded = true;
    cityGuide.autoCloseAt = 0;
    cityGuide.section = (cityGuide.section + 1) % GUIDE_SECTIONS;
    Sound.play("00_kompas.wav", {volume: 0.35});
    sendGuideState();
});

Events.on("browserDocumentReady", ({viewId}) => {
    if (viewId !== cityGuide.view) return;
    cityGuide.pageReady = true;
    sendGuideState();
});

Events.on("playerSpawn", (player) => {
    if (!player.isLocal) return;
    showGuide();
    cityGuide.expanded = true;
    cityGuide.autoCloseAt = Date.now() + GUIDE_WELCOME_MS;
    cityGuide.section = 0;
    sendGuideState();
});

Events.on("playerDeath", (player) => {
    if (!player.isLocal) return;
    cityGuide.expanded = false;
    cityGuide.autoCloseAt = 0;
    sendGuideState();
});

Events.on("sample:selector:accepted", (choice) => {
    if (!choice || typeof choice.locationId !== "string" || typeof choice.model !== "string") return;
    cityGuide.character = choice;
    sendGuideState();
});

Events.on("sample:shops:state", (state) => {
    cityGuide.courier = Boolean(state?.courier);
    sendGuideState();
});

Events.on("sample:tour", (tour) => {
    if (!tour || typeof tour.active !== "boolean") return;
    const wasActive = cityGuide.tour?.active === true;
    cityGuide.tour = tour;
    cityGuide.tourUpdatedAt = Date.now();
    if (tour.active && !wasActive && LocalPlayer?.spawned) {
        showGuide();
        cityGuide.expanded = true;
        cityGuide.section = 0;
        cityGuide.autoCloseAt = Date.now() + GUIDE_TOUR_HINT_MS;
    }
    sendGuideState();
});

Events.on("missionUnload", () => {
    closeGuide();
    cityGuide.character = null;
    cityGuide.courier = false;
    cityGuide.tour = null;
});

Events.on("resourceStop", (name) => {
    if (name === GUIDE_RESOURCE) closeGuide();
});

Events.on("render", () => {
    if (cityGuide.view === null) return;
    const now = Date.now();
    if (cityGuide.autoCloseAt && now >= cityGuide.autoCloseAt) {
        cityGuide.expanded = false;
        cityGuide.autoCloseAt = 0;
        sendGuideState();
    }
    const second = Math.floor(now / 1000);
    if (second !== cityGuide.lastSecond) {
        cityGuide.lastSecond = second;
        sendGuideState();
    }
});
