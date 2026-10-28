// Free Ride, the default Mafia1Online gamemode. This entry owns the player
// lifecycle, personal cars and the menu plumbing; each activity lives in its
// own module next to it. Shared rules are in lib/.
const util = require("./lib/util.js");
const commands = require("./lib/commands.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const nametag = require("./lib/nametag.js");
const devmode = require("./lib/devmode.js");
const {toast, toastAll, feed, name, text, COLORS} = require("./lib/notify.js");

const {RESOURCE, onClient, distance, playable, vehicleById, formatMoney, headingOf, speedOf} = util;
const CAR_CHOICE_COOLDOWN_MS = 750;
const REPAIR_COOLDOWN_MS = 45_000;
const FLIP_COOLDOWN_MS = 10_000;
const CAR_REACH = 8;
const TIP_EVERY_MS = 7 * 60_000;
const MENU_COMMAND_INTERVAL_MS = 300;
const MENU_REQUEST_INTERVAL_MS = 800;
const LEADERBOARD_CACHE_MS = 5000;

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

const CAR_MODELS = new Set(CAR_MODELS_BY_ID.filter(Boolean).map((model) => model.toLowerCase()));

const spawnedInGeneration = new Map();
const welcomed = new Set();
const lastVehicles = new Map();
const personalCars = new Map();
const retiringVehicles = new Map();
const cooldowns = new Map();
let generation = 0;
let spawnTimer = null;
let tipTimer = null;
let tipIndex = 0;
let leaderboardCache = null;

const TIPS = [
    "F2 opens the city menu: jobs, races, crews, events and everyone in town.",
    "Lucas Bertone pays for particular traffic cars. /export shows his list.",
    "Go on police duty with /duty and take down wanted players for pay.",
    "Thirty cash stashes hide at the roadside. They show only up close.",
    "Crews can take over street corners with turf wars. /turf shows who holds what.",
    "Speed traps sit on the longest roads. /speedtraps shows the records.",
    "Feeling lucky? /dice <player> <bet>. Feeling bold? /rob a shop counter with a gun drawn.",
    "Open a street race with /race and race anyone who joins. Routes follow the real city roads.",
    "Three kills in a row put a price on your head. Anyone who drops you collects it.",
    "Challenge someone with /duel <name>. Duels run in a private copy of the city.",
    "Start a crew with /crew create <TAG>. Crewmates share a colour and see each other across town.",
    "Supply drops and hot cars appear around the city. Watch the compass.",
    "Salieri's back window has courier work, Yellow Pete sells guns, the pump sells fuel.",
    "Use /goto <name> to ask for a ride to a friend; they decide with /accept.",
];

function cooldown(player, key, ms) {
    const id = `${player.id}:${key}`;
    const wait = (cooldowns.get(id) ?? 0) + ms - Date.now();
    if (wait > 0) return Math.ceil(wait / 1000);
    cooldowns.set(id, Date.now());
    return 0;
}

// Stock missions other than Free Ride have no selector; their native spawn
// hint places everyone as soon as the whole server has loaded.
function spawnReadyPlayers() {
    const next = World.getMissionGeneration();
    if (next !== generation) {
        generation = next;
        spawnedInGeneration.clear();
    }
    if (!World.isReady() || util.inFreeRide()) return;
    for (const player of World.getPlayers()) {
        if (player.spawned || spawnedInGeneration.get(player.id) === generation) continue;
        const position = player.getSuggestedSpawn();
        if (!position || !player.spawn(position)) continue;
        spawnedInGeneration.set(player.id, generation);
    }
}

function clearRetiring() {
    for (const timer of retiringVehicles.values()) clearTimeout(timer);
    retiringVehicles.clear();
}

function retireAfter(id, missionGeneration, delayMs = 4000) {
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
        vehicle.destroy();
    };
    retiringVehicles.set(id, setTimeout(check, delayMs));
}

function carColor(player) {
    const crew = player.state.get("fr.crew");
    return crew?.color ? parseInt(crew.color.slice(1), 16) : 0xe8c57d;
}

