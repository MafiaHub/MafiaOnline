// The prisoner's view of jail.js: the game's own countdown watch while the
// sentence runs, and a notice when the cell door opens. The server fades the
// screen around the moves in and out of the jail (fr:fade), and the jail's
// walls, doors and warden come from the server as map objects and an NPC.
Events.on("fr:jail", (state) => {
    if (state?.active) {
        if (Number.isInteger(state.seconds) && state.seconds > 0) Hud.startCountdown(state.seconds);
        return;
    }
    Hud.hideWatch();
    Hud.announce("CELL OPEN", 2);
});
