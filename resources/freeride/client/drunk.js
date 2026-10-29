// What drink does to this player's eyes (server/drunk.js keeps the level
// as the "fr.drunk" state key, 0 to 100 in steps of 5). The world sways with
// the retail boat swing (CAMERA_SETSWING), the view breathes in and out
// (the camera's field of view), a warm haze and dark edges close in, and a
// wasted player's view lurches now and then. A blackout holds the screen
// dark until the server has carried them out.
const DRUNK_KEY = "fr.drunk";
const SWING_FROM = 20;
const SWING_MAX = 70;
const WOBBLE_FROM = 25;
const WOBBLE_DEGREES = 6;
const WOBBLE_PERIOD_MS = 4500;
const HAZE_FROM = 10;
const LURCH_FROM = 70;
const LURCH_SWING = 100;
const LURCH_MS = 1200;
const BLACKOUT_GRACE_MS = 3000;

const daze = {swing: 0, baseFov: null, lurchUntil: 0, nextLurch: 0, blackout: false, blackoutTimer: null};

function drunkLevel() {
    const player = LocalPlayer;
    if (!player?.spawned || !player.alive) return 0;
    const value = Number(player.state.get(DRUNK_KEY) ?? 0);
    return Number.isFinite(value) ? Math.max(0, Math.min(100, value)) : 0;
}

function setSwing(value) {
    const rounded = Math.round(value);
    if (rounded === daze.swing) return;
    if (Camera.setSwing(rounded) !== false) daze.swing = rounded;
}

function restoreFov() {
    if (daze.baseFov === null) return;
    Camera.setFov(daze.baseFov);
    daze.baseFov = null;
}

function sober() {
    setSwing(0);
    restoreFov();
    daze.lurchUntil = 0;
    daze.nextLurch = 0;
}

function endBlackout(seconds) {
    if (!daze.blackout) return;
    daze.blackout = false;
    if (daze.blackoutTimer !== null) clearTimeout(daze.blackoutTimer);
    daze.blackoutTimer = null;
    Fade.in(seconds);
}

function drawHaze(level, now) {
    const {width, height} = Web.getScreenSize();
    const pulse = 0.85 + 0.15 * Math.sin(now / 900);
    const haze = Math.round(Math.min(0.22, level / 100 * 0.22) * pulse * 255);
    Draw.rect(0, 0, width, height, haze * 0x1000000 + 0x5a3812);
    // Dark edges, deeper the drunker.
    const edge = Math.round(Math.min(width, height) * (0.06 + level / 100 * 0.1));
    const shade = Math.round(Math.min(0.5, level / 100 * 0.5) * 255) * 0x1000000;
    Draw.rect(0, 0, width, edge, shade);
    Draw.rect(0, height - edge, width, edge, shade);
    Draw.rect(0, edge, edge, height - 2 * edge, shade);
    Draw.rect(width - edge, edge, edge, height - 2 * edge, shade);
}

Events.on("render", () => {
    if (daze.blackout) return;
    const level = drunkLevel();
    const now = Date.now();
    if (level <= 0) {
        if (daze.swing !== 0 || daze.baseFov !== null) sober();
        return;
    }
    // A wasted player on foot lurches every few seconds.
    if (level >= LURCH_FROM && !LocalPlayer.getVehicle()) {
        if (!daze.nextLurch) daze.nextLurch = now + 3000 + Math.random() * 5000;
        if (now >= daze.nextLurch) {
            daze.lurchUntil = now + LURCH_MS;
            daze.nextLurch = now + 6000 + Math.random() * 6000;
        }
    } else daze.nextLurch = 0;
    const swing = now < daze.lurchUntil ? LURCH_SWING : level < SWING_FROM ? 0 : Math.min(SWING_MAX, (level - SWING_FROM + 10) * 0.9);
    setSwing(swing);
    if (level >= WOBBLE_FROM) {
        if (daze.baseFov === null) daze.baseFov = Camera.getFov();
        if (daze.baseFov !== null) {
            const amplitude = (level - WOBBLE_FROM + 5) / (100 - WOBBLE_FROM + 5) * WOBBLE_DEGREES;
            Camera.setFov(daze.baseFov + Math.sin(now / WOBBLE_PERIOD_MS * 2 * Math.PI) * amplitude);
        }
    } else restoreFov();
    if (level >= HAZE_FROM) drawHaze(level, now);
});

// The screen stays dark while the server moves the player (a move is a new
// life, whose spawn fades in) and until they come round.
Events.on("fr:drunk:blackout", ({seconds}) => {
    daze.blackout = true;
    sober();
    Fade.out(1.6);
    Sound.play("01b_srdce.wav", {volume: 0.8});
    if (daze.blackoutTimer !== null) clearTimeout(daze.blackoutTimer);
    daze.blackoutTimer = setTimeout(() => endBlackout(1.5), (Number(seconds) || 6) * 1000 + BLACKOUT_GRACE_MS);
});
Events.on("playerSpawn", (player) => {
    if (player.isLocal && daze.blackout) Fade.out(0);
});
Events.on("fr:drunk:wake", () => endBlackout(2.5));

Events.on("playerDeath", (player) => {
    if (player.isLocal) sober();
});
Events.on("missionUnload", () => {
    sober();
    daze.blackout = false;
    if (daze.blackoutTimer !== null) clearTimeout(daze.blackoutTimer);
    daze.blackoutTimer = null;
    daze.baseFov = null;
    daze.swing = 0;
});
Events.on("resourceStop", (name) => {
    if (name !== "freeride") return;
    sober();
    // Nobody is left to send the wake-up.
    endBlackout(0.5);
});
