// The ambulance. A player signs on at Lost Heaven Hospital (the stock
// models/9medic.4ds, which both city scenes' cache.bin place at
// (-759.22, 11.56, 778.99): its entrance steps run down to z 758 facing the
// street whose road graph line runs along z 729.1; the sign-on arrow is at
// the foot of the steps) and takes one of the stock ambulances parked at the
// kerb (lib/stations.js keeps them there). Calls come in from the street: someone collapsed, someone
// badly hurt, a smoke victim the fire brigade carried out, and players who
// went down while a medic is on duty: they wait for one before the usual
// respawn (lib/life.js holds it). The medic drives there, holds G beside
// the patient to treat them and either leaves them on their feet or takes
// them to the hospital, riding in the ambulance, before the time runs out
// for a tip.
//
// Some calls are more than that. A woman in labour rides in for a bigger fare
// and a tip for a smooth ride: every bump makes her cry out. A heart attack
// needs CPR on the pavement, G tapped in a steady rhythm, before his time
// runs out; then he rides in. A pile-up leaves three people hurt around two
// wrecks, tagged critical, serious and minor: the critical one bleeds out
// unless treated first, then rides in, the other two are patched up where
// they sit, and with the fire brigade on duty one wreck burns beside them
// (firefighters.js). The patients talk in their own voices: moaning while
// they wait, crying out at a crash.
const roads = require("./lib/roads.js");
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const jobkey = require("./lib/jobkey.js");
const stations = require("./lib/stations.js");
const firefighters = require("./firefighters.js");
const barRoom = require("./lib/bar_room.js");
const prison = require("./lib/prison.js");
const {toast, card, compass, markers, markersAll, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, flatDistance, playable, playerById, vehicleById, speedOf, formatMoney, formatMinutes, headingOf, pick, onStart, onStop, inFreeRide} =
    require("./lib/util.js");

const AMBULANCE = "ambulance00.i3d";
const CITY_HOSPITAL = {
    name: "Lost Heaven Hospital",
    signOn: {x: -759.4, y: 11.6, z: 755.5},
    // Nose to +x at the kerb of the street in front of the steps, on the
    // street (the bay's height): a parked car nobody drives does not settle.
    spots: [
        {position: {x: -752.0, y: 11.55, z: 733.0}, heading: Math.PI / 2},
        {position: {x: -764.0, y: 11.55, z: 733.0}, heading: Math.PI / 2},
    ],
    bay: {x: -759.4, y: 11.5, z: 735.0},
    // The landing in front of the doors (3DVERE4 at z 763.5, floor y 13.2).
    door: {x: -759.4, y: 13.3, z: 762.0},
};
const HOSPITALS = {freeride: CITY_HOSPITAL, freeride_extended: CITY_HOSPITAL, freeridenoc: CITY_HOSPITAL};
const TREAT_REACH = 3;
const TREAT_MS = 5000;
const REVIVE_MS = 4000;
const BAY_RADIUS = 22;
const BAY_SPEED = 3;
const TICK_MS = 250;
const CARD_MS = 1000;
const CALL_EVERY_MS = 3 * 60_000;
const FIRST_CALL_MS = 45_000;
const MAX_CALLS = 3;
const CALL_LIFETIME_MS = 8 * 60_000;
const DOWN_MS = 60_000;
const BOARD_TIMEOUT_MS = 15_000;
const PICKUP_REACH = 20;
// A ride to the hospital pays its base, PER_METRE of the straight distance
// and RIDE_TIP inside the time: a serious call about $300-460, a woman in
// labour $510-700 with the smooth-ride tip, a heart attack $520-690. A
// pile-up's three patients (critical rides in, the serious and the minor
// are patched up where they sit, no quick tip) with the triage pay and the
// bonus come to about $530-700 for one medic.
const PAY = {minor: 140, serious: 220, revive: 200, labour: 330, cardiac: 400, critical: 160, hurt: 100, scraped: 70};
// A pile-up: treating the critical patient first, and once all three are
// seen to with nobody lost, split between the medics who treated them.
const TRIAGE_PAY = 40;
const PILEUP_PAY = 100;
// A player brought back pays the medic once in this long: no farming one
// friend who keeps going down.
const REVIVE_PAY_EVERY_MS = 10 * 60_000;
const PER_METRE = 0.1;
// Treated where they lie, quickly: 40% more. Brought in inside the time: 25%.
const TIP = 0.4;
const RIDE_TIP = 0.25;
const XP = 50;
const CIVILIANS = ["Delnik01.i3d", "Delnik02.i3d", "Czena01.i3d", "Czena02.i3d", "Homeless01.i3d"];
// Stock clips (a4.dta anims\): a panicking walker lying on the ground
// (C_human anim_info 337) and crouched (331).
const LYING = "x panika leh stativ.i3d";
const CROUCHED = "x panika drep stativ.i3d";
const WOMEN = ["Czena01.i3d", "Czena02.i3d"];
const MEN = ["Delnik01.i3d", "Delnik02.i3d", "Homeless01.i3d"];
// Retail voice lines (Npc.say): 85-91 pain, 21-23 finding someone (relief).
const PAIN_LINES = [85, 86, 87, 88, 89, 90, 91];
const RELIEF_LINES = [21, 22, 23];
const MOAN_EVERY_MS = 11_000;
// How long a heart attack and a pile-up's critical patient last untreated;
// longer when the nearest medic is far.
const CARDIAC_MS = 4 * 60_000;
const BLEED_MS = 3.5 * 60_000;
// CPR: this many compressions, each 0.33 to 0.95 s after the last (about
// two a second, with room for the network); a pause of 2.5 s starts over
// without losing what was done, pressing too fast takes one back.
const CPR_NEEDED = 18;
const CPR_MIN_GAP_MS = 330;
const CPR_MAX_GAP_MS = 950;
const CPR_RESTART_MS = 2500;
// A woman in labour: the tip window is shorter, a smooth ride tips more.
const SMOOTH_TIP = 0.2;
const CRASH_SPEED_DROP = 9;
const CRASH_COOLDOWN_MS = 3000;
// The pile-up's wrecks are towed this long after the last patient is seen to.
const WRECK_CLEAR_MS = 30_000;
const TRIAGE = {critical: {label: "CRITICAL", order: 0}, hurt: {label: "SERIOUS", order: 1}, minor: {label: "MINOR", order: 2}};

