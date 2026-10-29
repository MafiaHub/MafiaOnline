// Bars: rooms with a barman behind the counter, drinkers at the tables and
// a drink menu at the counter (N/B, like the shop counters).
//
// Lost Heaven Extended has the real thing. The mod places Salieri's bar of
// the story missions (models/salierybar.4ds) where the stock city has only
// its front, with working doors: walk in through the front door or the side
// door off the alley. It also places Morello's bar (models/morellobar.4ds),
// whose door is a fixed mesh: an arrow on the pavement leads in
// (lib/maps.js). Stock Lost Heaven has no rooms behind Salieri's door, so
// its arrow leads to a back-room bar built from stock models
// (lib/bar_room.js).
//
// The people are scripted NPCs (Npc.spawn) in the bar's world while anyone
// is inside, and leave 20 seconds after the last guest. Their places, models
// and clips are the story missions' own: Luigi stands behind Salieri's
// counter (MISE02-ULICKA, MISE10-SALIERY), Salieri's men sit in the booths
// (MISE02-ULICKA: "Mise zidle01st", "ZidleSed03", "ZidleSed04"), a sailor
// waits in the billiard room (MISE16-SALIERY), and Morello's barman cleans
// up between Morello's men (MISE03-MORELLO: "x barman stativ", "x barman
// stul", "x barman umyvani"). The clips play with npc.playAnimation as the
// retail scripts' PERSON_PLAYANIM does.
const life = require("./lib/life.js");
const maps = require("./lib/maps.js");
const barRoom = require("./lib/bar_room.js");
const commands = require("./lib/commands.js");
const interiors = require("./interiors.js");
const drunk = require("./drunk.js");
const {toast, compass, markersAll, markers, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, flatDistance, emit, onClient, onStart, onStop, formatMoney} = require("./lib/util.js");

const TICK_MS = 500;
const STAND_DOWN_MS = 20_000;
const RESPAWN_MS = 60_000;
const PURCHASE_COOLDOWN_MS = 2500;
const SERVE_MS = 3000;
const CHEERS_MS = 4000;
const ROUND_PER_HEAD = 4;
const LABEL_RANGE = 40;
// A lone looping clip is given again this often, in case the host's AI
// turned the drinker out of it.
const REFRESH_MS = 30_000;

// Prices for a city where a day's odd jobs pay $30 an hour.
const DRINKS = Object.freeze([
    {id: "beer", name: "Beer", price: 4, strength: 10, health: 2, note: "Cold, from the back room. +2 health"},
    {id: "wine", name: "Red wine", price: 6, strength: 14, health: 3, note: "Chianti, for the old country. +3 health"},
    {id: "gin", name: "Bathtub gin", price: 7, strength: 18, health: 3, note: "Nobody asks where it was made. +3 health"},
    {id: "whiskey", name: "Canadian whiskey", price: 10, strength: 22, health: 5, note: "Came over the lake last night. +5 health"},
    {id: "grappa", name: "The house grappa", price: 15, strength: 32, health: 6, note: "Strong enough to strip paint. +6 health"},
    {id: "coffee", name: "Black coffee", price: 3, strength: -15, health: 4, note: "Sobers you up a little. +4 health"},
    {id: "round", name: "A round for the house", price: null, strength: 10, health: 2, note: `A beer for everyone in the bar · $${ROUND_PER_HEAD} a head`},
]);

const BARMAN = Object.freeze([["x barman stativ.i3d", 7000], ["x barman stul.i3d", 5000], ["x barman stativ.i3d", 7000], ["x barman umyvani.i3d", 6000]]);
const SERVING = "x barman stul.i3d";
const GLASS = Object.freeze([["GestSklStativ.i3d", 8000], ["GestSklNapitise.i3d", 4000], ["GestSklStativ.i3d", 6000], ["GestSkl01.i3d", 4000]]);
const CHEERS = "GestSklPripiti.i3d";

function box(x0, x1, z0, z1, y0, y1) {
    return {x0, x1, z0, z1, y0, y1};
}