function spawnPersonalCar(player, model) {
    if (!playable(player)) return {ok: false, message: "Enter the city before choosing a car."};
    if (!CAR_MODELS.has(model.toLowerCase())) return {ok: false, message: "That car model is unavailable."};
    if (["race", "duel"].includes(life.activity(player)?.kind)) return {ok: false, message: life.busyMessage(player)};
    const oldVehicle = player.getVehicle();
    const {x, y, z} = oldVehicle?.position ?? player.position;
    const heading = oldVehicle ? headingOf(oldVehicle.rotation) : headingOf(player.rotation);
    const vehicle = Vehicle.spawn(model, {x: x + Math.cos(heading) * 3.5, y: y + 0.3, z: z - Math.sin(heading) * 3.5}, heading, player);
    if (!vehicle) return {ok: false, message: "The motor pool is busy. Try again."};
    if (!player.putInVehicle(vehicle, 0)) {
        vehicle.destroy();
        return {ok: false, message: "The new driver's seat is unavailable."};
    }
    vehicle.setRadarMarker(true, carColor(player));
    vehicle.state.set("fr.owner", player.nickname);
    for (const id of personalCars.get(player.id) ?? []) retireAfter(id, World.getMissionGeneration());
    if (!personalCars.has(player.id)) personalCars.set(player.id, new Set());
    personalCars.get(player.id).add(vehicle.id);
    lastVehicles.set(player.id, vehicle.id);
    return {ok: true, vehicleId: vehicle.id, message: "Your car is ready. Open a race with /race."};
}

function currentOrLastVehicle(player) {
    return player.getVehicle() ?? vehicleById(lastVehicles.get(player.id));
}

function nearbyCar(player) {
    const vehicle = currentOrLastVehicle(player);
    if (!vehicle || !vehicle.model || vehicle.terminalState !== 0) {
        toast(player, "Get in a car, or call one with /car.", {tone: "bad"});
        return null;
    }
    if (!player.getVehicle() && distance(player.position, vehicle.position) > CAR_REACH) {
        toast(player, "Walk up to your car first.", {tone: "bad"});
        return null;
    }
    return vehicle;
}

function welcome(player) {
    if (welcomed.has(player.id)) return;
    welcomed.add(player.id);
    const record = profiles.get(player);
    const {level, rank} = profiles.progress(record);
    const returning = record.playMs > 0 || record.xp > 0;
    toast(player, returning
        ? `Welcome back, ${player.nickname}. Level ${level} ${rank} · ${formatMoney(player.money)}.`
        : `Welcome to Lost Heaven, ${player.nickname}. You start with ${formatMoney(player.money)}.`,
    {title: "Free Ride", tone: "gold", seconds: 7});
    setTimeout(() => {
        if (player.spawned) toast(player, "F2 opens the city menu · F1 shows who is in town · /help lists commands.", {seconds: 8});
    }, 2500);
}

function showTip() {
    const players = World.getPlayers().filter((player) => player.spawned);
    if (!players.length) return;
    const tip = TIPS[tipIndex++ % TIPS.length];
    for (const player of players) toast(player, tip, {title: "Tip", seconds: 7});
}

function statsLine(record) {
    const {level, rank} = profiles.progress(record);
    const ratio = record.deaths ? (record.kills / record.deaths).toFixed(2) : record.kills.toFixed(2);
    return `${record.name}: level ${level} ${rank}, ${record.kills} kills / ${record.deaths} deaths (${ratio}), ` +
        `${record.racesWon} race wins, ${record.deliveries} deliveries, ${Math.round(record.playMs / 3_600_000 * 10) / 10} h played`;
}

util.onStart(() => {
    spawnTimer = setInterval(spawnReadyPlayers, 200);
    tipTimer = setInterval(showTip, TIP_EVERY_MS);
    // A restart of this resource keeps the connected players.
    for (const player of World.getPlayers()) if (!profiles.get(player)) profiles.attach(player);
});

util.onStop(() => {
    clearInterval(spawnTimer);
    clearInterval(tipTimer);
    clearRetiring();
    // A restart keeps the players connected; take off what this run put on them.
    for (const player of World.getPlayers()) {
        for (const key of ["fr.bounty", "fr.activity", "fr.crew"]) profiles.setPublic(player, key, null);
        nametag.reset(player);
    }
    // Node keeps required modules cached; a restarted resource must load
    // fresh copies of lib/ instead of the stopped ones.
    for (const key of Object.keys(require.cache)) if (key.startsWith(__dirname)) delete require.cache[key];
});

