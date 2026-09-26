// Free Ride door lab. Stock mission doors register themselves the first time a
// nearby player uses them. The server then owns their target, lock and native
// use state, and replicas restore the same pose for reconnecting players.
//
// Commands: /doors, /door status|open|openback|close|ajar|lock|unlock|enable|disable.
// All actions find a registered door by its actual mission hinge position; no
// assumed city coordinates or frame names are baked into this example.

const FREE_RIDE_MISSIONS = new Set(["freeitaly", "freeride", "freeridenoc", "freekrajina"]);
const COMMAND_REACH = 4.5;
const LIST_REACH = 18;

function inFreeRide() {
    return FREE_RIDE_MISSIONS.has(World.getMission());
}

function distanceTo(player, door) {
    const a = player.position;
    const b = door.position;
    return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

function nearbyDoors(player, radius) {
    return Door.getAll()
        .map((door) => ({door, distance: distanceTo(player, door)}))
        .filter(({distance}) => distance <= radius)
        .sort((a, b) => a.distance - b.distance);
}

function describe(door) {
    const pose = door.open ? `${Math.round(door.openFraction * 100)}% open` : "closed";
    const control = door.enabled ? (door.locked ? "locked" : "usable") : "use disabled";
    return `${door.frameName}: ${pose}, ${control}`;
}

Events.on("playerCommand", (player, command, args) => {
    if (command !== "doors" && command !== "door") return;
    if (!inFreeRide() || !player.spawned || !player.alive) {
        player.sendMessage("Door controls are available on foot in Free Ride.");
        return;
    }

    if (command === "doors") {
        const nearby = nearbyDoors(player, LIST_REACH);
        if (!nearby.length) {
            player.sendMessage("No synchronized door nearby. Use a stock door once, then try /doors.");
            return;
        }
        for (const {door, distance} of nearby.slice(0, 4)) {
            player.sendMessage(`${describe(door)} (${distance.toFixed(1)} m)`);
        }
        return;
    }

    const nearest = nearbyDoors(player, COMMAND_REACH)[0];
    if (!nearest) {
        player.sendMessage("No synchronized door within reach. Use a stock door once to register it.");
        return;
    }

    const {door} = nearest;
    const action = args[0]?.toLowerCase() ?? "status";
    let accepted = true;
    switch (action) {
    case "status": break;
    case "open": accepted = door.openDoor(false, true); break; // paired leaves swing apart
    case "openback": accepted = door.openDoor(true, false); break;
    case "close": accepted = door.closeDoor(); break;
    case "ajar": accepted = door.setOpenFraction(0.35, false, true); break;
    case "lock": door.locked = true; break;
    case "unlock": door.locked = false; break;
    case "enable": door.enabled = true; break;
    case "disable": door.enabled = false; break;
    default:
        player.sendMessage("Use /door status|open|openback|close|ajar|lock|unlock|enable|disable.");
        return;
    }
    player.sendMessage(accepted ? describe(door) : `Cannot ${action} ${door.frameName} right now.`);
});

Events.on("doorStateChange", (door, player) => {
    if (inFreeRide()) {
        console.log(`[Free Ride doors] ${player?.nickname ?? "Script"}: ${describe(door)}`);
    }
});
