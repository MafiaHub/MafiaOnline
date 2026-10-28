// City upkeep. Phone booths, hydrants, trash bins and barrels stay knocked
// down and boxes stay where they were pushed, for every player alike, until
// the server repairs them. Traffic signs are the exception: they come from
// the city cache, fall only for the player who hit them and stay down until
// the mission changes. Here the city crew comes round a few minutes after
// the first damage; /props shows the damage and /repairprops calls the crew
// at once. The sample has no admins, so anyone may call the crew, at most
// once a minute.

const REPAIR_AFTER_MS = 5 * 60_000;
const CHECK_MS = 15_000;
const CALL_COOLDOWN_MS = 60_000;

let timer = null;
// When the damage since the last repair started, or null while all is whole.
let damagedSince = null;
// When a player last called the crew.
let lastCall = 0;

function describe({broken, moved}) {
    return `${broken} broken prop${broken === 1 ? "" : "s"}, ${moved} moved`;
}

function repair(reason) {
    const restored = World.repairProps();
    damagedSince = null;
    if (restored > 0) {
        for (const player of World.getPlayers()) player.sendMessage(`The city crew ${reason}: ${restored} prop${restored === 1 ? "" : "s"} back in place.`);
    }
    return restored;
}

function checkDamage() {
    const damage = World.getPropDamage();
    if (damage.broken + damage.moved === 0) {
        damagedSince = null;
        return;
    }
    damagedSince ??= Date.now();
    if (Date.now() - damagedSince >= REPAIR_AFTER_MS) repair("came round");
}

Events.on("resourceStart", (name) => {
    if (name !== "mafia1online-sample") return;
    damagedSince = null;
    timer = setInterval(checkDamage, CHECK_MS);
});

Events.on("resourceStop", (name) => {
    if (name !== "mafia1online-sample" || timer === null) return;
    clearInterval(timer);
    timer = null;
});

// A new mission starts with every prop whole.
Events.on("missionChange", () => {
    damagedSince = null;
});

Events.on("playerCommand", (player, command) => {
    const action = command.toLowerCase();
    if (action === "props") {
        const damage = World.getPropDamage();
        const due = damagedSince === null ? "" : `; the crew comes in ${Math.max(0, Math.ceil((damagedSince + REPAIR_AFTER_MS - Date.now()) / 60_000))} min`;
        player.sendMessage(`City props: ${describe(damage)}${due}. Traffic signs are not tracked.`);
        return;
    }
    if (action === "repairprops") {
        const wait = lastCall + CALL_COOLDOWN_MS - Date.now();
        if (wait > 0) {
            player.sendMessage(`The city crew was just called; try again in ${Math.ceil(wait / 1000)} s.`);
            return;
        }
        lastCall = Date.now();
        if (repair(`was called by ${player.nickname}`) === 0) player.sendMessage("Nothing to repair: every prop is in place.");
    }
});