// Activities (lib/life.js) whose players still wait for a medic.
const JOBS = new Set(["fire", "medic", "taxi", "duty", "courier", "bootleg", "farm"]);

const medics = new Map();
const calls = new Map();
let nextCall = 1;
let nextCallAt = 0;
let timer = null;
let lastCardAt = 0;
let lastMarkers = "";
let lastItems = [];
// Patients who walk off after treatment or into the hospital, removed a
// little later; and the exit tasks after which they walk in.
const strays = new Set();
const arrivals = new Map();
// Player record -> when their revive last paid.
const revivePaid = new Map();
// Pile-ups: id -> {id, position, where, wrecks, callIds, treated, medics, lost, fireId, clearAt}.
const pileups = new Map();
let nextPileup = 1;

function hospital() {
    return HOSPITALS[World.getMission()] ?? null;
}

// The ambulance the medic drove last carries their patients.
function ambulanceOf(medic) {
    return vehicleById(medic.ambulanceId);
}

const station = stations.define({
    id: "medic",
    title: "Ambulance",
    crew: "medics",
    mapLabel: "Hospital",
    color: COLORS.green,
    radar: 0x8fc27a,
    model: AMBULANCE,
    vehicleLabel: "AMBULANCE",
    pitch: "Answer calls around the city: treat people where they lie, drive the badly hurt here quickly for a tip, and bring back players " +
        "who went down. $140 to $460 a patient, about $500 to $700 for a birth, a heart attack or a pile-up. Press G here to sign on.",
    place: hospital,
    available: () => true,
    onDuty: (player) => medics.has(player.id),
    start: (player) => startDuty(player),
    stop: (player) => endDuty(player, "Off duty. The ambulances stay at the hospital."),
    onDrive: (player, vehicle) => {
        const medic = medics.get(player.id);
        // A patient getting in or aboard keeps their ambulance; one still
        // waiting at the roadside goes into whichever the medic brings.
        const state = calls.get(medic?.patient)?.state;
        if (medic && state !== "boarding" && state !== "aboard") medic.ambulanceId = vehicle.id;
    },
});

function place(position) {
    const nearest = maps.places().map((entry) => ({entry, gap: flatDistance(entry.position, position)})).sort((a, b) => a.gap - b.gap)[0];
    return nearest && nearest.gap < 700 ? `near ${nearest.entry.name}` : "in the city";
}

function callLabel(call) {
    if (call.kind === "downed") return `MAN DOWN · ${call.label}`;
    if (call.triage) return TRIAGE[call.triage].label;
    return {serious: "BADLY HURT", labour: "IN LABOUR", cardiac: "HEART ATTACK"}[call.kind] ?? "INJURED";
}

function presentCalls() {
    const open = [...calls.values()].filter((call) => call.state === "waiting");
    // A pile-up has one beacon and blip, on its most urgent patient left.
    const leads = new Set();
    for (const group of pileups.values()) {
        const lead = open.filter((call) => call.pileupId === group.id).sort((a, b) => TRIAGE[a.triage].order - TRIAGE[b.triage].order)[0];
        if (lead) leads.add(lead.id);
    }
    const items = open.map((call) => {
        const grouped = Boolean(call.pileupId);
        return {id: `call${call.id}`, position: call.position, label: callLabel(call), color: call.triage === "critical" ? COLORS.red : COLORS.green,
            beacon: !grouped || leads.has(call.id), range: 900, near: grouped ? 60 : 120,
            map: grouped ? (leads.has(call.id) ? {icon: "diamond", label: "Pile-up", flash: true} : false)
                : {icon: "diamond", label: call.kind === "downed" ? "Man down" : "Patient", flash: call.kind === "downed" || call.kind === "cardiac"}};
    });
    const payload = JSON.stringify(items);
    if (payload === lastMarkers) return;
    lastMarkers = payload;
    lastItems = items;
    markersAll("medic", items);
}

function callMessage(call) {
    switch (call.kind) {
        case "downed": return `${call.label} is down ${call.where}.`;
        case "serious": return `Someone is badly hurt ${call.where}.`;
        case "labour": return `A woman has gone into labour ${call.where}. Get her to the hospital quickly, and gently: every bump costs you the tip.`;
        case "cardiac": return `A man collapsed with a heart attack ${call.where}. He has ${formatMinutes(call.expires - Date.now())}: ` +
            "give him CPR, G tapped steadily beside him.";
        default: return `Someone collapsed ${call.where}.`;
    }
}

// fields: what the kind of call needs (its model, clip, expiry, pile-up);
// quiet: no toast or feed line of its own.
function openCall(kind, position, label, npc = null, playerId = null, killerId = null, {quiet = false, ...fields} = {}) {
    const call = {id: nextCall++, kind, position: {...position}, label, npc, playerId, killerId, state: "waiting", openedAt: Date.now(),
        medicId: null, progress: 0, generation: World.getMissionGeneration(), where: place(position), nextLineAt: Date.now() + 4000, ...fields};
    calls.set(call.id, call);
    if (!quiet) {
        for (const [id] of medics) {
            const medic = playerById(id);
            if (medic) toast(medic, callMessage(call), {title: "Ambulance call", tone: "gold", seconds: kind === "labour" || kind === "cardiac" ? 8 : 6});
        }
        feed("medic", [text("Ambulance", COLORS.green), text(` · ${kind === "downed" ? `${label} needs a medic` : "a call came in"} ${call.where}`)]);
    }
    presentCalls();
    return call;
}

// A pile-up's patient is seen to, lost or given up on: the last of them
// settles the pile-up.
function leaveGroup(call) {
    const group = pileups.get(call.pileupId);
    if (!group || !group.callIds.delete(call.id)) return;
    if (!call.resolved) group.missed++;
    if (!group.callIds.size) finishPileup(group);
}

function closeCall(call, removeNpc = true) {
    calls.delete(call.id);
    leaveGroup(call);
    if (removeNpc && call.npc) {
        const npc = call.npc;
        call.npc = null;
        npc.remove();
    }
    if (call.kind === "downed" && call.state === "waiting") {
        const player = playerById(call.playerId);
        if (player) life.holdRespawn(player, null);
    }
    presentCalls();
}