function inBoxes(boxes, position) {
    return boxes.some((b) => position.x >= b.x0 && position.x <= b.x1 && position.z >= b.z0 && position.z <= b.z1 &&
        position.y >= b.y0 && position.y <= b.y1);
}

// Positions are the bar's own frames in the story missions; headings are
// the frames' rotations (the engine turns by the conjugate quaternion).
const SALIERI = {
    id: "salieri", name: "Salieri's Bar", tagline: "Family business. Keep your voice down", world: MAIN_WORLD,
    // bar01, bar02, the billiard room and the kitchen corridor.
    rooms: [box(-1782.6, -1772.7, 5.5, 11.2, -6.5, -2.0), box(-1782.6, -1777.9, 11.1, 15.6, -6.5, -2.0),
        box(-1788.3, -1777.1, 15.7, 22.3, -6.5, -2.0), box(-1774.6, -1772.6, 11.2, 17.8, -6.5, -2.0)],
    // Customers stand between the west stools (@Rbstol01-02, z 8.75), out of
    // reach of the Salieri's Back Window counter at the front door
    // (server/shops.js, 3.2 m around "Salieri_save").
    counter: {position: {x: -1776.8, y: -5.5, z: 8.5}, reach: 1.0},
    // The label hangs over the west shop window, clear of the Back Window's
    // own over the front door.
    door: {x: -1778.6, y: -3.0, z: 4.9},
    wake: {position: {x: -1776.3, y: -5.5, z: 3.6}, heading: Math.PI},
    bartender: {model: "Luigi.i3d", position: {x: -1776.1, y: -5.44, z: 10.26}, heading: -3.066, routine: BARMAN},
    patrons: [
        {model: "SalMan01K.i3d", position: {x: -1776.48, y: -5.51, z: 6.63}, heading: -1.565, routine: [["Mise zidle01st.i3d", 0]]},
        {model: "Gangster01.i3d", position: {x: -1778.32, y: -5.46, z: 6.67}, heading: 1.564, routine: [["ZidleSed03.i3d", 0]]},
        {model: "Enemy06.i3d", position: {x: -1779.78, y: -5.54, z: 6.8}, heading: -1.527, routine: [["ZidleSed04.i3d", 0]]},
        {model: "Czena04.i3d", position: {x: -1778.45, y: -5.5, z: 9.6}, heading: Math.PI / 2, routine: GLASS, glass: true},
        {model: "sailor03.i3d", position: {x: -1778.47, y: -5.56, z: 19.44}, heading: -1.578, routine: [["GestZamStativ.i3d", 0]]},
    ],
};

const MORELLO = {
    id: "morello", name: "Morello's Bar", tagline: "Wrong side of the street", world: MAIN_WORLD,
    rooms: [box(-415.7, -406.0, 451.6, 461.2, 16.0, 20.5)],
    // The four stools in front of the counter (z 458.1).
    counter: {position: {x: -412.6, y: 17.35, z: 457.6}, reach: 1.9},
    door: null,
    wake: {position: {x: -411.5, y: 17.45, z: 449.6}, heading: Math.PI},
    bartender: {model: "civil33.i3d", position: {x: -410.61, y: 17.34, z: 460.1}, heading: 2.23, routine: BARMAN},
    patrons: [
        {model: "Enemy04K.i3d", position: {x: -413.63, y: 17.33, z: 455.39}, heading: 1.508, routine: [["GestikulaceNew01.i3d", 0]]},
        {model: "Enemy12K.i3d", position: {x: -412.48, y: 17.39, z: 455.37}, heading: -1.577, routine: [["GestikulaceNew02.i3d", 0]]},
        {model: "Enemy08K.i3d", position: {x: -409.16, y: 17.46, z: 458.67}, heading: -0.83, routine: GLASS, glass: true},
        {model: "Enemy10K.i3d", position: {x: -409.86, y: 17.37, z: 454.92}, heading: 3.028, routine: [["KoureniAutoStativ.i3d", 0]]},
    ],
};

