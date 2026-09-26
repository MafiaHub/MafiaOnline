// A repeatable drive through the Free Ride districts. The server owns the
// clock, checkpoints and payout; the client only draws the current objective.
const TOUR_MISSIONS = new Set(["freeride", "freeridenoc"]);
const TOUR_STOPS = [
    {name: "Salieri's Back Window", position: {x: -1774.5226, y: -4.5073, z: 5.2466}},
    {name: "Yellow Pete's", position: {x: 61.13562, y: 6.12056, z: 113.52177}},
    {name: "The Night Pump", position: {x: -110.6361, y: 8.4794, z: -135.5433}},
    {name: "The Dispensary", position: {x: -761.3176, y: 14.813, z: 762.4796}},
];
const TOUR_TIME_MS = 15 * 60_000;
const TOUR_REWARD = 240;
// Shop markers sit inside or beside their buildings; the road must count.
const STOP_RADIUS = 30;
const STOP_SPEED_METRES_PER_SECOND = 3;
const TICK_MS = 500;
const STATUS_MS = 10_000;
const tours = new Map();
const personalBest = new Map();
let tourTimer = null;

function distance(a, b) {
    return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

function remaining(tour) {
    return Math.max(0, Math.ceil((tour.deadline - Date.now()) / 1000));
}

function sendStatus(player, tour = tours.get(player.id)) {
    const stop = tour && TOUR_STOPS[tour.route[tour.next]];
    player.emit("sample:tour", JSON.stringify({
        active: !!stop,
        checkpoint: stop ? tour.next + 1 : 0,
        total: tour ? tour.route.length : 0,
        name: stop?.name ?? "",
        secondsRemaining: tour ? remaining(tour) : 0,
        bestSeconds: personalBest.get(player.id) ?? null,
        position: stop?.position ?? null,
    }));
}

function sendCompass(player, stop) {
    player.emit("sample:hud", JSON.stringify(stop
        ? {action: "compass", position: stop.position}
        : {action: "compassClear"}));
}

function finish(player, reason) {
    const tour = tours.get(player.id);
    if (!tour) return;
    tours.delete(player.id);
    sendStatus(player);
    sendCompass(player, null);
    if (reason) player.sendMessage(reason);
}

function start(player) {
    if (!TOUR_MISSIONS.has(World.getMission()) || !World.isReady() ||
        !player.spawned || !player.alive || player.virtualWorld !== 0) {
        player.sendMessage("The city circuit is available after entering Free Ride.");
        return;
    }
    if (player.getSeat() !== 0) {
        player.sendMessage("Drive a car to start the city circuit. Use /car if you need one.");
        return;
    }
    if (tours.has(player.id)) {
        player.sendMessage("You are already on a circuit. Use /tour status or /tour cancel.");
        return;
    }
    const car = player.getVehicle();
    const origin = car.position;
    let nearest = 0;
    for (let index = 1; index < TOUR_STOPS.length; index++) {
        if (distance(origin, TOUR_STOPS[index].position) < distance(origin, TOUR_STOPS[nearest].position)) nearest = index;
    }
    // Start with the next landmark so a player cannot claim a checkpoint while
    // parked at the closest one. Three different districts make a full run.
    const route = [1, 2, 3].map((offset) => (nearest + offset) % TOUR_STOPS.length);
    const now = Date.now();
    const tour = {generation: World.getMissionGeneration(), route, next: 0,
        startedAt: now, deadline: now + TOUR_TIME_MS, lastStatusAt: now};
    tours.set(player.id, tour);
    player.sendMessage(`City circuit started: visit three districts in 15 minutes for $${TOUR_REWARD}. Stop the car at each marker.`);
    player.sendMessage(`First stop: ${TOUR_STOPS[route[0]].name}. Use /tour status or /tour cancel.`);
    sendStatus(player, tour);
    sendCompass(player, TOUR_STOPS[route[0]]);
}

function tick() {
    const players = new Map(World.getPlayers().map((player) => [player.id, player]));
    const now = Date.now();
    for (const [id, tour] of tours) {
        const player = players.get(id);
        if (!player) {
            tours.delete(id);
            continue;
        }
        if (!TOUR_MISSIONS.has(World.getMission()) ||
            tour.generation !== World.getMissionGeneration() || !player.spawned ||
            !player.alive || player.virtualWorld !== 0) {
            finish(player, "City circuit cancelled.");
            continue;
        }
        if (now >= tour.deadline) {
            finish(player, "City circuit time ran out. You can try again with /tour.");
            continue;
        }
        const car = player.getSeat() === 0 ? player.getVehicle() : null;
        const stop = TOUR_STOPS[tour.route[tour.next]];
        if (car && car.missionGeneration === tour.generation && car.terminalState === 0 &&
            distance(car.position, stop.position) <= STOP_RADIUS &&
            Math.hypot(car.velocity.x, car.velocity.y, car.velocity.z) <= STOP_SPEED_METRES_PER_SECOND) {
            tour.next++;
            if (tour.next === tour.route.length) {
                const seconds = Math.ceil((now - tour.startedAt) / 1000);
                const best = personalBest.get(id);
                if (best === undefined || seconds < best) personalBest.set(id, seconds);
                const paid = player.giveMoney(TOUR_REWARD);
                finish(player, `Circuit complete in ${Math.floor(seconds / 60)}m ${seconds % 60}s. ${paid ? `Earned $${TOUR_REWARD}.` : "Your wallet is full."}`);
                continue;
            }
            const next = TOUR_STOPS[tour.route[tour.next]];
            player.sendMessage(`Checkpoint ${tour.next}/3 reached. Next: ${next.name}.`);
            tour.lastStatusAt = now;
            sendStatus(player, tour);
            sendCompass(player, next);
        } else if (now - tour.lastStatusAt >= STATUS_MS) {
            tour.lastStatusAt = now;
            sendStatus(player, tour);
        }
    }
}

Events.on("resourceStart", (name) => {
    if (name === "mafia1online-sample") tourTimer = setInterval(tick, TICK_MS);
});
Events.on("resourceStop", (name) => {
    if (name !== "mafia1online-sample") return;
    if (tourTimer !== null) clearInterval(tourTimer);
    tourTimer = null;
    tours.clear();
    personalBest.clear();
});
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) finish(player, "City circuit cancelled as the light changed.");
});
Events.on("playerDeath", (player) => finish(player, "City circuit ended when you died."));
Events.on("playerDisconnect", (player) => {
    tours.delete(player.id);
    personalBest.delete(player.id);
});
Events.on("playerSpawn", (player) => sendStatus(player));
Events.on("playerCommand", (player, command, args) => {
    if (command.toLowerCase() !== "tour") return;
    const action = args[0]?.toLowerCase() ?? "start";
    if (action === "start") return start(player);
    if (action === "cancel") {
        if (tours.has(player.id)) finish(player, "City circuit cancelled.");
        else player.sendMessage("No city circuit is active.");
        return;
    }
    if (action === "status") {
        const tour = tours.get(player.id);
        sendStatus(player, tour);
        player.sendMessage(tour
            ? `Checkpoint ${tour.next + 1}/3: ${TOUR_STOPS[tour.route[tour.next]].name}; ${Math.ceil(remaining(tour) / 60)} minutes left.`
            : `No circuit active. Drive a car and use /tour. Best: ${personalBest.has(player.id) ? `${personalBest.get(player.id)} seconds` : "none"}.`);
        return;
    }
    player.sendMessage("Use /tour [start|status|cancel].");
});
