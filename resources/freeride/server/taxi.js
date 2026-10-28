// Taxi fares. A player in a cab takes calls from street corners across the
// city: pick the fare up, then get them to their corner before the meter
// runs out for a tip. Corners come from the road graph.
const roads = require("./lib/roads.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, card, compass, markers, COLORS} = require("./lib/notify.js");
const {flatDistance, playable, speedOf, headingOf, formatMoney, formatMinutes, onStart, onStop} = require("./lib/util.js");

const TAXI_MODELS = new Set(["taxi00.i3d", "fordapicktaxi00.i3d", "phantomtaxi00.i3d"]);
const STOP_RADIUS = 14;
const STOP_SPEED = 3;
const OUT_OF_CAB_MS = 20_000;
const TICK_MS = 500;
const CARD_MS = 1000;
const TIP = 0.3;
const XP = 25;

const shifts = new Map();
let timer = null;

function cabOf(player) {
    const car = player.getSeat() === 0 ? player.getVehicle() : null;
    return car && TAXI_MODELS.has(car.model.toLowerCase()) ? car : null;
}

// Country roads have few crossings, so a fare can wait anywhere on a road.
function corner(origin, minimum, maximum) {
    const graph = roads.current();
    const index = graph.crossingAround(origin, minimum, maximum);
    if (index !== null) return graph.crossings[index];
    for (let attempt = 0; attempt < 40; attempt++) {
        const {position} = graph.roadPoint();
        const gap = flatDistance(position, origin);
        if (gap >= minimum && gap <= maximum * 2) return position;
    }
    return graph.roadPoint().position;
}

function call(player, shift) {
    const cab = cabOf(player);
    shift.phase = "pickup";
    shift.target = corner(cab?.position ?? player.position, 150, 450);
    shift.deadline = 0;
    compass(player, "taxi", 70, {position: shift.target});
    markers(player, "taxi", [{id: "fare", position: shift.target, label: "FARE WAITING", color: COLORS.gold, beacon: true, primary: true}]);
}

function end(player, message) {
    const shift = shifts.get(player.id);
    if (!shift) return;
    shifts.delete(player.id);
    life.end(player, "taxi");
    compass(player, "taxi", 70, null);
    markers(player, "taxi", []);
    card(player, "taxi", null);
    if (message) toast(player, `${message} ${shift.fares} fare${shift.fares === 1 ? "" : "s"}, ${formatMoney(shift.earned)}.`, {title: "Taxi"});
}

function start(player) {
    if (!playable(player)) return;
    if (!life.begin(player, "taxi", "Driving a cab")) {
        toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    if (!cabOf(player)) {
        const heading = headingOf(player.getVehicle()?.rotation ?? player.rotation);
        const {x, y, z} = player.getVehicle()?.position ?? player.position;
        if (player.getVehicle()) player.removeFromVehicle();
        const cab = Vehicle.spawn("taxi00.i3d", {x: x + Math.cos(heading) * 3.5, y: y + 0.3, z: z - Math.sin(heading) * 3.5}, heading, player);
        if (!cab || !player.putInVehicle(cab, 0)) {
            cab?.destroy();
            life.end(player, "taxi");
            toast(player, "The cab company has nothing free.", {tone: "bad"});
            return;
        }
        cab.state.set("fr.fullTank", true);
        cab.state.set("fr.owner", player.nickname);
        cab.setRadarMarker(true, 0xe2b866);
    }
    const shift = {phase: "pickup", target: null, from: null, deadline: 0, fares: 0, earned: 0, outSince: null, cardAt: 0};
    shifts.set(player.id, shift);
    call(player, shift);
    toast(player, "Dispatch has a fare for you. Follow the compass and stop at the corner. /taxi to clock off.", {title: "Taxi", tone: "gold"});
}

function tick() {
    const now = Date.now();
    for (const [id, shift] of shifts) {
        const player = World.getPlayers().find((other) => other.id === id);
        if (!player) {
            shifts.delete(id);
            continue;
        }
        if (!playable(player)) {
            end(player, "Shift over.");
            continue;
        }
        const cab = cabOf(player);
        if (!cab) {
            shift.outSince ??= now;
            if (now - shift.outSince > OUT_OF_CAB_MS) end(player, "You left the cab. Shift over.");
            continue;
        }
        shift.outSince = null;
        const arrived = flatDistance(cab.position, shift.target) <= STOP_RADIUS && speedOf(cab) < STOP_SPEED;
        if (shift.phase === "pickup" && arrived) {
            shift.from = shift.target;
            shift.phase = "ride";
            shift.target = corner(shift.from, 700, 1600);
            const straight = flatDistance(shift.from, shift.target);
            shift.deadline = now + (straight / 9 + 30) * 1000;
            compass(player, "taxi", 70, {position: shift.target});
            markers(player, "taxi", [{id: "drop", position: shift.target, label: "DROP-OFF", color: COLORS.green, beacon: true, primary: true}]);
            toast(player, `Fare aboard: ${Math.round(straight)} m across town. Make it quick for a tip.`, {title: "Taxi", tone: "gold"});
        } else if (shift.phase === "ride" && arrived) {
            const fare = Math.round(30 + flatDistance(shift.from, shift.target) * 0.2);
            const tip = now <= shift.deadline ? Math.round(fare * TIP) : 0;
            player.giveMoney(fare + tip);
            shift.fares++;
            shift.earned += fare + tip;
            profiles.bump(player, "fares");
            profiles.addXp(player, XP, "Taxi fare");
            toast(player, tip ? `Fare ${formatMoney(fare)} and a ${formatMoney(tip)} tip.` : `Fare ${formatMoney(fare)}. No tip; you were late.`, {title: "Taxi", tone: "good"});
            call(player, shift);
        }
        if (now - shift.cardAt >= CARD_MS) {
            shift.cardAt = now;
            card(player, "taxi", {title: "Taxi", subtitle: shift.phase === "pickup" ? "Pick up the fare" : "Drive the fare to the drop-off",
                rows: [["Distance", `${Math.round(flatDistance(cab.position, shift.target))} m`],
                    ...(shift.phase === "ride" ? [["Tip window", shift.deadline > now ? formatMinutes(shift.deadline - now) : "missed"]] : []),
                    ["This shift", `${shift.fares} · ${formatMoney(shift.earned)}`]], tone: "gold"});
        }
    }
}

commands.register({name: "taxi", aliases: ["cab"], usage: "/taxi", help: "Start or end a taxi shift.", category: "City"}, (player) => {
    if (shifts.has(player.id)) end(player, "Shift over.");
    else start(player);
});

menu.provide("taxi", (player) => ({active: shifts.has(player.id), fares: shifts.get(player.id)?.fares ?? 0}));

onStart(() => { timer = setInterval(tick, TICK_MS); });
onStop(() => clearInterval(timer));
Events.on("playerDisconnect", (player) => shifts.delete(player.id));
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) end(player, null);
});
