// The car radio is a client-only HTML audio view. The directory page is not
// an audio source; radio.html plays the station's direct live MP3 stream.
const RADIO_PAGE = "fw://resources/freeride/client/radio.html";
const STREAM_TIMEOUT_MS = 12_000;

// The station plays this loud at full media volume (Esc → Settings → Audio).
const STREAM_VOLUME = 0.72;
const RADIO_KEY = "f9";

// The view is made once and kept: a browser started on every car entry was
// a hitch exactly as the player got in. Only play and stop go to it.
const receiver = {vehicleId: null, view: null, ready: false, online: false, startedAt: 0, reportedFailure: false, enabled: true};

// The player may move F9 and flip the radio on the settings screen.
Settings.describeKey(RADIO_KEY, "Car radio on or off");
Settings.defineToggle("radio", "Car radio", receiver.enabled);
const radioKey = () => Settings.getKeyLabel(RADIO_KEY);
const streamVolume = () => STREAM_VOLUME * Settings.getMediaVolume();

function reportFailure() {
    if (receiver.vehicleId === null || receiver.reportedFailure) return;
    receiver.online = false;
    receiver.reportedFailure = true;
    Hud.showMessage("RADIO · Station unavailable", 0xe8c57d);
}

function stopRadio() {
    if (receiver.view !== null && receiver.vehicleId !== null) Web.emit(receiver.view, "radio:stop");
    receiver.vehicleId = null;
    receiver.online = false;
    receiver.startedAt = 0;
    receiver.reportedFailure = false;
}

function destroyRadio() {
    stopRadio();
    if (receiver.view !== null) Web.destroyView(receiver.view);
    receiver.view = null;
    receiver.ready = false;
}

function radioView() {
    if (receiver.view !== null) return receiver.view;
    try {
        receiver.view = Web.createView(RADIO_PAGE, {width: 1, height: 1, x: 0, y: 0, zIndex: -100, visible: true, focus: false});
    }
    catch (error) {
        console.warn(`[freeride] Web radio view unavailable: ${error}`);
        return null;
    }
    receiver.ready = false;
    const view = receiver.view;
    Web.on(view, "radio:playing", () => {
        if (view !== receiver.view || receiver.vehicleId === null) return;
        receiver.online = true;
        receiver.reportedFailure = false;
        Hud.showMessage("RADIO · Jazz Club Bandstand", 0xe8c57d);
    });
    Web.on(view, "radio:failed", () => {
        if (view === receiver.view) reportFailure();
    });
    return view;
}

function startRadio(vehicle) {
    stopRadio();
    receiver.vehicleId = vehicle.id;
    receiver.startedAt = Date.now();
    const view = radioView();
    if (view === null) {
        reportFailure();
        return;
    }
    // A page still loading plays once it is ready (browserDocumentReady).
    if (receiver.ready) Web.emit(view, "radio:play", {volume: streamVolume()});
}

function setRadio(enabled, announce) {
    receiver.enabled = enabled;
    Settings.setToggle("radio", enabled);
    if (!enabled) {
        stopRadio();
        if (announce) Hud.showMessage(`RADIO · Off (${radioKey()} or /radio to tune in)`, 0xe8c57d);
        return;
    }
    const player = LocalPlayer;
    const vehicle = player?.spawned && player.alive ? player.getVehicle() : null;
    if (vehicle && receiver.vehicleId === null) startRadio(vehicle);
    if (announce) Hud.showMessage(vehicle ? "RADIO · Tuning in…" : "RADIO · On for your next car", 0xe8c57d);
}

// The server keeps the choice with the player's record (/radio does the same).
Key.bind(RADIO_KEY, () => {
    setRadio(!receiver.enabled, true);
    Events.emitServer("fr:radio:set", {enabled: receiver.enabled});
});
Events.on("fr:radio:setting", ({enabled, announce}) => setRadio(enabled !== false, announce === true));
Events.on("settingToggle", (id, value, resource) => {
    if (resource !== "freeride" || id !== "radio" || value === receiver.enabled) return;
    setRadio(value, false);
    Events.emitServer("fr:radio:set", {enabled: value});
});
Events.on("mediaVolumeChange", () => {
    if (receiver.view !== null) Web.emit(receiver.view, "radio:volume", {volume: streamVolume()});
});

Events.on("browserDocumentReady", ({viewId}) => {
    if (viewId !== receiver.view) return;
    receiver.ready = true;
    if (receiver.vehicleId !== null) Web.emit(viewId, "radio:play", {volume: streamVolume()});
});
Events.on("missionUnload", stopRadio);
Events.on("playerDeath", (player) => { if (player.isLocal) stopRadio(); });
Events.on("resourceStop", (name) => { if (name === "freeride") destroyRadio(); });

Events.on("render", () => {
    const player = LocalPlayer;
    const vehicle = player?.spawned && player.alive ? player.getVehicle() : null;
    if (!receiver.enabled) return;
    if ((vehicle?.id ?? null) !== receiver.vehicleId) {
        if (vehicle) startRadio(vehicle);
        else if (receiver.vehicleId !== null) stopRadio();
    }
    if (receiver.vehicleId !== null && !receiver.online && Date.now() - receiver.startedAt > STREAM_TIMEOUT_MS) reportFailure();
});
