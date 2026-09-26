// The server owns spawns. After a mission loads, the client reports a native
// scene spawn hint even when the mission has no stock player actor.
const spawned = new Map();
const lastVehicles = new Map();
const ownedVehicles = new Map();
const lastCarChoiceAt = new Map();
const retiringVehicles = new Map();
let generation = 0;
let spawnTimer = null;

const COLT = 6;
const BAT = 4;
const RESPAWN_DELAY_MS = 5000;
// IDs and default-color models from patched retail tables/carindex.def in
// a8.dta. The client reads that table for the display names. A page event
// supplies only an ID; the server chooses the model filename.
const CAR_MODELS_BY_ID = [
    null,
    "fordttud00.i3d", "fordtto00.i3d", "fordtru00.i3d", "fordtpi00.i3d", "fordtfor00.i3d", "fordtco00.i3d",
    "foratu00.i3d", "foraro00.i3d", "forapic00.i3d", "forafo00.i3d", "forade00.i3d", "foracou00.i3d", "foraca00.i3d",
    "chev00.i3d", "forvco00.i3d", "forvfor00.i3d", "forvro00.i3d", "forvto00.i3d", "forvtud00.i3d",
    "chemafor00.i3d", "chematud00.i3d", "blackha00.i3d", "taxi00.i3d", "pontfor00.i3d", "ponttud00.i3d",
    "hudcou00.i3d", "hudfor00.i3d", "hudtu00.i3d", "cordca00.i3d", "cordph00.i3d", "cordse00.i3d",
    "buicou00.i3d", "buikfor00.i3d", "speedster00.i3d", "merced500k00.i3d", "cad_ford00.i3d", "cad_phaeton00.i3d",
    "cad_road00.i3d", "arrow00.i3d", "hartmann00.i3d", "phantom00.i3d", "deusejco00.i3d", "bugatti00.i3d",
    "miller00.i3d", "duesenberg00.i3d", "alfa8c00.i3d", "ambulance00.i3d", "fire00.i3d", "hearsea00.i3d",
    "hearseca00.i3d", "airflfor00.i3d", "airfltud00.i3d", "polcad00.i3d", "poli00.i3d", "polimfor00.i3d",
    "polimtud00.i3d", "trucka00.i3d", "truckb00.i3d", "alfa00.i3d", "thunderbird00.i3d", "TruckBx00.i3d",
    "fordhot00.i3d", "buigang00.i3d", "black00.i3d", "lowgas00.i3d", "blackdragon00.i3d", "cord_sedanh00.i3d",
    "flamer00.i3d", "fordapick00.i3d", "fordapicktaxi00.i3d", "fordth00.i3d", "fthot00.i3d", "hotrodp200.i3d",
    "hotrodp300.i3d", "hotrodp400.i3d", "hotrodp500.i3d", "chevroletm6h00.i3d", "tbirdold00.i3d",
    "fordadelh00.i3d", "hotrodp600.i3d", "phantomtaxi00.i3d",
];

function giveStarterWeapons(player) {
    return player.setInventory([
        {weaponId: BAT},
        {weaponId: COLT, loaded: 6, reserve: 12},
    ], COLT);
}

function giveTestWeapons(player, bigWeapon = 4) {
    const big = bigWeapon === 4 ? {weaponId: 4} : bigWeapon === 11 ? {weaponId: 11, loaded: 8, reserve: 32} : {weaponId: 10, loaded: 50, reserve: 150};
    return player.setInventory([
        {weaponId: 3},                 // knife: melee replay
        {weaponId: COLT, loaded: 6, reserve: 36},
        big,
        {weaponId: 15, loaded: 3},     // grenades
    ], bigWeapon);
}