function backRoom() {
    const [counterEnd, talkA, talkB, seatA, seatB] = barRoom.patrons();
    return {
        id: "backroom", name: "Salieri's Bar", tagline: "The back room. Knock twice", world: barRoom.WORLD, built: true,
        rooms: null, counter: barRoom.order(), door: null,
        wake: maps.MAPS.freeride.interiors[0].outside,
        bartender: {model: "Luigi.i3d", ...barRoom.bartender(), routine: BARMAN},
        patrons: [
            {model: "Czena04.i3d", ...counterEnd, routine: GLASS, glass: true},
            {model: "Enemy04K.i3d", ...talkA, routine: [["GestikulaceNew01.i3d", 0]]},
            {model: "Enemy12K.i3d", ...talkB, routine: [["GestikulaceNew02.i3d", 0]]},
            {model: "SalMan01K.i3d", ...seatA, routine: [["Mise zidle01st.i3d", 0]]},
            {model: "Gangster01.i3d", ...seatB, routine: [["ZidleSed03.i3d", 0]]},
        ],
    };
}

function barsHere() {
    const map = maps.current();
    if (map?.id === "freeride_extended") return [SALIERI, MORELLO];
    if (map?.id === "freeride") return [backRoom()];
    return [];
}

let bars = [];
// barId -> {staff: [{spec, npc, ready, step, until, servingUntil, respawnAt}], occupiedSince, emptySince}
const venues = new Map();
const lastPurchase = new Map();
const lastBar = new Map();
// Players whose compass /bar pointed at a bar.
const pointed = new Set();
// Bars the fire brigade is working in (firefighters.js): no staff, no drinks.
const closed = new Set();
let timer = null;

function canSpawn() {
    return typeof Npc !== "undefined" && typeof Npc.spawn === "function";
}

function contains(bar, position) {
    return bar.rooms ? inBoxes(bar.rooms, position) : barRoom.inRoom(position, 0.5);
}

function barOf(player) {
    if (!player?.spawned || !player.alive || player.missionGeneration !== World.getMissionGeneration()) return null;
    return bars.find((bar) => player.virtualWorld === bar.world && contains(bar, player.position)) ?? null;
}

function guests(bar) {
    return World.getPlayers().filter((player) => barOf(player) === bar);
}

function venue(bar) {
    if (!venues.has(bar.id)) {
        venues.set(bar.id, {staff: [bar.bartender, ...bar.patrons].map((spec, index) => ({spec, bartender: index === 0, npc: null, ready: false,
            step: 0, until: 0, servingUntil: 0, respawnAt: 0})), emptySince: 0});
    }
    return venues.get(bar.id);
}

function spawnMember(bar, member) {
    const {model, position, heading} = member.spec;
    member.npc = Npc.spawn(model, position, heading, {behaviour: "neutral", virtualWorld: bar.world}) ?? null;
    member.ready = false;
    member.step = 0;
    member.until = 0;
    if (!member.npc) member.respawnAt = Date.now() + RESPAWN_MS;
}

function play(member, clip) {
    if (!member.npc || !member.ready || typeof member.npc.playAnimation !== "function") return;
    member.npc.playAnimation(clip, {loop: true});
}

// The clips loop; a routine of several moves on to the next after its time,
// a routine of one is refreshed.
function animate(member, now) {
    if (!member.ready || now < member.until || now < member.servingUntil) return;
    const routine = member.spec.routine;
    const [clip, ms] = routine[member.step % routine.length];
    member.step++;
    play(member, clip);
    member.until = now + (routine.length > 1 ? ms : REFRESH_MS);
}

function staffUp(bar) {
    if (!canSpawn() || (bar.built && !barRoom.isBuilt())) return;
    const now = Date.now();
    for (const member of venue(bar).staff) if (!member.npc && now >= member.respawnAt) spawnMember(bar, member);
}

function standDown(bar) {
    const place = venues.get(bar.id);
    if (!place) return;
    for (const member of place.staff) {
        member.npc?.remove();
        member.npc = null;
        member.ready = false;
        member.respawnAt = 0;
    }
}