Events.on("playerConnect", (player) => {
    const {returning} = profiles.attach(player);
    feed("join", [name(player, COLORS.gold), text(returning ? " is back in town" : " arrived in Lost Heaven")]);
    console.log(`[${RESOURCE}] ${player.nickname} connected (${returning ? "returning" : "new"})`);
});

Events.on("playerDisconnect", (player) => {
    feed("leave", [name(player), text(" left the city")]);
    profiles.detach(player);
    welcomed.delete(player.id);
    spawnedInGeneration.delete(player.id);
    lastVehicles.delete(player.id);
    for (const key of [...cooldowns.keys()]) if (key.startsWith(`${player.id}:`)) cooldowns.delete(key);
    for (const id of personalCars.get(player.id) ?? []) {
        const vehicle = vehicleById(id);
        if (vehicle && vehicle.getOccupants().every((occupant) => occupant === null)) vehicle.destroy();
    }
    personalCars.delete(player.id);
});

Events.on("missionChange", () => {
    clearRetiring();
    lastVehicles.clear();
    personalCars.clear();
});

Events.on("vehicleDestroy", (vehicle) => {
    clearTimeout(retiringVehicles.get(vehicle.id));
    retiringVehicles.delete(vehicle.id);
    for (const [playerId, id] of lastVehicles) if (id === vehicle.id) lastVehicles.delete(playerId);
    for (const ids of personalCars.values()) ids.delete(vehicle.id);
});

Events.on("vehicleTerminal", (vehicle, state) => {
    if (state !== 2 && state !== 3) return;
    const id = vehicle.id;
    const missionGeneration = World.getMissionGeneration();
    // The engine supplies the splash and fall. The car goes after the death
    // fade and the respawn have had time to run.
    setTimeout(() => {
        if (World.getMissionGeneration() !== missionGeneration) return;
        const current = vehicleById(id);
        if (current && current.terminalState === state) current.destroy();
    }, 6000);
});

Events.on("missionReady", spawnReadyPlayers);
Events.on("playerMissionReady", spawnReadyPlayers);
Events.on("playerSpawn", (player) => { if (!life.isTransit(player)) welcome(player); });
Events.on("vehiclePlayerEntered", (vehicle, player) => lastVehicles.set(player.id, vehicle.id));
Events.on("vehiclePlayerExited", (vehicle, player) => lastVehicles.set(player.id, vehicle.id));

onClient("fr:car:choose", (player, request) => {
    if (!request || request.generation !== World.getMissionGeneration() || !Number.isInteger(request.id)) return;
    const model = CAR_MODELS_BY_ID[request.id];
    if (!model || cooldown(player, "car", CAR_CHOICE_COOLDOWN_MS)) return;
    const result = spawnPersonalCar(player, model);
    util.emit(player, "fr:car:result", {id: request.id, ...result});
    if (result.ok) toast(player, result.message, {tone: "good"});
});

onClient("fr:menu:request", (player) => {
    if (cooldown(player, "menu", MENU_REQUEST_INTERVAL_MS)) return;
    util.emit(player, "fr:menu:data", {
        ...menu.collect(player),
        commands: commands.list(),
        you: {name: player.nickname, money: player.money, ...profiles.progress(profiles.get(player) ?? {xp: 0})},
    });
});

// Menu buttons skip the chat rate limit, so they get their own.
onClient("fr:menu:command", (player, request) => {
    if (!cooldown(player, "menuCommand", MENU_COMMAND_INTERVAL_MS)) commands.runText(player, request?.text);
});

menu.provide("leaderboard", () => {
    if (!leaderboardCache || Date.now() - leaderboardCache.at > LEADERBOARD_CACHE_MS) {
        leaderboardCache = {at: Date.now(), board: {
            level: profiles.leaderboard("level"),
            money: profiles.leaderboard("money"),
            kills: profiles.leaderboard("kills"),
            races: profiles.leaderboard("racesWon"),
        }};
    }
    return leaderboardCache.board;
});

commands.register({name: "help", aliases: ["commands"], usage: "/help", help: "List the commands in chat.", category: "City"}, (player) => {
    const byCategory = new Map();
    for (const entry of commands.list()) {
        if (entry.category === "Dev") continue;
        if (!byCategory.has(entry.category)) byCategory.set(entry.category, []);
        byCategory.get(entry.category).push(`/${entry.name}`);
    }
    player.sendMessage("Free Ride · F2 city menu · F1 players · F9 radio", 0xe8c57d);
    for (const [category, names] of byCategory) player.sendMessage(`${category}: ${names.join(" ")}`, 0xd9cdb4);
});

