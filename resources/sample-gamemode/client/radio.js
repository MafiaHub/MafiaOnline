// The car radio is a client-only HTML audio view. The directory page is not
// an audio source; radio.html plays the station's direct live MP3 stream.
const RADIO_PAGE = "fw://resources/mafia1online-sample/client/radio.html";
const STREAM_TIMEOUT_MS = 12_000;

const receiver = {vehicleId: null, view: null, online: false, startedAt: 0, reportedFailure: false, enabled: true};

function reportFailure() {
    if (receiver.vehicleId === null || receiver.reportedFailure) return;
    receiver.online = false;
    receiver.reportedFailure = true;
    Hud.showMessage("RADIO · Station unavailable", 0xe8c57d);
}

function stopRadio() {
    if (receiver.view !== null) {
        Web.emit(receiver.view, "radio:stop");
        Web.destroyView(receiver.view);
        receiver.view = null;
    }
    receiver.vehicleId = null;
    receiver.online = false;
    receiver.startedAt = 0;
    receiver.reportedFailure = false;
}

function startRadio(vehicle) {
    stopRadio();
    receiver.vehicleId = vehicle.id;
    receiver.startedAt = Date.now();
    try {
        receiver.view = Web.createView(RADIO_PAGE, {width: 1, height: 1, x: 0, y: 0, zIndex: -100, visible: true, focus: false});
    }
    catch (error) {
        console.warn(`[mafia1online-sample] Web radio view unavailable: ${error}`);
        reportFailure();
        return;
    }
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
}

Key.bind("f9", () => {
    receiver.enabled = !receiver.enabled;
    if (!receiver.enabled) {
        stopRadio();
        Hud.showMessage("RADIO · Off (F9 to tune in)", 0xe8c57d);
        return;
    }
    const player = LocalPlayer;
    const vehicle = player?.spawned && player.alive ? player.getVehicle() : null;
    if (vehicle) startRadio(vehicle);
    Hud.showMessage(vehicle ? "RADIO · Tuning in…" : "RADIO · On for your next car", 0xe8c57d);
});

Events.on("browserDocumentReady", ({viewId}) => {
    if (viewId === receiver.view && receiver.vehicleId !== null) Web.emit(viewId, "radio:play", {volume: 0.72});
});
Events.on("missionUnload", stopRadio);
Events.on("playerDeath", (player) => { if (player.isLocal) stopRadio(); });
Events.on("resourceStop", (name) => { if (name === "mafia1online-sample") stopRadio(); });

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
