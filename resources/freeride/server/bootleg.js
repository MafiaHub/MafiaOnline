// Bootleg runs, on maps with both the countryside and the city: drive out to
// the still in the hills, wait while the crates are loaded, then bring them
// to Salieri's bar before the whisky is missed. Loading the car tips off the
// police. Any car will do, but it has to arrive in one piece.
const maps = require("./lib/maps.js");
const roads = require("./lib/roads.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, card, compass, markers, feed, name, text, COLORS} = require("./lib/notify.js");
const {flatDistance, playable, speedOf, vehicleById, formatMoney, formatMinutes, onStart, onStop} = require("./lib/util.js");

const STILL = "hills";
const BUYER = "salieri";
const STOP_RADIUS = 16;
const LOAD_MS = 8000;
const DELIVERY_MS = 10 * 60_000;
const PAY = 700;
const XP = 120;
const TICK_MS = 500;

const runs = new Map();
let timer = null;

function available() {
    return Boolean(maps.current()?.places.includes(STILL) && maps.current().places.includes(BUYER));
}

function end(player, message, tone = "bad") {
    const run = runs.get(player.id);
    if (!run) return;
    runs.delete(player.id);
    life.end(player, "bootleg");
    compass(player, "bootleg", 65, null);
    markers(player, "bootleg", []);
    card(player, "bootleg", null);
    if (message) toast(player, message, {title: "Bootleg run", tone});
}

// Cars stop on the road nearest each landmark, not at the building itself.
function stop(key) {
    const graph = roads.current();
    return graph.crossings[graph.nearestCrossing(maps.landmark(key).position)];
}

function point(key, label, color) {
    return [{id: key, position: stop(key), label, color, beacon: true, primary: true}];
}

function tick() {
    const now = Date.now();
    for (const [id, run] of runs) {
        const player = World.getPlayers().find((other) => other.id === id);
        if (!player) {
            runs.delete(id);
            continue;
        }
        if (!playable(player) || !available()) {
            end(player, "The run is off.");
            continue;
        }
        const car = player.getSeat() === 0 ? player.getVehicle() : null;
        if (run.phase === "deliver") {
            const loaded = vehicleById(run.carId);
            if (!loaded || loaded.terminalState !== 0) {
                end(player, "The car is wrecked and the whisky with it.");
                continue;
            }
            if (now > run.deadline) {
                end(player, "Too slow. Salieri found another supplier for tonight.");
                continue;
            }
        }
        const goal = stop(run.phase === "deliver" ? BUYER : STILL);
        const stopped = car && flatDistance(car.position, goal) <= STOP_RADIUS && speedOf(car) < 2;
        if (run.phase === "drive") {
            if (stopped) {
                run.phase = "load";
                run.loadUntil = now + LOAD_MS;
                run.carId = car.id;
                toast(player, "The boys are loading the crates. Keep the engine running.", {title: "Bootleg run"});
            }
        } else if (run.phase === "load") {
            if (!stopped || car.id !== run.carId) {
                run.phase = "drive";
                toast(player, "You pulled away before the crates were in.", {title: "Bootleg run", tone: "bad"});
            } else if (now >= run.loadUntil) {
                run.phase = "deliver";
                run.deadline = now + DELIVERY_MS;
                compass(player, "bootleg", 65, {position: stop(BUYER)});
                markers(player, "bootleg", point(BUYER, "DELIVER TO SALIERI'S", COLORS.green));
                Police.setWanted(player, 1);
                toast(player, `Loaded. Get it to Salieri's in ${formatMinutes(DELIVERY_MS)}. Somebody saw you.`, {title: "Bootleg run", tone: "gold"});
            }
        } else if (stopped && car.id === run.carId) {
            player.giveMoney(PAY);
            profiles.bump(player, "deliveries");
            profiles.addXp(player, XP, "Bootleg run");
            feed("bootleg", [name(player, COLORS.gold), text(" brought a load of whisky to Salieri's · "), text(formatMoney(PAY), COLORS.gold)]);
            end(player, `Salieri pays ${formatMoney(PAY)} for the load.`, "good");
            continue;
        }
        card(player, "bootleg", {title: "Bootleg run", subtitle: run.phase === "deliver" ? "Bring the whisky to Salieri's bar" : run.phase === "load" ? "Loading at the still" : "Drive to the still in the hills",
            rows: [["Distance", `${Math.round(flatDistance(player.position, goal))} m`],
                ...(run.phase === "deliver" ? [["Time left", formatMinutes(run.deadline - now)]] : []), ["Pay", formatMoney(PAY)]],
            progress: run.phase === "load" ? 1 - (run.loadUntil - now) / LOAD_MS : run.phase === "deliver" ? (run.deadline - now) / DELIVERY_MS : undefined, tone: "gold"});
    }
}

commands.register({name: "bootleg", aliases: ["whisky"], usage: "/bootleg [cancel]", help: "Run whisky from the hills to Salieri's (Lost Heaven Extended).", category: "City"}, (player, args) => {
    if (args[0]?.toLowerCase() === "cancel") {
        end(player, "You called the run off.");
        return;
    }
    if (runs.has(player.id) || !playable(player)) return;
    if (!available()) {
        toast(player, "There are no stills on this map. Play Lost Heaven Extended (/map).", {tone: "bad"});
        return;
    }
    if (!life.begin(player, "bootleg", "Bootleg run")) {
        toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    runs.set(player.id, {phase: "drive", carId: null, loadUntil: 0, deadline: 0});
    compass(player, "bootleg", 65, {position: stop(STILL)});
    markers(player, "bootleg", point(STILL, "THE STILL", COLORS.gold));
    toast(player, "Salieri needs whisky. Drive out to the still in the hills and stop at the marker.", {title: "Bootleg run", tone: "gold"});
});

menu.provide("bootleg", (player) => ({available: available(), active: runs.has(player.id), pay: PAY}));

onStart(() => { timer = setInterval(tick, TICK_MS); });
onStop(() => clearInterval(timer));
Events.on("playerDisconnect", (player) => runs.delete(player.id));
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) end(player, null);
});
