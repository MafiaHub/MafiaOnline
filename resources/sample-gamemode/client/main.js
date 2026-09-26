// Runs in each client's sandboxed V8. The server owns the world; this script
// only drives the local HUD and sounds and talks to server/main.js.
let countdownRunning = false;
let courierCompass = null;
let tourCompass = null;
let tourActive = false;
let manualCompass = null;

function updateCompass() {
    if (tourActive && tourCompass) {
        Hud.setCompassTarget(tourCompass);
        return;
    }
    if (courierCompass) {
        Hud.setCompassTarget(courierCompass);
        return;
    }
    const target = typeof manualCompass?.playerId === "number"
        ? World.getPlayers().find((player) => player.id === manualCompass.playerId) : null;
    if (target) Hud.setCompassTarget(target);
    else if (manualCompass?.position) Hud.setCompassTarget(manualCompass.position);
    else Hud.clearCompassTarget();
}

Events.on("missionReady", ({mission, missionGeneration}) => {
    console.log(`[mafia1online-sample] Mission ${mission} (generation ${missionGeneration}) loaded`);
    Hud.showMessage(`Welcome to ${mission}`, 0xe8c070);
    Fade.in(1.5);
});

Events.on("playerSpawn", (player) => {
    if (!player.isLocal) return;
    Hud.announce("Good luck", 2);
    Events.emitServer("sample:spawned", {mission: World.getMission()});
});

Events.on("playerStreamIn", (player) => {
    if (!player.isLocal && LocalPlayer?.spawned) Hud.showMessage(`${player.nickname} is nearby`, 0xa0c0ff);
});

Events.on("pickupStreamIn", (pickup) => {
    console.log(`[mafia1online-sample] Weapon pickup ${pickup.id}: item ${pickup.weaponId}, ${pickup.loaded}+${pickup.reserve} rounds`);
});

Events.on("pickupStreamOut", (pickupId, lastKnown) => {
    console.log(`[mafia1online-sample] Weapon pickup ${pickupId} removed: item ${lastKnown.weaponId}, ${lastKnown.loaded}+${lastKnown.reserve} rounds`);
});

Events.on("playerDeath", (player) => {
    if (player.isLocal) Sound.play("01b_srdce.wav");
});

// The server sends these with player.emit(name, JSON.stringify(data)).
Events.on("sample:hud", (data) => {
    switch (data?.action) {
    case "countdown":
        countdownRunning = Hud.startCountdown(data.seconds ?? 30);
        break;
    case "watch":
        Hud.showWatch(data.hours ?? 12, data.minutes ?? 0);
        break;
    case "score":
        Hud.addScore(data.points ?? 100);
        break;
    case "compass": {
        if (!tourActive) manualCompass = {playerId: data.playerId, position: data.position};
        updateCompass();
        Sound.play("00_kompas.wav");
        break;
    }
    case "compassClear":
        manualCompass = null;
        updateCompass();
        break;
    case "clear":
        Hud.hideWatch();
        Hud.hideScore();
        manualCompass = null;
        updateCompass();
        Camera.setSwing(0);
        countdownRunning = false;
        break;
    case "fade":
        Fade.out(1);
        setTimeout(() => Fade.in(1), 2000);
        break;
    case "swing":
        Camera.setSwing(data.intensity ?? 30);
        break;
    case "sound": {
        const wave = data.wave ?? "00_dog.wav";
        if (Sound.play(wave) === null) Hud.showMessage(`Could not play ${wave}`);
        break;
    }
    }
});

Events.on("countdownEnd", () => {
    if (!countdownRunning) return;
    countdownRunning = false;
    Hud.announce("Time is up!", 3);
    Events.emitServer("sample:countdownEnd", {});
});

Events.on("sample:shops:state", (state) => {
    const pete = state?.courier && state.generation === World.getMissionGeneration() && Array.isArray(state.shops)
        ? state.shops.find((shop) => shop.id === "pete") : null;
    courierCompass = pete?.position ?? null;
    updateCompass();
});

Events.on("sample:tour", (state) => {
    tourActive = Boolean(state?.active);
    tourCompass = tourActive ? state.position : null;
    updateCompass();
});

Events.on("missionUnload", () => {
    courierCompass = null;
    tourCompass = null;
    tourActive = false;
    manualCompass = null;
    Hud.clearCompassTarget();
});
