// The prisoner's view of jail.js: the game's own countdown watch while the
// sentence runs, and a notice when the cell door opens. The server fades the
// screen around the moves in and out of the jail (fr:fade), and the jail's
// walls, doors and warden come from the server as map objects and an NPC.
let counting = false;

Events.on("fr:jail", (state) => {
    if (state?.active) {
        if (Number.isInteger(state.seconds) && state.seconds > 0) {
            Hud.startCountdown(state.seconds);
            counting = true;
        }
        return;
    }
    counting = false;
    Hud.hideWatch();
    Hud.announce("CELL OPEN", 2);
});
// A run-out watch would stay up over the money counter while the warden
// comes to the door.
Events.on("countdownEnd", () => {
    if (!counting) return;
    counting = false;
    Hud.hideWatch();
});
// The server moves prisoners out when Free Ride stops; the watch goes too.
Events.on("resourceStop", (name) => { if (name === "freeride") Hud.hideWatch(); });