function spawnReadyPlayers() {
    const next = World.getMissionGeneration();
    if (next !== generation) {
        generation = next;
        spawned.clear();
        lastVehicles.clear();
        ownedVehicles.clear();
        lastCarChoiceAt.clear();
    }
    if (!World.isReady() || ["freeride", "freeridenoc"].includes(World.getMission())) return;

    for (const player of World.getPlayers()) {
        if (player.spawned || spawned.get(player.id) === generation) continue;
        const position = player.getSuggestedSpawn();
        if (!position || !player.spawn(position)) continue;
        spawned.set(player.id, generation);
        console.log(`[mafia1online-sample] Spawned ${player.nickname} in ${World.getMission()}`);
    }
}

function clearRetiringVehicles() {
    for (const timer of retiringVehicles.values()) clearTimeout(timer);
    retiringVehicles.clear();
}

function retireVehicleAfter(id, missionGeneration, delayMs = 4000) {
    if (retiringVehicles.has(id)) return;
    const check = () => {
        retiringVehicles.delete(id);
        if (World.getMissionGeneration() !== missionGeneration) return;
        const vehicle = vehicleById(id);
        if (!vehicle) return;
        // A passenger can still be in the old car. Wait for the last occupant
        // instead of deleting a live multiplayer vehicle under them.
        if (vehicle.getOccupants().some((occupant) => occupant !== null)) {
            retiringVehicles.set(id, setTimeout(check, 1000));
            return;
        }
        if (vehicle.destroy()) console.log(`[mafia1online-sample] Retired previous car ${id}`);
    };
    retiringVehicles.set(id, setTimeout(check, delayMs));
}

Events.on("resourceStart", (name) => {
    if (name === "mafia1online-sample") spawnTimer = setInterval(spawnReadyPlayers, 200);
});

Events.on("resourceStop", (name) => {
    if (name === "mafia1online-sample" && spawnTimer) {
        clearInterval(spawnTimer);
        spawnTimer = null;
    }
    if (name === "mafia1online-sample") {
        clearRetiringVehicles();
        spawned.clear();
        lastVehicles.clear();
        ownedVehicles.clear();
        lastCarChoiceAt.clear();
    }
});

Events.on("playerDisconnect", (player) => {
    spawned.delete(player.id);
    lastVehicles.delete(player.id);
    lastCarChoiceAt.delete(player.id);
    for (const id of ownedVehicles.get(player.id) ?? []) {
        const vehicle = vehicleById(id);
        if (vehicle && vehicle.getOccupants().every((occupant) => occupant === null)) vehicle.destroy();
    }
    ownedVehicles.delete(player.id);
});
Events.on("missionChange", () => {
    clearRetiringVehicles();
    lastVehicles.clear();
    ownedVehicles.clear();
    lastCarChoiceAt.clear();
});
Events.on("vehicleDestroy", (vehicle) => {
    if (retiringVehicles.has(vehicle.id)) {
        clearTimeout(retiringVehicles.get(vehicle.id));
        retiringVehicles.delete(vehicle.id);
    }
    for (const [playerId, id] of lastVehicles) if (id === vehicle.id) lastVehicles.delete(playerId);
    for (const [playerId, ids] of ownedVehicles) {
        ids.delete(vehicle.id);
        if (ids.size === 0) ownedVehicles.delete(playerId);
    }
});
Events.on("vehicleTerminal", (vehicle, state) => {
    if (state !== 2 && state !== 3) return;
    const id = vehicle.id;
    const generation = World.getMissionGeneration();
    // The engine supplies the splash and fall. This gamemode removes the
    // terminal car after the death fade and normal respawn have had time to run.
    setTimeout(() => {
        if (World.getMissionGeneration() !== generation) return;
        const current = vehicleById(id);
        if (current && current.terminalState === state) current.destroy();
    }, 6000);
});
Events.on("missionReady", spawnReadyPlayers);
Events.on("playerMissionReady", spawnReadyPlayers);
Events.on("playerSpawn", (player) => {
    if (!giveStarterWeapons(player)) console.error(`[mafia1online-sample] Could not equip starter weapons for ${player.nickname}`);
    player.sendMessage("Welcome to Lost Heaven. /help lists the gamemode activities.");
});
Events.on("playerRespawn", (player) => {
    if (!giveStarterWeapons(player)) console.error(`[mafia1online-sample] Could not restore starter weapons for ${player.nickname}`);
});