commands.register({name: "stats", usage: "/stats [player]", help: "Show a career record.", category: "City"}, (player, args) => {
    const target = args.length ? util.findPlayer(args.join(" ")) : player;
    const record = target && profiles.get(target);
    if (!record) {
        toast(player, "No such player in town.", {tone: "bad"});
        return;
    }
    player.sendMessage(statsLine(record), 0xe8c57d);
});

commands.register({name: "car", aliases: ["cars", "v"], usage: "/car [model.i3d]", help: "Open the motor pool or spawn a stock car model.", category: "Cars"}, (player, args) => {
    if (!playable(player)) return;
    if (!args.length) {
        util.emit(player, "fr:car:open", {generation: World.getMissionGeneration()});
        return;
    }
    if (cooldown(player, "car", CAR_CHOICE_COOLDOWN_MS)) return;
    const result = spawnPersonalCar(player, args[0]);
    toast(player, result.message, {tone: result.ok ? "good" : "bad"});
});

commands.register({name: "repair", aliases: ["fix"], usage: "/repair", help: "Patch up your car once every 45 seconds.", category: "Cars"}, (player) => {
    if (!playable(player)) return;
    const vehicle = nearbyCar(player);
    if (!vehicle) return;
    const wait = cooldown(player, "repair", REPAIR_COOLDOWN_MS);
    if (wait) {
        toast(player, `The mechanic needs ${wait} more seconds.`, {tone: "bad"});
        return;
    }
    toast(player, vehicle.repair() ? "Good as new." : "This car cannot be repaired.", {tone: "good"});
});

commands.register({name: "flip", usage: "/flip", help: "Put an overturned car back on its wheels.", category: "Cars"}, (player) => {
    if (!playable(player)) return;
    const vehicle = nearbyCar(player);
    if (!vehicle) return;
    if (speedOf(vehicle) > 2) {
        toast(player, "Let the car come to a stop first.", {tone: "bad"});
        return;
    }
    if (cooldown(player, "flip", FLIP_COOLDOWN_MS)) return;
    const {x, y, z} = vehicle.position;
    vehicle.setTransform({x, y: y + 1.2, z}, headingOf(vehicle.rotation), {x: 0, y: 0, z: 0});
    toast(player, "Back on four wheels.", {tone: "good"});
});

commands.register({name: "engine", usage: "/engine", help: "Start or stop the engine from the driver's seat.", category: "Cars"}, (player) => {
    const vehicle = player.getVehicle();
    if (!vehicle || player.getSeat() !== 0) {
        toast(player, "Sit in the driver's seat first.", {tone: "bad"});
        return;
    }
    vehicle.setEngine(!vehicle.engineOn);
});

commands.register({name: "kill", aliases: ["suicide"], usage: "/kill", help: "End this life and respawn.", category: "City"}, (player) => {
    if (!player.alive) return;
    const busy = life.activity(player);
    if (busy?.kind === "duel") {
        toast(player, "Not in the middle of a duel.", {tone: "bad"});
        return;
    }
    player.setHealth(0);
});

Events.on("consoleCommand", (command, args) => {
    if (command !== "fr") return;
    const [action, ...rest] = args;
    if (action === "save") {
        profiles.save();
        console.log("[freeride] Player records saved");
    } else if (action === "money" && rest.length >= 2) {
        const target = util.findPlayer(rest[0]);
        const amount = Number(rest[1]);
        if (target && Number.isInteger(amount) && target.giveMoney(amount)) console.log(`[freeride] ${target.nickname} now has ${formatMoney(target.money)}`);
        else console.log("[freeride] Usage: fr money <player> <whole dollars>");
    } else if (action === "dev" && ["on", "off"].includes(rest[0])) {
        devmode.set(rest[0] === "on");
        console.log(`[freeride] Development commands ${devmode.isOn() ? "on" : "off"}`);
    } else if (action === "say" && rest.length) {
        toastAll(rest.join(" "), {title: "City Hall", tone: "gold", seconds: 8});
    } else if (action !== "event") {
        console.log("[freeride] Console: fr save | fr money <player> <amount> | fr say <text> | fr dev on|off | fr event <supply|hotcar>");
    }
});