function tick() {
    const now = Date.now();
    for (const bar of bars) {
        const place = venue(bar);
        // A burning bar's people are the fire brigade's to get out.
        if (guests(bar).length && !closed.has(bar.id)) {
            place.emptySince = 0;
            staffUp(bar);
        } else if (place.staff.some((member) => member.npc)) {
            if (!place.emptySince) place.emptySince = now;
            else if (now - place.emptySince >= STAND_DOWN_MS) standDown(bar);
        }
        for (const member of place.staff) animate(member, now);
    }
    for (const player of World.getPlayers()) {
        const bar = barOf(player);
        if (!bar) continue;
        lastBar.set(player.id, bar.id);
        if (pointed.delete(player.id)) compass(player, "bar", 45, null);
    }
}

function findMember(npc) {
    for (const [barId, place] of venues) {
        const member = place.staff.find((entry) => entry.npc && entry.npc.id === npc.id);
        if (member) return {barId, member};
    }
    return null;
}

function serve(bar, player, clip = SERVING) {
    const member = venue(bar).staff[0];
    if (!member.npc || !member.ready) return;
    member.npc.face(player.position);
    play(member, clip);
    member.servingUntil = Date.now() + SERVE_MS;
    member.until = 0;
    // Back to his post once the glass is down.
    setTimeout(() => {
        if (member.npc && member.ready) member.npc.face(member.spec.heading);
    }, SERVE_MS);
}

function cheers(bar) {
    const now = Date.now();
    for (const member of venue(bar).staff) {
        if (!member.spec.glass || !member.npc || !member.ready) continue;
        play(member, CHEERS);
        member.servingUntil = now + CHEERS_MS;
        member.until = 0;
    }
}

function publicBar(bar) {
    return {id: bar.id, name: bar.name, tagline: bar.tagline, world: bar.world, counter: bar.counter,
        drinks: DRINKS.map(({id, name, price, note}) => ({id, name, price, note}))};
}

function sendState(player) {
    emit(player, "fr:bars:state", {generation: World.getMissionGeneration(), bars: bars.map(publicBar)});
}

function publishDoors(player = null) {
    const items = bars.filter((bar) => bar.door).map((bar) => ({id: `bar-${bar.id}`, position: bar.door, label: bar.name.toUpperCase(),
        color: COLORS.gold, range: LABEL_RANGE}));
    if (player) markers(player, "bars", items);
    else markersAll("bars", items);
}

// A round someone else buys does not black a guest out (and cost them cash).
function drink(player, entry, treated = false) {
    const health = Math.min(100, player.health + entry.health);
    if (health > player.health) player.setHealth(health);
    return drunk.add(player, entry.strength, treated ? drunk.MAX_LEVEL - 1 : drunk.MAX_LEVEL);
}

function buy(player, request) {
    if (!World.isReady() || request?.generation !== World.getMissionGeneration() || drunk.isBlackedOut(player)) return;
    const bar = barOf(player);
    const entry = DRINKS.find((candidate) => candidate.id === request?.drinkId);
    if (!bar || bar.id !== request?.barId || !entry || player.getVehicle() ||
        flatDistance(player.position, bar.counter.position) > bar.counter.reach || Math.abs(player.position.y - bar.counter.position.y) > 1.5) return;
    const now = Date.now();
    if (now - (lastPurchase.get(player.id) ?? 0) < PURCHASE_COOLDOWN_MS) return;
    lastPurchase.set(player.id, now);
    if (closed.has(bar.id)) {
        toast(player, "The bar is on fire. Get out!", {tone: "bad"});
        return;
    }
    const barman = venue(bar).staff[0];
    if (canSpawn() && !(barman.npc && barman.ready)) {
        toast(player, "Nobody is behind the counter right now.", {tone: "bad"});
        return;
    }
    const company = guests(bar);
    const price = entry.id === "round" ? ROUND_PER_HEAD * company.length : entry.price;
    if (!player.trySpendMoney(price)) {
        toast(player, `That is ${formatMoney(price)}; you have ${formatMoney(player.money)}.`, {tone: "bad"});
        return;
    }
    serve(bar, player);
    if (entry.id === "round") {
        for (const guest of company) {
            drink(guest, entry, guest !== player);
            toast(guest, guest === player ? `A beer for everyone, ${formatMoney(price)}. The room drinks to you.`
                : `${player.nickname} buys a round. Cheers!`, {title: bar.name, tone: "gold"});
        }
        cheers(bar);
        return;
    }
    const level = drink(player, entry);
    toast(player, `${entry.name}, ${formatMoney(price)}. ${entry.strength < 0 ? "Your head clears a little." : drunk.stageOf(level) + "."}`,
        {title: bar.name, tone: "good"});
}