Events.on("vehiclePlayerEntered", (vehicle, player, {seat, stolen}) => {
    lastVehicles.set(player.id, vehicle.id);
    console.log(`[mafia1online-sample] ${player.nickname} ${stolen ? "stole" : "entered"} vehicle ${vehicle.id}, seat ${seat}`);
});
Events.on("vehiclePlayerExited", (vehicle, player, {seat}) => {
    lastVehicles.set(player.id, vehicle.id);
    console.log(`[mafia1online-sample] ${player.nickname} exited vehicle ${vehicle.id}, seat ${seat}`);
});

// Death never respawns on its own; the gamemode decides.
Events.on("playerDeath", (victim, killer, {missionGeneration, spawnGeneration, weaponId, cause}) => {
    const weapon = weaponId === null ? "unknown weapon" : weaponId === 0 ? "fists" : Weapon.get(weaponId)?.name ?? `weapon ${weaponId}`;
    console.log(`[mafia1online-sample] ${victim.nickname} died: ${cause}; killer ${killer?.nickname ?? "unknown"}; weapon ${weapon}`);
    if (["freeride", "freeridenoc"].includes(World.getMission())) return; // selector.js owns these respawns.
    setTimeout(() => {
        if (victim.alive || victim.missionGeneration !== missionGeneration || victim.spawnGeneration !== spawnGeneration || !World.isReady()) return;
        victim.respawn(victim.getSuggestedSpawn() ?? victim.position);
    }, RESPAWN_DELAY_MS);
});

function reportVehicle(player, vehicle, action) {
    if (action === "engine") {
        if (player.getVehicle()?.id !== vehicle.id || player.getSeat() !== 0) {
            player.sendMessage("Sit in the driver's seat to use /engine.");
            return;
        }
        if (!vehicle.setEngine(!vehicle.engineOn)) player.sendMessage("Engine state could not be changed");
        return;
    }
    if (action === "cardamage") {
        const damage = vehicle.getDamageState();
        if (!damage) {
            player.sendMessage(`Vehicle ${vehicle.id}: native damage not yet reported`);
            return;
        }
        const broken = (parts, bit) => parts.filter((part) => (part.flags & bit) !== 0).length;
        player.sendMessage(`Vehicle ${vehicle.id}: damage revision ${damage.revision}, body ${damage.bodyDamage.toFixed(1)}, glass/parts ${broken(damage.zones, 1)}, tires ${broken(damage.wheels, 0x400)}`);
        return;
    }
    const {x, y, z} = vehicle.velocity;
    player.sendMessage(`Vehicle ${vehicle.id}: ${Math.round(Math.hypot(x, y, z) * 3.6)} km/h, gear ${vehicle.gear}, fuel ${vehicle.fuel.toFixed(1)}/${vehicle.fuelTankCapacity.toFixed(1)}, engine ${vehicle.engineOn ? "on" : "off"}`);
}

function vehicleById(id) {
    return id === undefined ? null : World.getVehicles().find((vehicle) => vehicle.id === id) ?? null;
}

function currentOrLastVehicle(player) {
    return player.getVehicle() ?? vehicleById(lastVehicles.get(player.id));
}

function spawnPersonalCar(player, model) {
    if (!World.isReady() || !player.spawned || !player.alive) return {ok: false, message: "Enter Free Ride before choosing a car."};
    if (!/^[A-Za-z0-9_.-]+\.i3d$/.test(model)) return {ok: false, message: "That car model is unavailable."};
    const oldVehicle = player.getVehicle();
    const {x, y, z} = oldVehicle?.position ?? player.position;
    const vehicle = Vehicle.spawn(model, {x: x + 4, y, z}, oldVehicle?.rotation ?? 0, player);
    if (!vehicle) return {ok: false, message: "Car could not be spawned."};
    if (!player.putInVehicle(vehicle, 0)) {
        vehicle.destroy();
        return {ok: false, message: "The new driver's seat is unavailable."};
    }
    if (!vehicle.setRadarMarker(true, 0xe8c57d)) console.warn(`[mafia1online-sample] Could not mark car ${vehicle.id} on the radar`);
    for (const id of ownedVehicles.get(player.id) ?? []) retireVehicleAfter(id, World.getMissionGeneration());
    if (!ownedVehicles.has(player.id)) ownedVehicles.set(player.id, new Set());
    ownedVehicles.get(player.id).add(vehicle.id);
    lastVehicles.set(player.id, vehicle.id);
    console.log(`[mafia1online-sample] ${player.nickname} spawned and entered ${model} (${vehicle.id})`);
    return {ok: true, vehicleId: vehicle.id, message: "You're in your new car. Use /tour to drive the city circuit."};
}