// Too late: the patient dies where they lie, and is taken away later.
function lose(call) {
    for (const [id] of medics) {
        const medic = playerById(id);
        if (medic) toast(medic, `Too late: the ${call.kind === "cardiac" ? "heart attack" : "crash victim"} ${call.where} did not make it.`,
            {title: "Ambulance", tone: "bad", seconds: 5});
    }
    feed("medic", [text("Ambulance", COLORS.muted), text(` · a patient ${call.where} did not make it`)]);
    const npc = call.npc;
    call.npc = null;
    closeCall(call, false);
    if (npc) walkAway(npc, null, 15_000);
}

// The retail voices of men (26-28) and women (29-31), one per patient.
function voiceFor(model) {
    return (/^czena/i.test(model) ? 29 : 26) + Math.floor(Math.random() * 3);
}

function spawnPatient(model, position) {
    return Npc.spawn(model, position, Math.random() * Math.PI * 2, {behaviour: "neutral", health: 100, voice: voiceFor(model)});
}

// A road graph point 200 to 1000 m from the origin.
function roadSpot(origin) {
    const graph = roads.current();
    let spot = null;
    for (let attempt = 0; attempt < 30 && !spot; attempt++) {
        const candidate = graph.roadPoint();
        const gap = origin ? flatDistance(candidate.position, origin) : 400;
        if (gap >= 200 && gap <= 1000) spot = candidate;
    }
    return spot ?? graph.roadPoint();
}

// How long an urgent patient lasts: the call's own time, or more when the
// nearest medic is far off (about 12 m/s there and two minutes to work).
function timeToLive(position, ms) {
    const gaps = [...medics.keys()].map(playerById).filter(Boolean).map((player) => flatDistance(player.position, position));
    return Math.max(ms, gaps.length ? Math.min(...gaps) / 12 * 1000 + 120_000 : 0);
}

// A street call: someone on the pavement by a road. Now and then a woman in
// labour, a heart attack or a pile-up.
function streetCall(origin, kind = pick(["minor", "minor", "serious", "serious", "labour", "cardiac", "pileup"])) {
    if (typeof Npc === "undefined" || calls.size >= MAX_CALLS) return null;
    if (kind === "pileup") return pileup(origin) ?? streetCall(origin, "serious");
    const spot = roadSpot(origin);
    const side = Math.random() < 0.5 ? -4.5 : 4.5;
    const position = {x: spot.position.x + spot.direction.z * side, y: spot.position.y + 0.2, z: spot.position.z - spot.direction.x * side};
    const model = kind === "labour" ? pick(WOMEN) : kind === "cardiac" ? pick(MEN) : pick(CIVILIANS);
    const npc = spawnPatient(model, position);
    if (!npc) return null;
    if (kind === "labour") return openCall("labour", position, "a woman in labour", npc, null, null, {clip: CROUCHED});
    if (kind === "cardiac") return openCall("cardiac", position, "a heart attack", npc, null, null, {clip: LYING, expires: Date.now() + timeToLive(position, CARDIAC_MS)});
    return openCall(kind === "serious" ? "serious" : "minor", position, "a civilian", npc);
}

// A pile-up: two wrecks across the lane and three people hurt around them,
// on the pavements either side (4.5 to 5 m off the road graph line, as the
// street calls lie).
function pileup(origin) {
    if (calls.size > MAX_CALLS - 1 || [...pileups.values()].some((group) => !group.clearAt)) return null;
    const spot = roadSpot(origin);
    const {x, y, z} = spot.position;
    const {direction} = spot;
    const heading = Math.atan2(direction.x, direction.z);
    const along = (metres, side = 0) => ({x: x + direction.x * metres + direction.z * side, y, z: z + direction.z * metres - direction.x * side});
    const wrecks = [[along(3.5, 1.2), heading + 0.7], [along(-3.0, -1.0), heading + Math.PI - 0.5]].map(([position, rotation]) => {
        const car = Vehicle.spawn(pick(firefighters.WRECKS), {x: position.x, y: position.y + 0.6, z: position.z}, rotation);
        if (car) {
            car.setEngine(false);
            car.state.set("fr.wreck", true);
        }
        return car;
    }).filter(Boolean);
    if (!wrecks.length) return null;
    const center = along(0);
    const group = {id: nextPileup++, position: center, where: place(center), wrecks: wrecks.map((car) => car.id), callIds: new Set(), treated: 0,
        medics: new Set(), missed: 0, fireId: null, clearAt: 0, openedAt: Date.now()};
    pileups.set(group.id, group);
    const side = Math.random() < 0.5 ? -1 : 1;
    const patients = [["critical", along(1.0, 5 * side), LYING], ["hurt", along(-4.0, 5 * side), LYING], ["minor", along(5.5, -5 * side), CROUCHED]];
    for (const [triage, position, clip] of patients) {
        const model = pick(CIVILIANS);
        const npc = spawnPatient(model, {x: position.x, y: position.y + 0.2, z: position.z});
        if (!npc) continue;
        const call = openCall(triage === "critical" ? "serious" : "minor", position, "a crash victim", npc, null, null,
            {quiet: true, clip, triage, pileupId: group.id, ...(triage === "critical" ? {expires: Date.now() + timeToLive(position, BLEED_MS)} : {})});
        group.callIds.add(call.id);
    }
    if (!group.callIds.size) {
        clearWrecks(group);
        return null;
    }
    // With the fire brigade on duty, the first wreck burns beside them.
    if (firefighters.crewSize()) group.fireId = firefighters.startIncident("pileup", null, {position: along(3.5, 1.2), vehicleId: group.wrecks[0]})?.id ?? null;
    for (const [id] of medics) {
        const medic = playerById(id);
        if (medic) toast(medic, `A pile-up ${group.where}: ${group.callIds.size} people hurt around two wrecks. The critical one first, they are ` +
            `bleeding out; the others you patch up where they sit.${group.fireId ? " A wreck is burning: the fire brigade is on its way." : ""}`,
            {title: "Ambulance call", tone: "gold", seconds: 10});
    }
    feed("medic", [text("Ambulance", COLORS.green), text(` · a pile-up ${group.where}, ${group.callIds.size} hurt`)]);
    presentCalls();
    return group;
}