// After a blackout: the pavement outside the bar they drank in.
drunk.setWakeProvider((player) => {
    const bar = barOf(player) ?? bars.find((candidate) => candidate.id === lastBar.get(player.id));
    if (!bar) return null;
    interiors.release(player);
    return bar.wake;
});

function nearestDoor(player) {
    let best = null;
    for (const bar of bars) {
        const interior = maps.current()?.interiors?.find((entry) => entry.id === `b:${bar.id === "backroom" ? "salieri-bar" : bar.id}`);
        const position = bar.door ?? interior?.outside.position;
        if (!position) continue;
        const gap = distance(player.position, position);
        if (!best || gap < best.gap) best = {bar, position, gap};
    }
    return best;
}

commands.register({name: "bar", usage: "/bar", help: "Point the compass at the nearest bar.", category: "City"}, (player) => {
    const nearest = nearestDoor(player);
    if (!nearest) {
        toast(player, "There is no bar on this map.", {tone: "bad"});
        return;
    }
    compass(player, "bar", 45, {position: nearest.position});
    pointed.add(player.id);
    toast(player, `${nearest.bar.name} is ${Math.round(nearest.gap)} m away. At the counter, N picks a drink and B buys it.`);
});

function reset() {
    bars = barsHere();
    venues.clear();
    lastBar.clear();
    pointed.clear();
    closed.clear();
}

// A fire in the bar (firefighters.js): the staff go at once and come back
// with the next guest after it reopens.
function setClosed(barId, shut) {
    if (!shut) {
        closed.delete(barId);
        return;
    }
    closed.add(barId);
    const bar = bars.find((candidate) => candidate.id === barId);
    if (bar) standDown(bar);
}

Events.on("npcSpawn", (npc) => {
    const found = findMember(npc);
    if (!found) return;
    found.member.ready = true;
    npc.face(found.member.spec.heading);
    found.member.until = 0;
    animate(found.member, Date.now());
});
const lost = (npc) => {
    const found = findMember(npc);
    if (!found) return;
    found.member.npc = null;
    found.member.ready = false;
    found.member.respawnAt = Date.now() + RESPAWN_MS;
};
Events.on("npcRetire", lost);
Events.on("npcDeath", lost);

onStart(() => {
    reset();
    if (bars.some((bar) => bar.built)) barRoom.build();
    publishDoors();
    for (const player of World.getPlayers()) sendState(player);
    timer = setInterval(tick, TICK_MS);
});
onStop(() => {
    clearInterval(timer);
    for (const bar of bars) standDown(bar);
    barRoom.teardown();
    markersAll("bars", []);
});
// The mission change removed every NPC and map object.
Events.on("missionChange", () => {
    barRoom.forget();
    reset();
    if (bars.some((bar) => bar.built)) barRoom.build();
    publishDoors();
});
Events.on("playerSpawn", (player) => {
    if (life.isTransit(player)) return;
    sendState(player);
    publishDoors(player);
});
Events.on("playerRespawn", sendState);
Events.on("playerDisconnect", (player) => {
    lastPurchase.delete(player.id);
    lastBar.delete(player.id);
    pointed.delete(player.id);
});
onClient("fr:bar:buy", buy);

module.exports = {DRINKS, ROUND_PER_HEAD, barOf, bars: () => bars, setClosed, isClosed: (barId) => closed.has(barId)};
