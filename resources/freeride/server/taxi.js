// Taxi fares, the single-player way. Anyone driving a cab (taxi00,
// fordapicktaxi00, phantomtaxi00) gets hailed: a walker up the street waves
// at the cab (taxiHail) and keeps waving; stop next to them (taxiPickup) and
// they get into the back. Drive them to a corner across town on the meter,
// and arrive quickly and smoothly for a tip. A crash makes the passenger
// complain; a third gets them out without paying. Without a simulation host
// (no walkers) or when nobody hails for a while, dispatch sends the cab to a
// fare waiting at a street corner instead. /taxi starts or ends a shift and
// brings a cab.
const roads = require("./lib/roads.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const {toast, card, compass, markers, COLORS} = require("./lib/notify.js");
const {flatDistance, playable, speedOf, headingOf, vehicleById, formatMoney, formatMinutes, onStart, onStop} = require("./lib/util.js");

const TAXI_MODELS = new Set(["taxi00.i3d", "fordapicktaxi00.i3d", "phantomtaxi00.i3d"]);
const STOP_RADIUS = 15;
const STOP_SPEED = 3;
const OUT_OF_CAB_MS = 20_000;
const TICK_MS = 500;
const CARD_MS = 1000;
const XP = 25;
// The meter: a flag drop and a rate per metre driven with the fare aboard,
// up to METER_CAP times the quote for the straight route.
const FLAG_DROP = 8;
const RATE_PER_METRE = 0.12;
const METER_CAP = 1.5;
// Tips, as shares of the quoted fare: up to SPEED_TIP for the time left of
// the ride time (retail pays the fare plus the fare times the time left),
// SMOOTH_TIP for a ride without a crash.
const SPEED_TIP = 0.4;
const SMOOTH_TIP = 0.2;
const CRASHES_TO_LEAVE = 3;
// A crash: the cab loses this much speed in one tick, or its damage changes.
const CRASH_SPEED_DROP = 9;
const CRASH_COOLDOWN_MS = 3000;
// With walkers about, dispatch calls in a corner fare only after this long
// without a hail.
const DISPATCH_AFTER_MS = 45_000;
const HAIL_TOAST_MS = 20_000;

const shifts = new Map();
// When a player without a shift was last told someone waves.
const hailToasts = new Map();
// The cab /taxi brought each player, sent back with the shift when empty.
const companyCabs = new Map();
let timer = null;

function cabOf(player) {
    const car = player.getSeat() === 0 ? player.getVehicle() : null;
    return car && TAXI_MODELS.has(car.model.toLowerCase()) ? car : null;
}

function taxiApi() {
    return typeof Taxi === "object" && Taxi ? Taxi : null;
}

function hailsAvailable() {
    return Boolean(taxiApi()?.isAvailable());
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

// Retail's ride time for a path: 60 * ((20 + length / 13.8) / 60 + 1)
// seconds in whole minutes (C_traffic_element::MavaNaTaxi). The road is
// taken as a third longer than the straight line.
function rideTimeMs(straight) {
    const length = straight * 1.3;
    return 60_000 * (Math.floor((20 + length * 0.072463766) / 60) + 1);
}

function quoteFor(straight) {
    return Math.round(30 + straight * 0.2);
}

function shiftOf(player) {
    return shifts.get(player.id) ?? null;
}

function clearRide(player, shift) {
    shift.passenger = null;
    shift.ride = null;
    shift.phase = "cruise";
    shift.idleSince = Date.now();
    compass(player, "taxi", 70, null);
    showMarkers(player, shift);
}

function showMarkers(player, shift) {
    const items = [];
    if (shift.ride?.target) items.push({id: "drop", position: shift.ride.target, label: "DROP-OFF", color: COLORS.green, beacon: true, primary: true});
    else if (shift.corner) items.push({id: "fare", position: shift.corner.target, label: "FARE WAITING", color: COLORS.gold, beacon: true, primary: true});
    if (!shift.ride) {
        for (const [id, hail] of shift.hails) items.push({id: `hail${id}`, position: hail.pedestrian.position, label: "HAILING", color: COLORS.gold, beacon: false, primary: false});
    }
    markers(player, "taxi", items);
}

function returnCab(playerId, delayMs = 3000) {
    const id = companyCabs.get(playerId);
    if (id === undefined) return;
    companyCabs.delete(playerId);
    const send = () => {
        const cab = vehicleById(id);
        if (cab && cab.getOccupants().every((occupant) => occupant === null)) cab.destroy();
    };
    if (delayMs > 0) setTimeout(send, delayMs);
    else send();
}

function end(player, message) {
    const shift = shifts.get(player.id);
    if (!shift) return;
    shifts.delete(player.id);
    returnCab(player.id);
    if (shift.passenger) taxiApi()?.dropOff(shift.passenger);
    life.end(player, "taxi");
    compass(player, "taxi", 70, null);
    markers(player, "taxi", []);
    card(player, "taxi", null);
    if (message) toast(player, `${message} ${shift.fares} fare${shift.fares === 1 ? "" : "s"}, ${formatMoney(shift.earned)}.`, {title: "Taxi"});
}

// A shift is the life activity "taxi"; a player busy with something else
// cannot take fares.
function begin(player) {
    if (shifts.has(player.id)) return shifts.get(player.id);
    if (!playable(player) || !life.begin(player, "taxi", "Driving a cab")) return null;
    const shift = {phase: "cruise", fares: 0, earned: 0, outSince: null, cardAt: 0, idleSince: Date.now(), hails: new Map(), hailToastAt: 0,
        passenger: null, ride: null, corner: null, lastSpeed: 0};
    shifts.set(player.id, shift);
    return shift;
}

function start(player) {
    if (!playable(player)) return;
    if (life.activity(player) && life.activity(player).kind !== "taxi") {
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
            toast(player, "The cab company has nothing free.", {tone: "bad"});
            return;
        }
        cab.state.set("fr.fullTank", true);
        cab.state.set("fr.owner", player.nickname);
        cab.setRadarMarker(true, 0xe2b866);
        // One company cab at a time: the last one goes back.
        returnCab(player.id, 0);
        companyCabs.set(player.id, cab.id);
    }
    if (!begin(player)) {
        returnCab(player.id);
        toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    const shift = shiftOf(player);
    if (hailsAvailable()) {
        toast(player, "Cruise the streets: people wave cabs down. Pull up next to one and they get in. /taxi to clock off.", {title: "Taxi", tone: "gold"});
    } else {
        dispatch(player, shift);
        toast(player, "Dispatch has a fare for you. Follow the compass and stop at the corner. /taxi to clock off.", {title: "Taxi", tone: "gold"});
    }
}

// The fallback: a fare waiting at a street corner.
function dispatch(player, shift) {
    const cab = cabOf(player);
    shift.corner = {target: corner(cab?.position ?? player.position, 150, 450)};
    compass(player, "taxi", 70, {position: shift.corner.target});
    showMarkers(player, shift);
}

function startRide(player, shift, from, passenger) {
    const target = corner(from, 700, 1600);
    const straight = flatDistance(from, target);
    const now = Date.now();
    const allowed = rideTimeMs(straight);
    shift.corner = null;
    shift.phase = "ride";
    shift.ride = {from: {...from}, target, quote: quoteFor(straight), metres: 0, last: null, started: now, allowed, deadline: now + allowed, crashes: 0, lastCrashAt: 0};
    if (passenger) taxiApi()?.setDestination(passenger, target, STOP_RADIUS);
    compass(player, "taxi", 70, {position: target});
    showMarkers(player, shift);
    toast(player, `"Take me across town, ${Math.round(straight)} m." The meter is running; quick and smooth gets a tip.`, {title: "Taxi", tone: "gold"});
}

function meterOf(ride) {
    return Math.min(Math.round(FLAG_DROP + ride.metres * RATE_PER_METRE), Math.round(ride.quote * METER_CAP));
}

function pay(player, shift) {
    const ride = shift.ride;
    const now = Date.now();
    const meter = meterOf(ride);
    const timeLeft = Math.max(0, ride.deadline - now) / ride.allowed;
    const speedTip = Math.round(ride.quote * SPEED_TIP * timeLeft);
    const smoothTip = ride.crashes === 0 ? Math.round(ride.quote * SMOOTH_TIP) : 0;
    const total = meter + speedTip + smoothTip;
    player.giveMoney(total);
    shift.fares++;
    shift.earned += total;
    profiles.bump(player, "fares");
    profiles.addXp(player, XP, "Taxi fare");
    const tips = [speedTip && `${formatMoney(speedTip)} for speed`, smoothTip && `${formatMoney(smoothTip)} for a smooth ride`].filter(Boolean);
    toast(player, tips.length ? `Meter ${formatMoney(meter)}, tips ${tips.join(" and ")}.` : `Meter ${formatMoney(meter)}. No tip.`, {title: "Taxi", tone: "good"});
    return total;
}

function crash(player, shift) {
    const ride = shift.ride;
    const now = Date.now();
    // Paid or already walking out: the fare is on the pavement.
    if (!ride || ride.paid || ride.walkedOut || now - ride.lastCrashAt < CRASH_COOLDOWN_MS) return;
    ride.lastCrashAt = now;
    ride.crashes++;
    if (ride.crashes >= CRASHES_TO_LEAVE) {
        toast(player, "\"That's it, let me out!\" Your fare leaves without paying.", {title: "Taxi", tone: "bad"});
        ride.walkedOut = true;
        if (shift.passenger) taxiApi()?.dropOff(shift.passenger);
        else clearRide(player, shift);
        return;
    }
    const lines = ["\"Hey! Watch the road!\"", "\"Are you trying to kill me?\""];
    toast(player, `${lines[Math.min(ride.crashes, lines.length) - 1]} No tip for a rough ride.`, {title: "Taxi", tone: "bad"});
}

// ---- Native taxi passengers (Taxi module) -------------------------------

Events.on("taxiHail", (player, pedestrian) => {
    if (!player || !pedestrian) return;
    const shift = shiftOf(player);
    if (shift?.ride) return;
    if (shift) {
        shift.hails.set(pedestrian.id, {pedestrian});
        showMarkers(player, shift);
    }
    const now = Date.now();
    const last = shift?.hailToastAt ?? hailToasts.get(player.id) ?? 0;
    if (now - last >= HAIL_TOAST_MS) {
        if (shift) shift.hailToastAt = now; else hailToasts.set(player.id, now);
        toast(player, "Someone up the street is waving you down. Pull up next to them.", {title: "Taxi", tone: "gold"});
    }
});

Events.on("taxiHailEnd", (player, pedestrian) => {
    const shift = player && shiftOf(player);
    if (!shift || !pedestrian || !shift.hails.delete(pedestrian.id)) return;
    showMarkers(player, shift);
});

Events.on("taxiPickup", (player, pedestrian) => {
    if (!player || !pedestrian || !cabOf(player)) return;
    const api = taxiApi();
    const shift = begin(player);
    if (!shift || shift.passenger || shift.ride) {
        api.decline(pedestrian);
        if (!shift) toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    const passenger = api.board(player, pedestrian);
    if (!passenger) {
        toast(player, "They changed their mind.", {title: "Taxi"});
        return;
    }
    shift.hails.clear();
    shift.corner = null;
    shift.passenger = passenger;
    shift.phase = "boarding";
    shift.idleSince = Date.now();
    compass(player, "taxi", 70, null);
    showMarkers(player, shift);
});

Events.on("taxiPassengerEnter", (player, passenger, vehicle) => {
    const shift = player && shiftOf(player);
    if (!shift || !passenger || shift.passenger?.id !== passenger.id) return;
    startRide(player, shift, vehicle?.position ?? player.position, passenger);
});

Events.on("taxiArrive", (player, passenger) => {
    const shift = player && shiftOf(player);
    if (!shift || !passenger || shift.passenger?.id !== passenger.id || !shift.ride || shift.ride.paid) return;
    shift.ride.paid = pay(player, shift);
    taxiApi()?.dropOff(passenger);
});

Events.on("taxiPassengerExit", (player, passenger, _vehicle, info) => {
    const shift = player && shiftOf(player);
    if (!shift || !passenger || shift.passenger?.id !== passenger.id) return;
    const ride = shift.ride;
    if (!ride?.paid && !ride?.walkedOut) {
        const why = {driverLeft: "You left the cab; your fare walked off without paying.", failed: "The fare gave up on the cab.",
            died: "Your fare is dead.", gone: "Your fare is gone."}[info?.reason];
        if (why) toast(player, why, {title: "Taxi", tone: "bad"});
    }
    clearRide(player, shift);
});

Events.on("vehicleDamage", (vehicle) => {
    for (const [id, shift] of shifts) {
        if (!shift.ride || shift.phase !== "ride") continue;
        const player = World.getPlayers().find((other) => other.id === id);
        if (player && cabOf(player)?.id === vehicle?.id) crash(player, shift);
    }
});

// ---- The shift loop ------------------------------------------------------

function tick() {
    const now = Date.now();
    const api = taxiApi();
    // Walkers hail any cab driver, as in single player, but not one busy
    // with something else (a race, police duty) who cannot take the fare.
    if (api) {
        for (const player of World.getPlayers()) {
            const busy = life.activity(player);
            const wanted = Boolean(cabOf(player)) && playable(player) && (!busy || busy.kind === "taxi");
            if (api.isHailable(player) !== wanted) api.setHailable(player, wanted);
        }
    }
    for (const [id, shift] of shifts) {
        const player = World.getPlayers().find((other) => other.id === id);
        if (!player) {
            shifts.delete(id);
            continue;
        }
        // Dead, or taken off the job by another module (an arrest and the
        // jail): the fare gets out.
        if (!playable(player) || life.activity(player)?.kind !== "taxi") {
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
        const speed = speedOf(cab);
        const ride = shift.ride;
        if (ride && shift.phase === "ride") {
            if (ride.last) ride.metres += flatDistance(ride.last, cab.position);
            ride.last = {...cab.position};
            if (shift.lastSpeed - speed >= CRASH_SPEED_DROP) crash(player, shift);
        }
        shift.lastSpeed = speed;
        const stopped = speed < STOP_SPEED;
        if (!shift.passenger && !ride) {
            if (shift.corner && stopped && flatDistance(cab.position, shift.corner.target) <= STOP_RADIUS) {
                // A corner fare rides along without a body.
                startRide(player, shift, shift.corner.target, null);
            } else if (!shift.corner && (!hailsAvailable() || now - shift.idleSince >= DISPATCH_AFTER_MS) && shift.hails.size === 0) {
                dispatch(player, shift);
                toast(player, "Dispatch has a fare waiting at a corner. Follow the compass.", {title: "Taxi", tone: "gold"});
            }
        } else if (ride && !shift.passenger && !ride.paid && stopped && flatDistance(cab.position, ride.target) <= STOP_RADIUS) {
            ride.paid = pay(player, shift);
            clearRide(player, shift);
            shift.idleSince = now;
        }
        if (shift.hails.size && !shift.ride) showMarkers(player, shift);
        if (now - shift.cardAt >= CARD_MS) {
            shift.cardAt = now;
            const current = shift.ride;
            const rows = [];
            let subtitle = "Cruise for a fare";
            if (shift.phase === "boarding") subtitle = "Your fare is getting in";
            else if (current) {
                subtitle = "Drive the fare to the drop-off";
                rows.push(["Meter", formatMoney(meterOf(current))], ["Drop-off", `${Math.round(flatDistance(cab.position, current.target))} m`],
                    ["Tip window", current.deadline > now ? formatMinutes(current.deadline - now) : "missed"],
                    ["Ride", current.crashes ? `${current.crashes} crash${current.crashes === 1 ? "" : "es"}` : "smooth"]);
            } else if (shift.corner) {
                subtitle = "Pick up the fare at the corner";
                rows.push(["Distance", `${Math.round(flatDistance(cab.position, shift.corner.target))} m`]);
            } else if (shift.hails.size) {
                subtitle = "Someone is waving you down";
            }
            rows.push(["This shift", `${shift.fares} · ${formatMoney(shift.earned)}`]);
            card(player, "taxi", {title: "Taxi", subtitle, rows, tone: "gold"});
        }
    }
}

commands.register({name: "taxi", aliases: ["cab"], usage: "/taxi", help: "Start or end a taxi shift.", category: "City"}, (player) => {
    if (shifts.has(player.id)) end(player, "Shift over.");
    else start(player);
});

menu.provide("taxi", (player) => ({active: shifts.has(player.id), fares: shifts.get(player.id)?.fares ?? 0}));

onStart(() => { timer = setInterval(tick, TICK_MS); });
onStop(() => {
    clearInterval(timer);
    const api = taxiApi();
    if (api) for (const player of World.getPlayers()) if (api.isHailable(player)) api.setHailable(player, false);
    // Passengers out (Taxi.dropOff removes them), cards and markers off.
    for (const player of World.getPlayers()) end(player, null);
    for (const playerId of [...companyCabs.keys()]) returnCab(playerId, 0);
});
Events.on("playerDisconnect", (player) => {
    shifts.delete(player.id);
    hailToasts.delete(player.id);
    returnCab(player.id, 0);
});
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) end(player, null);
    companyCabs.clear();
});

module.exports = {shifts, TAXI_MODELS, rideTimeMs, quoteFor};