// Every patient of the pile-up seen to: the medics who were there share in
// it if nobody was lost or left. The wrecks go a little later.
function finishPileup(group) {
    group.clearAt = Date.now() + WRECK_CLEAR_MS;
    const team = [...group.medics].map(playerById).filter((player) => player && medics.has(player.id));
    if (group.missed || !team.length) return;
    const cut = Math.max(25, Math.round(PILEUP_PAY / team.length));
    for (const player of team) {
        const medic = medics.get(player.id);
        player.giveMoney(cut);
        medic.earned += cut;
        profiles.addXp(player, 40, "Cleared a pile-up");
        toast(player, `Everyone from the pile-up is seen to. ${formatMoney(cut)} more${team.length > 1 ? " (your share)" : ""}.`, {title: "Ambulance", tone: "good"});
    }
    feed("medic", [...team.flatMap((player, index) => [index ? text(", ") : text(""), name(player, COLORS.green)]),
        text(` cleared the pile-up ${group.where}`)]);
}

// The wrecks are towed once nobody sits in them and nothing burns on them.
function clearWrecks(group) {
    pileups.delete(group.id);
    for (const id of group.wrecks) {
        const wreck = vehicleById(id);
        if (wreck && wreck.getOccupants().every((occupant) => occupant === null)) wreck.destroy();
    }
}

// Walks off and is gone a little later.
function walkAway(npc, position, ms) {
    if (position) npc.walkTo(position);
    strays.add(npc);
    setTimeout(() => {
        strays.delete(npc);
        for (const [task, arrival] of arrivals) if (arrival.npc === npc) arrivals.delete(task);
        npc.remove();
    }, ms);
}

function payMedic(player, medic, amount, reason) {
    player.giveMoney(amount);
    medic.earned += amount;
    medic.patients++;
    profiles.bump(player, "patients");
    profiles.addXp(player, XP, reason);
}

function finishTreatment(player, medic, call) {
    call.medicId = player.id;
    if (call.kind === "downed") {
        const victim = playerById(call.playerId);
        calls.delete(call.id);
        presentCalls();
        if (!victim || victim.alive || !life.revive(victim, call.position, 0, 60)) {
            toast(player, "Too late for them.", {title: "Ambulance", tone: "bad"});
            return;
        }
        const key = profiles.keyOf(victim) ?? `#${victim.id}`;
        const now = Date.now();
        for (const [entry, at] of revivePaid) if (now - at >= REVIVE_PAY_EVERY_MS) revivePaid.delete(entry);
        // No pay for patching up your own victim or the same patient again.
        if (call.killerId === player.id || revivePaid.has(key)) {
            medic.patients++;
            toast(player, `${victim.nickname} is back on their feet. The city pays for that once in a while, not now.`, {title: "Ambulance"});
        } else {
            revivePaid.set(key, now);
            payMedic(player, medic, PAY.revive, "Brought someone back");
            toast(player, `${victim.nickname} is back on their feet. ${formatMoney(PAY.revive)}.`, {title: "Ambulance", tone: "good"});
        }
        toast(victim, `${player.nickname} patched you up.`, {title: "Ambulance", tone: "good"});
        feed("medic", [name(player, COLORS.green), text(" brought "), name(victim), text(" back")]);
        return;
    }
    // The badly hurt are treated for the ride: without an ambulance to take
    // them, they stay lying where they are and nothing is paid yet.
    if (call.kind !== "minor" && (!ambulanceOf(medic) || !hospital())) {
        toast(player, "They need the hospital: fetch an ambulance (two stand in front of the hospital), stop it beside them and treat them again.",
            {title: "Ambulance", tone: "bad", seconds: 7});
        return;
    }
    const npc = call.npc;
    npc?.stopAnimation?.();
    npc?.say?.(pick(RELIEF_LINES));
    triage(player, medic, call);
    // A pile-up's patients are patched up on the spot, no quick tip.
    const quick = !call.pileupId && Date.now() - call.openedAt < 4 * 60_000;
    if (call.kind === "minor") {
        // A pile-up's seriously hurt are splinted where they sit.
        const base = call.triage === "hurt" ? PAY.hurt : call.triage === "minor" ? PAY.scraped : PAY.minor;
        const pay = base + (quick ? Math.round(base * TIP) : 0) + (call.bonus ?? 0);
        payMedic(player, medic, pay, "Treated a patient");
        toast(player, `${call.triage === "hurt" ? "Splinted and bandaged" : "Patched up"}${quick ? " and thanked for the quick arrival" : ""}. ${formatMoney(pay)}.`,
            {title: "Ambulance", tone: "good"});
        call.npc = null;
        call.resolved = true;
        closeCall(call, false);
        if (npc) walkAway(npc, {x: npc.position.x + 15, y: npc.position.y, z: npc.position.z + 15}, 20_000);
        return;
    }
    toHospital(player, medic, call, call.kind === "labour" ? "Her waters broke. " : "Stabilised. ");
}

// The pile-up's first patient seen to: the critical one, as it should be.
function triage(player, medic, call) {
    const group = pileups.get(call.pileupId);
    if (!group) return;
    group.medics.add(player.id);
    if (group.treated++) return;
    if (call.triage === "critical") {
        player.giveMoney(TRIAGE_PAY);
        medic.earned += TRIAGE_PAY;
        toast(player, `The critical one first: good triage. ${formatMoney(TRIAGE_PAY)}.`, {title: "Ambulance", tone: "good", seconds: 4});
    } else if ([...calls.values()].some((other) => other.pileupId === group.id && other.triage === "critical" && other.state === "waiting")) {
        toast(player, "The critical patient is still bleeding: see to them next.", {title: "Ambulance", tone: "bad", seconds: 5});
    }
}

