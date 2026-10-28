// The prisoner's view of jail.js: a guard in the cell doorway, the game's own
// countdown watch, and a fade as the guard walks them out. The guard is a
// solid local human like the shop staff, so he also blocks the door.
const JAIL_RESOURCE = "freeride";
const GUARD_MODEL = "Pol01.i3d";

const cell = {guard: null};

function dismissGuard() {
    if (cell.guard !== null) Scene.destroy(cell.guard);
    cell.guard = null;
}

Events.on("fr:jail", (state) => {
    if (state?.active) {
        dismissGuard();
        if (state.guard && Scene.canUseHumanModel(GUARD_MODEL)) {
            cell.guard = Scene.createHuman(GUARD_MODEL, state.guard.position, state.guard.direction, true);
        }
        if (Number.isInteger(state.seconds) && state.seconds > 0) Hud.startCountdown(state.seconds);
        return;
    }
    Hud.hideWatch();
    Hud.announce("RELEASED", 2);
    Fade.out(1);
    setTimeout(() => {
        dismissGuard();
        Fade.in(1);
    }, 1600);
});

Events.on("missionUnload", () => { cell.guard = null; });
Events.on("resourceStop", (name) => { if (name === JAIL_RESOURCE) dismissGuard(); });