// /car opens the stock catalog; /car <filename> remains useful for direct
// testing of a model that does not appear in the retail catalog.
Events.on("playerCommand", (player, command, args) => {
    const action = command.toLowerCase();
    if (action === "help") {
        player.sendMessage("Free Ride follows the server clock; weather fronts arrive gradually. Music plays in cars.");
        player.sendMessage("Visit Pete, the pump, the dispensary or Salieri's back window. B buys; N selects an offer.");
        player.sendMessage("/doors, /door status|open|openback|close|ajar|lock|unlock show synchronized doors.");
        player.sendMessage("/tour [start|status|cancel] runs a timed city circuit for cash. Drive to each compass marker and stop.");
        player.sendMessage("/car opens the car catalog; /repair, /opacity 0-1, /engine, /carstate, /cardamage");
        player.sendMessage("/weapons bat|shotgun|thompson, /fists, /drop [id], /model [file], /follow [name|off]");
        player.sendMessage("/hud [sound|countdown|watch|score|compass|clear|fade|swing], /suicide");
        return;
    }
    if (action === "follow") {
        const name = args.join(" ").trim();
        if (!name) {
            player.sendMessage("Use /follow <nickname> or /follow off");
            return;
        }
        if (name.toLowerCase() === "off") {
            player.sendMessage(player.setCameraTarget(null) ? "Following your own player" : "Could not restore your camera");
            return;
        }
        const target = World.getPlayers().find(other => other.nickname.toLowerCase() === name.toLowerCase());
        if (!target) {
            player.sendMessage(`Player '${name}' is not connected`);
            return;
        }
        player.sendMessage(player.setCameraTarget(target) ? `Following ${target.nickname}` : `Could not follow ${target.nickname}`);
        return;
    }
    if (action === "suicide") {
        player.sendMessage(player.setHealth(0) ? "You will respawn in 5 seconds" : "Spawn first to use /suicide");
        return;
    }
    if (action === "model") {
        if (args.length === 0) {
            player.sendMessage(`Current model: ${player.model}. Use /model <stock-human.i3d> or /model default`);
            return;
        }
        const model = args[0].toLowerCase() === "default" ? "Tommy.i3d" : args[0];
        player.sendMessage(player.setModel(model) ? `Player model set to ${model}` : "Invalid model filename; use a stock .i3d filename up to 64 characters");
        return;
    }
    if (action === "weapons" && player.spawned && player.alive) {
        const variants = {bat: 4, shotgun: 11, thompson: 10};
        const variant = (args[0] ?? "bat").toLowerCase();
        if (!(variant in variants)) {
            player.sendMessage("Use /weapons [bat|shotgun|thompson]");
            return;
        }
        player.sendMessage(giveTestWeapons(player, variants[variant]) ? `Test weapons equipped: knife, Colt, ${variant}, grenades` : "Could not equip test weapons");
        return;
    }
    if (action === "fists" && player.spawned && player.alive) {
        player.sendMessage(player.setInventory([]) ? "Unarmed: fists selected; use /weapons to restore the test kit" : "Could not select fists");
        return;
    }
    if (action === "drop" && player.spawned && player.alive) {
        const requested = args[0] === undefined ? undefined : Number(args[0]);
        if (requested !== undefined && (!Number.isInteger(requested) || requested < 2 || requested > 15 || !Weapon.get(requested))) {
            player.sendMessage("Use /drop [weaponId] with a stock weapon ID from 2 to 15");
            return;
        }
        const pickup = requested === undefined ? player.dropWeapon() : player.dropWeapon(requested);
        player.sendMessage(pickup ? `Dropped weapon ${pickup.weaponId} as pickup ${pickup.id}` : "Weapon not held, or no room for another pickup");
        return;
    }
    if ((action === "repair" || action === "opacity") && player.spawned && player.alive) {
        const vehicle = currentOrLastVehicle(player);
        if (!vehicle || !vehicle.model) {
            player.sendMessage("Enter a car or spawn one with /car first.");
            return;
        }
        const at = player.getVehicle()?.id === vehicle.id ? vehicle.position : player.position;
        if (Math.hypot(at.x - vehicle.position.x, at.y - vehicle.position.y, at.z - vehicle.position.z) > 8) {
            player.sendMessage("Move within 8 metres of that vehicle first.");
            return;
        }
        if (action === "repair") {
            player.sendMessage(vehicle.repair() ? `Vehicle ${vehicle.id} repaired.` : "The vehicle could not be repaired.");
            return;
        }
        const opacity = Number(args[0]);
        if (args[0] === undefined || !Number.isFinite(opacity) || opacity < 0 || opacity > 1) {
            player.sendMessage("Use /opacity <0-1>; 1 is fully visible.");
            return;
        }
        player.sendMessage(vehicle.setOpacity(opacity) ? `Vehicle opacity set to ${opacity}.` : "Opacity could not be changed.");
        return;
    }
    if (!["car", "carstate", "cardamage", "engine"].includes(action) || !player.spawned || !player.alive || !World.isReady()) return;
    if (action !== "car") {
        const vehicle = currentOrLastVehicle(player);
        if (!vehicle || !vehicle.model) {
            player.sendMessage("Spawn a car with /car first");
            return;
        }
        reportVehicle(player, vehicle, action);
        return;
    }
    if (args.length === 0) {
        player.emit("sample:car:open", JSON.stringify({generation: World.getMissionGeneration()}));
        return;
    }
    const result = spawnPersonalCar(player, args[0]);
    player.sendMessage(result.message);
});