// CPR: G tapped in a steady rhythm beside him until his heart starts.
function compress(player, medic, call) {
    const now = Date.now();
    const cpr = call.cpr ??= {count: 0, lastAt: 0, medicId: null};
    // One pair of hands on his chest: a second medic waits until the first
    // stops, then carries on from the count.
    if (cpr.medicId !== null && cpr.medicId !== player.id && now - cpr.lastAt <= CPR_RESTART_MS) {
        const other = playerById(cpr.medicId);
        toast(player, `${other?.nickname ?? "Another medic"} is doing the CPR. Let them keep the rhythm.`, {title: "Ambulance", seconds: 3});
        return;
    }
    if (cpr.medicId !== player.id) {
        cpr.medicId = player.id;
        cpr.lastAt = 0;
    }
    const gap = now - cpr.lastAt;
    let verdict = "GOOD";
    if (!cpr.lastAt || gap > CPR_RESTART_MS) verdict = "KEEP GOING";
    else if (gap < CPR_MIN_GAP_MS) {
        cpr.count = Math.max(0, cpr.count - 1);
        verdict = "TOO FAST";
    } else if (gap > CPR_MAX_GAP_MS) verdict = "TOO SLOW";
    else cpr.count++;
    cpr.lastAt = now;
    medic.cprAt = now;
    medic.hint = "cpr";
    if (cpr.count < CPR_NEEDED) {
        jobkey.prompt(player, `CPR · ${verdict} · ${cpr.count}/${CPR_NEEDED}`, cpr.count / CPR_NEEDED);
        return;
    }
    medic.cprAt = 0;
    medic.hint = null;
    jobkey.prompt(player, null);
    call.expires = null;
    if (!ambulanceOf(medic) || !hospital()) {
        toast(player, "He is breathing again, but he needs the hospital: fetch an ambulance, stop it beside him and press G.",
            {title: "Ambulance", tone: "gold", seconds: 7});
        return;
    }
    call.medicId = player.id;
    call.npc?.stopAnimation?.();
    call.npc?.say?.(pick(PAIN_LINES));
    toHospital(player, medic, call, "He is breathing again! ");
}

// Badly hurt: to the hospital, in the ambulance.
function toHospital(player, medic, call, opening) {
    const npc = call.npc;
    const ambulance = ambulanceOf(medic);
    const home = hospital();
    if (!ambulance || !home) {
        toast(player, "You need an ambulance to take them in: there are two in front of the hospital.", {tone: "bad"});
        return;
    }
    // They get in once the medic is back at the wheel: a passenger of a car
    // a player drives rides along (NpcService's player-driven seat rule),
    // while an empty car would go to the simulation host.
    call.state = npc ? "pickup" : "aboard";
    medic.patient = call.id;
    const straight = flatDistance(call.position, home.bay);
    call.deadline = Date.now() + (straight / 11 + (call.kind === "labour" ? 60 : 90)) * 1000;
    call.from = {...call.position};
    call.crashes = 0;
    medic.lastSpeed = null;
    presentCalls();
    toast(player, `${opening}Bring the ambulance up and stop beside them, then get them to ${home.name}; ` +
        `${formatMinutes(call.deadline - Date.now())} for a tip${call.kind === "labour" ? ", and drive smoothly" : ""}.`, {title: "Ambulance", tone: "gold"});
}

// A bump on the way in with a woman in labour aboard.
function jolt(player, call) {
    const now = Date.now();
    if (now - (call.joltAt ?? 0) < CRASH_COOLDOWN_MS) return;
    call.joltAt = now;
    call.crashes++;
    call.npc?.say?.(pick(PAIN_LINES));
    toast(player, `${pick(["\"Easy! The baby!\"", "\"Are you trying to deliver it here?\"", "\"Mind the road!\""])} No smooth-ride tip now.`,
        {title: "Ambulance", tone: "bad", seconds: 4});
}

// The patient boards a still ambulance its medic drives, in a free back or
// passenger seat.
function board(player, medic, call) {
    const ambulance = ambulanceOf(medic);
    const npc = call.npc;
    if (!npc) {
        stretcher(call);
        return;
    }
    if (!ambulance || ambulance.getDriver()?.id !== player.id || speedOf(ambulance) > 1 || distance(ambulance.position, npc.position) > PICKUP_REACH) return;
    const seat = [1, 2, 3].find((index) => index < (ambulance.seatCount ?? 4) && !ambulance.getOccupants()[index]) ?? 1;
    call.task = npc.enterVehicle(ambulance, seat);
    if (!call.task) {
        stretcher(call);
        return;
    }
    call.state = "boarding";
    call.boardSince = Date.now();
}

// Carried in on the stretcher: the patient rides out of sight.
function stretcher(call) {
    call.state = "aboard";
    if (call.npc) {
        const npc = call.npc;
        call.npc = null;
        npc.remove();
    }
}

function deliver(player, medic, call) {
    const home = hospital();
    const ambulance = ambulanceOf(medic);
    medic.patient = null;
    calls.delete(call.id);
    call.resolved = true;
    leaveGroup(call);
    const inTime = Date.now() <= call.deadline;
    const base = call.kind === "labour" ? PAY.labour : call.kind === "cardiac" ? PAY.cardiac : call.triage === "critical" ? PAY.critical : PAY.serious;
    const fare = base + Math.round(flatDistance(call.from, home.bay) * PER_METRE);
    const tip = inTime ? Math.round(fare * RIDE_TIP) : 0;
    const smooth = call.kind === "labour" && !call.crashes ? Math.round(fare * SMOOTH_TIP) : 0;
    const bonus = call.bonus ?? 0;
    const pay = fare + tip + smooth + bonus;
    payMedic(player, medic, pay, "Brought a patient in");
    const extras = [tip ? `a ${formatMoney(tip)} tip` : "", smooth ? `${formatMoney(smooth)} for the smooth ride` : "",
        bonus ? `${formatMoney(bonus)} from the fire call` : ""].filter(Boolean);
    const baby = pick(["boy", "girl"]);
    toast(player, `${call.kind === "labour" ? `It's a ${baby}! ` : ""}${inTime ? "Delivered in time" : "Delivered, but slowly"}. ${formatMoney(fare)}` +
        `${extras.length ? ` and ${extras.join(" and ")}` : ""}.`, {title: "Ambulance", tone: "good", seconds: 6});
    feed("medic", [name(player, COLORS.green), text(call.kind === "labour" ? ` rushed a mother to ${home.name}: it's a ${baby}` : ` brought a patient to ${home.name}`)]);
    // The patient walks in through the doors, once out of the car (a walk
    // given earlier would cancel the getting out).
    let npc = call.npc;
    if (npc) {
        const exit = npc.exitVehicle();
        if (exit) arrivals.set(exit, {npc, door: home.door});
        walkAway(npc, exit ? null : home.door, 25_000);
    } else if (typeof Npc !== "undefined" && ambulance) {
        const {x, y, z} = ambulance.position;
        npc = Npc.spawn(pick(CIVILIANS), {x: x + 1.5, y: y + 0.2, z: z + 1.5}, 0, {behaviour: "neutral"});
        if (npc) {
            setTimeout(() => npc.walkTo(home.door), 600);
            walkAway(npc, null, 25_000);
        }
    }
    presentCalls();
}

