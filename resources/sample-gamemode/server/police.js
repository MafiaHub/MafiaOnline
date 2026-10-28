// The retail police HUD, driven from the server: the simulation host runs the
// police for everyone and reports each player's crimes and wanted level; this
// resource decides what the player sees and what an arrest costs.

// Retail police lines (tables/textdb): "Police! ..." 2100 + the crime's message.
const CRIME_TEXT = {
    weaponSeen: 2100,
    firing: 2101,
    speeding: 2102,
    crashObject: 2103,
    crashCar: 2103,
    crashPedestrian: 2103,
    sidewalk: 2104,
    violence: 2105,
    redLight: 2106,
    carTheft: 2107,
    signDamage: 2108,
    assaultOnPolice: 2109,
    policeCarRammed: 2110,
};
// Retail: a fine costs 1000 points; an arrest ends the game, here a fade and a respawn.
const FINE_POINTS = 1000;
const ARREST_RESPAWN_MS = 4000;

function sendPolice(player, data) {
    player.emit("sample:police", JSON.stringify(data));
}

Events.on("playerCrime", (player, {crime}) => {
    const textId = CRIME_TEXT[crime];
    if (textId !== undefined) sendPolice(player, {action: "text", textId});
});

Events.on("playerWantedChange", (player, {level, previous}) => {
    sendPolice(player, {action: "wanted", level});
    if (level === 0 && previous > 0) player.sendMessage("The police lost interest in you.");
});

Events.on("playerSpawn", (player) => sendPolice(player, {action: "wanted", level: 0}));
Events.on("playerRespawn", (player) => sendPolice(player, {action: "wanted", level: 0}));

Events.on("playerArrest", (player, officer, {kind}) => {
    console.log(`[mafia1online-sample] ${player.nickname} ${kind === "fine" ? "fined" : "arrested"} by police NPC ${officer?.id ?? "?"}`);
    if (kind === "fine") {
        sendPolice(player, {action: "fine", points: FINE_POINTS});
        player.sendMessage(`Fined ${FINE_POINTS} points by the police.`);
        return;
    }
    const {missionGeneration, spawnGeneration} = player;
    Police.clear(player);
    sendPolice(player, {action: "arrest"});
    player.sendMessage("You were arrested. Released after questioning.");
    setTimeout(() => {
        if (!player.alive || player.missionGeneration !== missionGeneration || player.spawnGeneration !== spawnGeneration) return;
        player.respawn(player.getSuggestedSpawn() ?? player.position);
    }, ARREST_RESPAWN_MS);
});

Events.on("playerCommand", (player, command, args) => {
    if (command === "wanted") {
        const record = Police.getRecord(player);
        if (!record) {
            player.sendMessage("No police record while you are not spawned.");
            return;
        }
        if (args.length === 1) {
            const level = Number(args[0]);
            if (!Number.isInteger(level) || level < 0 || level > 3 || !Police.setWanted(player, level)) {
                player.sendMessage("Usage: /wanted [0-3] (needs the simulation host)");
            }
            return;
        }
        player.sendMessage(`Wanted level ${record.level}, ${record.officers} officers, crimes: ${record.crimes.join(", ") || "none"}`);
    }
});