Events.onClient("sample:car:choose", (player, request) => {
    if (!request || request.generation !== World.getMissionGeneration() || !Number.isInteger(request.id)) return;
    const model = CAR_MODELS_BY_ID[request.id];
    if (!model) return;
    const now = Date.now();
    if (now - (lastCarChoiceAt.get(player.id) ?? 0) < 750) return;
    lastCarChoiceAt.set(player.id, now);
    const result = spawnPersonalCar(player, model);
    player.emit("sample:car:result", JSON.stringify({id: request.id, ...result}));
    if (result.ok) player.sendMessage(result.message);
});

// /hud [sound|countdown|watch|score|compass|clear|fade|swing] drives the
// client script in client/main.js; sound is the default audio check.
Events.on("playerCommand", (player, command, args) => {
    if (command.toLowerCase() !== "hud") return;
    const action = args[0] ?? "sound";
    const data = {action};
    if (action === "countdown") data.seconds = Number(args[1] ?? 30);
    if (action === "score") data.points = Number(args[1] ?? 100);
    if (action === "swing") data.intensity = Number(args[1] ?? 30);
    if (action === "sound" && args[1]) data.wave = args[1];
    if (action === "compass") {
        const others = World.getPlayers().filter((other) => other.id !== player.id && other.spawned);
        if (others.length > 0) data.playerId = others[0].id;
        else data.position = {x: player.position.x + 50, y: player.position.y, z: player.position.z};
    }
    player.emit("sample:hud", JSON.stringify(data));
});

Events.onClient("sample:spawned", (player, data) => {
    console.log(`[mafia1online-sample] ${player.nickname}'s client spawned in ${data?.mission}`);
});

Events.onClient("sample:countdownEnd", (player) => {
    player.sendMessage("Your countdown ran out");
});