function endDuty(player, message) {
    const medic = medics.get(player.id);
    if (!medic) return;
    medics.delete(player.id);
    jobkey.release(player);
    jobkey.prompt(player, null);
    life.end(player, "medic");
    profiles.setPublic(player, "fr.job", null);
    compass(player, "medic", 43, null);
    card(player, "medic", null);
    const call = calls.get(medic.patient);
    if (call) closeCall(call);
    for (const open of calls.values()) if (open.medicId === player.id && open.state === "waiting") open.medicId = null;
    // Downed players wait only while someone can come.
    for (const open of [...calls.values()]) {
        if (open.kind === "downed" && ![...medics.keys()].some((id) => id !== open.playerId)) closeCall(open);
    }
    // Off duty nobody drives an ambulance: the player gets out first.
    if (World.getPlayers().some((other) => other.id === player.id) && player.getSeat() === 0 && station.isVehicle(player.getVehicle())) {
        player.removeFromVehicle();
    }
    station.refresh();
    if (message) toast(player, `${message} ${medic.patients} patient${medic.patients === 1 ? "" : "s"}, ${formatMoney(medic.earned)}.`, {title: "Ambulance"});
}

function startDuty(player) {
    if (!playable(player) || player.virtualWorld !== MAIN_WORLD) return;
    const home = hospital();
    if (!home) {
        toast(player, "There is no hospital out here.", {tone: "bad"});
        return;
    }
    if (player.getVehicle() || flatDistance(player.position, home.signOn) > stations.SIGN_ON_RADIUS + 1) {
        // Asked again, the compass stops showing the way.
        if (!station.guide(player)) {
            toast(player, "The compass no longer shows the way.", {title: "Ambulance", seconds: 3});
            return;
        }
        toast(player, `Sign on at ${home.name}: follow the compass to the arrow at the foot of the steps and press G. ` +
            "Hold TAB to see it on the map.", {title: "Ambulance", tone: "gold", seconds: 8});
        return;
    }
    if (!life.begin(player, "medic", "Ambulance duty")) {
        toast(player, life.busyMessage(player), {tone: "bad"});
        return;
    }
    medics.set(player.id, {ambulanceId: null, earned: 0, patients: 0, patient: null, treating: null, hint: null});
    profiles.setPublic(player, "fr.job", "medic");
    feed("medic", [name(player, COLORS.green), text(" went on ambulance duty")]);
    toast(player, "You are on duty. Take one of the ambulances parked in front of the hospital. Calls show on your compass and on the map (TAB). " +
        "Hold G beside a patient to treat them; the badly hurt ride with you to the hospital, quickly for a tip. " +
        "Players who go down now wait a minute for you. Back under the arrow, G clocks you off.",
        {title: "Ambulance", tone: "gold", seconds: 12});
    if (!calls.size) nextCallAt = Math.min(nextCallAt, Date.now() + 20_000);
}

function callNear(player) {
    for (const call of calls.values()) {
        if (call.state !== "waiting") continue;
        const target = call.kind === "downed" ? playerById(call.playerId)?.position ?? call.position : call.npc?.position ?? call.position;
        if (distance(target, player.position) <= TREAT_REACH) return call;
    }
    return null;
}

function nearestCall(position) {
    return [...calls.values()].filter((call) => call.state === "waiting")
        .map((call) => ({call, gap: flatDistance(call.position, position)})).sort((a, b) => a.gap - b.gap)[0] ?? null;
}

