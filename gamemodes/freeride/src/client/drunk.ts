// What drink does to this player's eyes (server/drunk.ts keeps the level
// as the "fr.drunk" state key, 0 to 100 in steps of 5). The world sways with
// the retail boat swing (CAMERA_SETSWING), the view breathes in and out
// (the camera's field of view), a warm haze and dark edges close in, and a
// wasted player's view lurches now and then. A blackout holds the screen
// dark until the server has carried them out.
import { withAlpha } from './lib/color';
import { clamp } from './lib/geometry';
import { livePlayer, onLocalDeath, onLocalSpawn } from './lib/player';
import { events, onServer, onStop } from './lib/runtime';
import { Timers } from './lib/timers';

const DRUNK_KEY = 'fr.drunk';
const SWING_FROM = 20;
const SWING_MAX = 70;
const WOBBLE_FROM = 25;
const WOBBLE_DEGREES = 6;
const WOBBLE_PERIOD_MS = 4500;
const HAZE_FROM = 10;
const HAZE_COLOR = 0x5a3812;
const LURCH_FROM = 70;
const LURCH_SWING = 100;
const LURCH_MS = 1200;
const BLACKOUT_GRACE_MS = 3000;

let swing = 0;
// The player's own field of view while drink makes it breathe.
let baseFov: number | null = null;
let lurchUntil = 0;
let nextLurch = 0;
let blackout = false;
const wake = new Timers();

function drunkLevel(player: Player): number {
    const value = Number((player.state.get(DRUNK_KEY) as unknown) ?? 0);

    return Number.isFinite(value) ? clamp(value, 0, 100) : 0;
}

function setSwing(value: number): void {
    const rounded = Math.round(value);

    if (rounded !== swing && Camera.setSwing(rounded)) {
        swing = rounded;
    }
}

function restoreFov(): void {
    if (baseFov === null) {
        return;
    }

    Camera.setFov(baseFov);
    baseFov = null;
}

function sober(): void {
    setSwing(0);
    restoreFov();
    lurchUntil = 0;
    nextLurch = 0;
}

function endBlackout(seconds: number): void {
    if (!blackout) {
        return;
    }

    blackout = false;
    wake.clear();
    Fade.in(seconds);
}

function drawHaze(level: number, now: number): void {
    const { width, height } = Web.getScreenSize();
    const pulse = 0.85 + 0.15 * Math.sin(now / 900);

    Draw.rect(
        0,
        0,
        width,
        height,
        withAlpha(HAZE_COLOR, Math.min(0.22, (level / 100) * 0.22) * pulse),
    );

    // Dark edges, deeper the drunker.
    const edge = Math.round(Math.min(width, height) * (0.06 + (level / 100) * 0.1));
    const shade = withAlpha(0x000000, Math.min(0.5, (level / 100) * 0.5));

    Draw.rect(0, 0, width, edge, shade);
    Draw.rect(0, height - edge, width, edge, shade);
    Draw.rect(0, edge, edge, height - 2 * edge, shade);
    Draw.rect(width - edge, edge, edge, height - 2 * edge, shade);
}

events.on('render', () => {
    if (blackout) {
        return;
    }

    const player = livePlayer();
    const level = player ? drunkLevel(player) : 0;
    const now = Date.now();

    if (!player || level <= 0) {
        if (swing !== 0 || baseFov !== null) {
            sober();
        }

        return;
    }

    // A wasted player on foot lurches every few seconds.
    if (level >= LURCH_FROM && !player.getVehicle()) {
        if (!nextLurch) {
            nextLurch = now + 3000 + Math.random() * 5000;
        }

        if (now >= nextLurch) {
            lurchUntil = now + LURCH_MS;
            nextLurch = now + 6000 + Math.random() * 6000;
        }
    } else {
        nextLurch = 0;
    }

    if (now < lurchUntil) {
        setSwing(LURCH_SWING);
    } else {
        setSwing(level < SWING_FROM ? 0 : Math.min(SWING_MAX, (level - SWING_FROM + 10) * 0.9));
    }

    if (level >= WOBBLE_FROM) {
        baseFov ??= Camera.getFov();

        if (baseFov !== null) {
            const amplitude =
                ((level - WOBBLE_FROM + 5) / (100 - WOBBLE_FROM + 5)) * WOBBLE_DEGREES;

            Camera.setFov(baseFov + Math.sin((now / WOBBLE_PERIOD_MS) * 2 * Math.PI) * amplitude);
        }
    } else {
        restoreFov();
    }

    if (level >= HAZE_FROM) {
        drawHaze(level, now);
    }
});

// The screen stays dark while the server moves the player (a move is a new
// life, whose spawn fades in) and until they come round.
onServer<{ seconds?: number }>('fr:drunk:blackout', ({ seconds }) => {
    blackout = true;
    sober();
    Fade.out(1.6);
    Sound.play('01b_srdce.wav', { volume: 0.8 });
    wake.clear();
    wake.after((Number(seconds) || 6) * 1000 + BLACKOUT_GRACE_MS, () => endBlackout(1.5));
});

onLocalSpawn(() => {
    if (blackout) {
        Fade.out(0);
    }
});

events.on('fr:drunk:wake', () => endBlackout(2.5));

onLocalDeath(sober);

events.on('missionUnload', () => {
    sober();
    blackout = false;
    wake.clear();
    baseFov = null;
    swing = 0;
});

onStop(() => {
    sober();
    // Nobody is left to send the wake-up.
    endBlackout(0.5);
});
