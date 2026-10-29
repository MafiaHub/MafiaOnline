// The rum distillery's two moments (server/distillery.js): the first look
// at the still when a player finds the camp, a short locked shot between
// fades, and the white flash of a still blowing up nearby.
const REVEAL_HOLD_MS = 4200;

let reveal = null;

// Back to the player's camera, fading the screen in over this many seconds
// (0 at once: the shot was cut short, maybe while the screen was black).
function endReveal(seconds = 0.6) {
    if (!reveal) return;
    for (const timer of reveal.timers) clearTimeout(timer);
    reveal = null;
    Camera.unlock();
    Fade.in(seconds);
}

Events.on("fr:rum:reveal", (data) => {
    const position = data?.position;
    const target = data?.target;
    if (!position || !target) return;
    endReveal(0);
    const direction = {x: target.x - position.x, y: target.y - position.y, z: target.z - position.z};
    reveal = {timers: []};
    Fade.out(0.5);
    reveal.timers.push(setTimeout(() => {
        if (!reveal) return;
        if (Camera.lock(position, direction) === false) {
            endReveal();
            return;
        }
        Fade.in(0.8);
        if (typeof data.title === "string") Hud.announce(data.title, 3);
    }, 600));
    reveal.timers.push(setTimeout(() => {
        if (!reveal) return;
        Fade.out(0.4);
        reveal.timers.push(setTimeout(() => endReveal(), 450));
    }, 600 + REVEAL_HOLD_MS));
});

Events.on("fr:rum:flash", (data) => {
    const strength = typeof data?.strength === "number" ? Math.max(0, Math.min(1, data.strength)) : 1;
    Fade.out(0.05, 0xffffff);
    setTimeout(() => Fade.in(0.4 + strength, 0xffffff), 80);
});

Events.on("playerDeath", (player) => { if (player.isLocal) endReveal(0); });
Events.on("missionUnload", () => endReveal(0));
Events.on("resourceStop", (name) => { if (name === "freeride") endReveal(0); });