function tick() {
    const now = Date.now();
    if (!inFreeRide() || !World.isReady()) return;
    if (medics.size && now >= nextCallAt && hospital()) {
        nextCallAt = now + CALL_EVERY_MS;
        const anchor = pick([...medics.keys()].map(playerById).filter(Boolean));
        streetCall(anchor?.position ?? null);
    }
    for (const call of [...calls.values()]) {
        if (call.generation !== World.getMissionGeneration()) {
            calls.delete(call.id);
            continue;
        }
        if (call.state === "waiting" && call.npc && call.ready && !call.lying) {
            call.lying = true;
            call.npc.playAnimation?.(call.clip ?? (call.kind === "minor" ? CROUCHED : LYING), {loop: true});
        }
        if (call.kind === "downed") {
            const victim = playerById(call.playerId);
            if (!victim || victim.alive || now - call.openedAt > DOWN_MS) {
                if (victim && !victim.alive) toast(victim, "No medic made it in time.", {title: "Ambulance"});
                closeCall(call);
            }
            continue;
        }
        if (call.state === "boarding" && now - call.boardSince > BOARD_TIMEOUT_MS) stretcher(call);
        // A heart attack or a pile-up's critical patient left too long.
        if (call.state === "waiting" && call.expires && now > call.expires) {
            lose(call);
            continue;
        }
        if (call.state === "waiting" && now - call.openedAt > CALL_LIFETIME_MS) {
            closeCall(call);
            continue;
        }
        // They moan while they wait (the heart attack is out cold); a woman
        // in labour more often, in the ambulance too.
        const labour = call.kind === "labour" && (call.state === "aboard" || call.state === "boarding" || call.state === "pickup");
        if (call.npc && call.ready && call.kind !== "cardiac" && (call.state === "waiting" || labour) && now >= call.nextLineAt) {
            call.nextLineAt = now + MOAN_EVERY_MS * (call.kind === "labour" ? 0.5 : 1) * (0.7 + Math.random() * 0.6);
            call.npc.say?.(pick(PAIN_LINES));
        }
    }
    // A pile-up's wrecks go once its patients are seen to and its fire is out.
    for (const group of [...pileups.values()]) {
        const burning = group.fireId && firefighters.incidents().some((incident) => incident.id === group.fireId);
        if (group.clearAt && now >= group.clearAt && !burning) clearWrecks(group);
    }
    for (const [id, medic] of medics) {
        const player = playerById(id);
        if (!player) {
            medics.delete(id);
            continue;
        }
        // Another module took the player off the job: an arrest and the jail.
        if (life.activity(player)?.kind !== "medic") {
            endDuty(player, "Your shift is over.");
            continue;
        }
        if (!playable(player)) continue;
        const call = calls.get(medic.patient);
        const home = hospital();
        const ambulance = ambulanceOf(medic);
        if (call && call.state === "pickup") {
            compass(player, "medic", 43, {position: call.npc?.position ?? call.position});
            board(player, medic, call);
        } else if (call && (call.state === "aboard" || call.state === "boarding")) {
            compass(player, "medic", 43, {position: home.bay});
            // Every bump with a woman in labour aboard costs the smooth tip.
            if (call.kind === "labour" && ambulance && player.getVehicle()?.id === ambulance.id) {
                const speed = speedOf(ambulance);
                if ((medic.lastSpeed ?? speed) - speed >= CRASH_SPEED_DROP) jolt(player, call);
                medic.lastSpeed = speed;
            }
            if (!ambulance) {
                toast(player, "The ambulance is gone, and the patient with it.", {tone: "bad"});
                medic.patient = null;
                closeCall(call);
            } else if (call.state === "aboard" && player.getVehicle()?.id === ambulance.id && flatDistance(ambulance.position, home.bay) <= BAY_RADIUS &&
                       speedOf(ambulance) < BAY_SPEED) {
                deliver(player, medic, call);
            }
        } else {
            const nearest = nearestCall(player.position);
            compass(player, "medic", 43, nearest ? {position: nearest.call.position} : null);
        }
        // Treating takes a few seconds of G beside the patient.
        if (medic.treating) {
            const treating = calls.get(medic.treating.callId);
            const reach = treating && callNear(player)?.id === treating.id;
            if (!treating || !reach || !jobkey.isHeld(player) || player.getVehicle()) {
                medic.treating = null;
                jobkey.prompt(player, null);
                medic.hint = null;
            } else {
                const needed = treating.kind === "downed" ? REVIVE_MS : TREAT_MS;
                const progress = Math.min(1, (now - medic.treating.startedAt) / needed);
                jobkey.prompt(player, treating.kind === "downed" ? "REVIVING…" : "TREATING…", progress);
                medic.hint = "busy";
                if (progress >= 1) {
                    medic.treating = null;
                    jobkey.prompt(player, null);
                    medic.hint = null;
                    finishTreatment(player, medic, treating);
                }
            }
            continue;
        }
        // CPR keeps its own prompt while the medic keeps tapping.
        if (medic.cprAt && now - medic.cprAt < CPR_RESTART_MS) continue;
        medic.cprAt = 0;
        const near = !player.getVehicle() && callNear(player);
        const hint = near ? (near.kind === "downed" ? "[G] HOLD TO REVIVE" : near.kind === "cardiac" ? "[G] TAP STEADILY FOR CPR" : "[G] HOLD TO TREAT") : null;
        if (hint !== medic.hint) {
            medic.hint = hint;
            jobkey.prompt(player, hint);
        }
    }
    if (now - lastCardAt >= CARD_MS) {
        lastCardAt = now;
        presentCalls();
        for (const [id, medic] of medics) {
            const player = playerById(id);
            if (!player) continue;
            const call = calls.get(medic.patient);
            const nearest = nearestCall(player.position);
            const rows = [];
            if (call) {
                rows.push(["Patient", call.state === "pickup" ? "waiting for the ambulance" : call.state === "boarding" ? "getting in" : "aboard"]);
                if (call.deadline) rows.push(["Tip window", call.deadline > now ? formatMinutes(call.deadline - now) : "missed"]);
                if (call.kind === "labour") rows.push(["Ride", call.crashes ? `${call.crashes} bump${call.crashes === 1 ? "" : "s"}` : "smooth so far"]);
                rows.push(["Hospital", `${Math.round(flatDistance(player.position, hospital().bay))} m`]);
            } else if (nearest) {
                const open = nearest.call;
                rows.push([open.triage ? `Pile-up · ${TRIAGE[open.triage].label.toLowerCase()}` : {downed: "Man down", serious: "Badly hurt", labour: "In labour",
                    cardiac: "Heart attack"}[open.kind] ?? "Collapsed", `${Math.round(nearest.gap)} m`]);
                if (open.expires) rows.push([open.kind === "cardiac" ? "His time" : "Bleeding out in", formatMinutes(Math.max(0, open.expires - now))]);
                if (open.cpr?.count) rows.push(["CPR", `${open.cpr.count}/${CPR_NEEDED}`]);
            }
            rows.push(["Open calls", `${[...calls.values()].filter((entry) => entry.state === "waiting").length}`]);
            rows.push(["This shift", `${medic.patients} · ${formatMoney(medic.earned)}`]);
            const subtitle = call ? (call.kind === "labour" ? "To the hospital, quickly and gently" : "Get the patient to the hospital")
                : !ambulanceOf(medic) ? "Take an ambulance from the hospital"
                : nearest?.call.kind === "cardiac" ? "CPR: tap G steadily beside him"
                : nearest?.call.pileupId ? "Pile-up: the critical one first"
                : nearest ? `A call ${nearest.call.where}` : "No calls. Wait by the hospital.";
            card(player, "medic", {title: "Ambulance", subtitle,
                rows, tone: "good"});
        }
    }
}

jobkey.register({
    press(player) {
        // A downed player gives up waiting.
        if (!player.alive) {
            const call = [...calls.values()].find((entry) => entry.kind === "downed" && entry.playerId === player.id);
            if (!call) return false;
            closeCall(call);
            toast(player, "You stop waiting for a medic.", {title: "Ambulance"});
            return true;
        }
        const medic = medics.get(player.id);
        if (!medic || !playable(player) || player.getVehicle()) return false;
        const call = callNear(player);
        if (!call) return false;
        if (call.kind === "cardiac") compress(player, medic, call);
        else medic.treating = {callId: call.id, startedAt: Date.now()};
        return true;
    },
    release(player) {
        const medic = medics.get(player.id);
        if (medic) medic.treating = null;
    },
});

