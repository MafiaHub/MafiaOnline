// Runs in each client's sandboxed V8. The server owns the world; this script
// drives the fades, announcements and sounds that frame each moment.
const COUNTDOWN_BEEP = "beep50.wav";
const CHECKPOINT_SOUND = "00_kompas.wav";

let countdownTimers = [];

function clearCountdown() {
    for (const timer of countdownTimers) clearTimeout(timer);
    countdownTimers = [];
}

Events.on("missionReady", () => Fade.in(1.5));

Events.on("playerSpawn", (player) => {
    if (player.isLocal) Fade.in(1);
});

Events.on("playerDeath", (player) => {
    if (!player.isLocal) return;
    clearCountdown();
    Sound.play("01b_srdce.wav");
    Fade.out(1.2);
});

Events.on("missionUnload", clearCountdown);

// "Get ready", then the given number of seconds, then the green light; the
// server releases the cars at the same moment.
Events.on("fr:race:countdown", ({seconds}) => {
    clearCountdown();
    const count = Math.max(1, Math.min(5, seconds ?? 3));
    Hud.announce("GET READY", 1);
    for (let step = 1; step <= count; step++) {
        countdownTimers.push(setTimeout(() => {
            Hud.announce(String(count - step + 1), 1);
            Sound.play(COUNTDOWN_BEEP, {volume: 0.7});
        }, step * 1000));
    }
    countdownTimers.push(setTimeout(() => {
        Hud.announce("GO!", 1.5);
        Sound.play(COUNTDOWN_BEEP, {volume: 1});
        Sound.play("18bc_start.wav", {volume: 0.8});
    }, (count + 1) * 1000));
});

Events.on("fr:race:checkpoint", () => Sound.play(CHECKPOINT_SOUND, {volume: 0.8}));

Events.on("fr:announce", ({text, seconds, sound}) => {
    if (typeof text === "string") Hud.announce(text, seconds ?? 2);
    if (typeof sound === "string") Sound.play(sound, {volume: 0.8});
});

// /dev hud on a server started with FREERIDE_DEV=1.
Events.on("fr:dev:hud", ({action, value}) => {
    const number = Number(value);
    switch (action) {
    case "countdown": Hud.startCountdown(Number.isFinite(number) ? number : 30); break;
    case "watch": Hud.showWatch(12, 0); break;
    case "swing": Camera.setSwing(Number.isFinite(number) ? number : 30); break;
    case "fade":
        Fade.out(1);
        setTimeout(() => Fade.in(1), 2000);
        break;
    case "clear":
        Hud.hideWatch();
        Camera.setSwing(0);
        break;
    default:
        if (Sound.play(typeof value === "string" ? value : "00_dog.wav") === null) Hud.showMessage("Could not play that sound");
    }
});