Events.on("npcSpawn", (npc) => {
    for (const call of calls.values()) if (call.npc && call.npc.id === npc.id) call.ready = true;
});

Events.on("npcTaskComplete", (npc, info) => {
    const arrival = arrivals.get(info.id);
    if (arrival && arrival.npc.id === npc.id) {
        arrivals.delete(info.id);
        npc.walkTo(arrival.door);
        return;
    }
    for (const call of calls.values()) {
        if (!call.npc || call.npc.id !== npc.id || call.state !== "boarding" || info.id !== call.task) continue;
        if (info.success) call.state = "aboard";
        else stretcher(call);
    }
});

// Bumps with a woman in labour aboard.
Events.on("vehicleDamage", (vehicle) => {
    for (const [id, medic] of medics) {
        const call = calls.get(medic.patient);
        if (call?.kind !== "labour" || (call.state !== "aboard" && call.state !== "boarding") || ambulanceOf(medic)?.id !== vehicle?.id) continue;
        const player = playerById(id);
        if (player && player.getVehicle()?.id === vehicle.id) jolt(player, call);
    }
});

Events.on("npcDeath", (npc) => {
    for (const call of [...calls.values()]) {
        if (!call.npc || call.npc.id !== npc.id) continue;
        const medic = medics.get(call.medicId);
        if (medic && medic.patient === call.id) medic.patient = null;
        feed("medic", [text("Ambulance", COLORS.muted), text(` · a patient ${call.where} did not make it`)]);
        closeCall(call, false);
    }
});

// A patient the host could not make or lost: a street call is off, one
// already in the medic's hands rides in on the stretcher.
Events.on("npcRetire", (npc) => {
    for (const call of [...calls.values()]) {
        if (!call.npc || call.npc.id !== npc.id) continue;
        call.npc = null;
        if (call.state === "waiting") closeCall(call, false);
        else call.state = "aboard";
    }
    for (const stray of strays) if (stray.id === npc.id) strays.delete(stray);
});

// A player who goes down while a medic is on duty waits for one.
Events.on("playerDeath", (player, killer, info) => {
    // Not in a duel, race or other contest; a job is fine. Not after /kill,
    // which asks for the respawn, and not for the only medic.
    const busy = life.activity(player);
    if (!hospital() || !inFreeRide() || player.virtualWorld !== MAIN_WORLD || (busy && !JOBS.has(busy.kind)) || info?.cause === "script") return;
    if (![...medics.keys()].some((id) => id !== player.id)) return;
    // Rooms built apart under the fields (the back-room bar, the jail): the
    // interior has already put the player back in the street world, but no
    // ambulance reaches them there.
    if (barRoom.inRoom(player.position, 1) || prison.inHall(player.position, 2)) return;
    if ([...calls.values()].some((call) => call.kind === "downed" && call.playerId === player.id)) return;
    life.holdRespawn(player, Date.now() + DOWN_MS);
    openCall("downed", player.position, player.nickname, null, player.id, killer instanceof Player ? killer.id : null);
    toast(player, "A medic has been called. Hold on for a minute, or press G to wake up at the hospital.", {title: "Ambulance", tone: "gold", seconds: 8});
});

// Smoke victims the fire brigade led out (firefighters.js).
firefighters.setAmbulance({
    // options.bonus: a share of a big fire call's bonus, paid with the fare.
    takePatient(npc, position, label, options = {}) {
        if (!medics.size || !hospital() || calls.size >= MAX_CALLS + 2) return false;
        npc.stop?.();
        // Coughing on the kerb, not lying in the road.
        const call = openCall("serious", npc.position ?? position, label, npc, null, null, {clip: CROUCHED, bonus: options.bonus ?? 0});
        call.ready = true;
        return true;
    },
});

// Signing on happens under the hospital's arrow; the command shows the way there
// and clocks off from anywhere.
commands.register({name: "medic", aliases: ["ambulance", "paramedic"], usage: "/medic", help: "Show the way to the hospital's sign-on point, or clock off.", category: "City"}, (player) => {
    if (medics.has(player.id)) endDuty(player, "Off duty.");
    else startDuty(player);
});

menu.provide("medic", (player) => ({onDuty: medics.has(player.id), medics: medics.size, available: Boolean(hospital()),
    calls: [...calls.values()].filter((call) => call.state === "waiting").length, earned: medics.get(player.id)?.earned ?? 0}));

onStart(() => {
    nextCallAt = Date.now() + FIRST_CALL_MS;
    timer = setInterval(tick, TICK_MS);
});
onStop(() => {
    clearInterval(timer);
    for (const id of [...medics.keys()]) {
        const player = playerById(id);
        if (player) endDuty(player, null);
    }
    for (const call of [...calls.values()]) closeCall(call);
    for (const group of [...pileups.values()]) clearWrecks(group);
    for (const npc of strays) npc.remove();
    strays.clear();
    arrivals.clear();
});
Events.on("playerSpawn", (player) => {
    // The calls open before this player came: markersAll sends only changes.
    if (lastItems.length) markers(player, "medic", lastItems);
});
Events.on("playerDisconnect", (player) => {
    for (const call of [...calls.values()]) if (call.kind === "downed" && call.playerId === player.id) calls.delete(call.id);
    const medic = medics.get(player.id);
    if (!medic) return;
    medics.delete(player.id);
    const call = calls.get(medic.patient);
    if (call) closeCall(call);
    for (const open of [...calls.values()]) {
        if (open.kind === "downed" && ![...medics.keys()].some((id) => id !== open.playerId)) closeCall(open);
    }
});
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) endDuty(player, "Your shift ended with the city reloading.");
    calls.clear();
    pileups.clear();
    strays.clear();
    arrivals.clear();
    lastMarkers = "";
    lastItems = [];
    nextCallAt = Date.now() + FIRST_CALL_MS;
});

module.exports = {onDuty: (player) => medics.has(player.id), calls: () => [...calls.values()], streetCall,
    // The next street call comes in ms (dev tools, tests).
    schedule(ms) { nextCallAt = Date.now() + ms; }};
