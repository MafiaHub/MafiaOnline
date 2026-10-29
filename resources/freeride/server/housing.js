// Homes. Every map's real residential buildings (data/houses.js) can be
// bought: tenement rooms, apartments and, on the hills, villas. A small
// arrow at the door says what is for sale and for how much; G buys it (press
// twice), G at your own door goes in. Inside, each home is its own virtual
// world over a shared room (lib/house_rooms.js), so a hundred homes need one
// set of rooms. The owner locks it, opens it to their crew, invites friends,
// furnishes it from a catalogue (lib/house_decor.js) with a small editor,
// and, at a flat or a house, parks a personal car at the kerb outside: it
// keeps its condition and is there after a restart (server/dealership.js).
// Selling a home back returns 60% of its price; there is no running cost.
const store = require("./lib/store.js");
const profiles = require("./lib/profiles.js");
const life = require("./lib/life.js");
const maps = require("./lib/maps.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const jobkey = require("./lib/jobkey.js");
const rooms = require("./lib/house_rooms.js");
const decor = require("./lib/house_decor.js");
const dealership = require("./dealership.js");
const HOUSES = require("./data/houses.js");
const {toast, feed, name, text, markers, compass, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, distance, flatDistance, playable, playerById, findPlayer, formatMoney, headingOf, emit, onClient, onStart, onStop} = require("./lib/util.js");

const TICK_MS = 250;
const CARRY_TICK_MS = 100;
const MARKER_EVERY = 4;
const DOOR_REACH = 1.8;
const INSIDE_REACH = 1.6;
const PARK_REACH = 6;
const TAKE_OUT_REACH = 4;
const MARKER_RANGE = 70;
const CONFIRM_MS = 8000;
const SELL_CONFIRM_MS = 15_000;
const BROWSE_MS = 180_000;
const FADE_MS = 400;
const MAX_HOMES = 2;
const SELL_FRACTION = 0.6;
const FIRST_WORLD = 0x52000000;
const FALL_GUARD = 6;
const LIFT_STEP = 0.1;
const TURN_STEP = Math.PI / 12;
const BLOCKING_ACTIVITIES = new Set(["race", "race-lobby", "duel", "jail"]);
const KIND_WORD = {poor: "ROOM", middle: "FLAT", rich: "VILLA"};
// The decorating keys (client/housing.js), off the keys the game plays with:
// Home/End (or numpad 4/6) turn, PgUp/PgDn (or numpad 8/2) raise and lower,
// Delete sells back, numpad 0 puts back or ends decorating.
const KEYS_HINT = "Home/End turn it, PgUp/PgDn raise and lower it, Delete sells it back, Num 0 puts it back";

// Building styles: what the street sees, what it costs. The price follows
// the building and the district it stands in: a tenement room is a couple of
// hours of jobs, a flat a few evenings, a villa a long-term goal.
const STYLES = {
    "4build142": ["Tenement room", 3800], "4build163": ["Walk-up room", 3500], "3rad03": ["Tenement room", 4200],
    "3rad02": ["Room over the shops", 4000], "9dum1": ["Room over the shops", 3600], "9dum2": ["Brick tenement room", 4400],
    "3rad07": ["Brick walk-up room", 4600], "%radovk21": ["Row-house room", 4800], "%zzdum2": ["Clapboard house room", 3200],
    "%zzdum3": ["Clapboard house room", 3200], "9dumch1": ["Clapboard cottage room", 3400], "9dumch3": ["Clapboard cottage room", 3400],
    "3rad04": ["Stone walk-up room", 4600], "4build29": ["Corner-house room", 4000], "9dum8r": ["Red-brick tenement room", 4400],
    "%dum2": ["Clapboard room", 3000], "chajda03": ["Log cabin", 2800],
    "%cinzak2": ["Apartment in the blue house", 14500], "%cinzak3": ["Apartment in the blue house", 14500],
    "%radovk10": ["Apartment over the arcade", 16500], "%radovk5": ["Apartment in the grand block", 18000],
    "3rad08": ["Brownstone apartment", 17000], "3rad06": ["Red-brick apartment", 15000], "3rad05": ["Rose-stone apartment", 14000],
    "3rad10": ["Rose-stone apartment", 14000], "9dum6r": ["Apartment in the yellow block", 13500], "4build56": ["Brick apartment", 15500],
    "4build46": ["Apartment in the stately block", 19000], "4build40": ["Brick apartment", 15000], "4build38z": ["Corner apartment", 15500],
    "%radovk30": ["Townhouse apartment", 16000], "%obldum1": ["White stucco apartment", 13000], "4build76": ["Town-house apartment", 14000],
    "#rad042": ["Brick family house", 38000], "0viil": ["Stone townhouse", 62000], "0vila": ["Victorian villa", 75000],
    "vilap": ["Hilltop mansion", 95000], "9vila2": ["Brick mansion", 90000],
};

// Every house of every map, by id; world ids follow the file's order.
const ALL = new Map();
const MAP_LISTS = {freeride: ["city"], freeride_extended: ["city", "extended"], freekrajina: ["krajina"]};
{
    let index = 0;
    for (const list of ["city", "extended", "krajina"]) {
        let number = 0;
        for (const row of HOUSES[list] ?? []) {
            const [id, tier, model, x, y, z, heading, px, py, pz, ph] = row;
            const [style, price] = STYLES[model] ?? [rooms.KINDS[tier].title, 5000];
            ALL.set(id, Object.freeze({id, tier, model, style, price, list, number: ++number, world: FIRST_WORLD + ++index,
                door: {x, y, z}, heading, out: {position: {x, y: y + 0.05, z}, heading},
                parking: px === undefined ? null : {position: {x: px, y: py, z: pz}, heading: ph}}));
        }
    }
}

// The houses of the current map, kept per map: the tick asks for them for
// every player several times a second. Read only.
let hereCache = {map: undefined, houses: []};

function here() {
    const map = maps.current()?.id;
    if (hereCache.map !== map) {
        const lists = MAP_LISTS[map] ?? [];
        hereCache = {map, houses: [...ALL.values()].filter((house) => lists.includes(house.list))};
    }
    return hereCache.houses;
}

function title(house) {
    return `${house.style} No. ${house.number}`;
}

// ---- Persistence ----------------------------------------------------------
// homes: id -> {owner (record key), ownerName, paid, bought, locked, crew (tag or null),
// guests: {key: name}, decor: [{n, item, room, x, z, lift, yaw}], next}
let homes = {};

function load() {
    homes = store.load("housing")?.homes ?? {};
}

function save() {
    store.save("housing", {version: 1, homes});
}

function keyOf(player) {
    return profiles.keyOf(player);
}

function ownedBy(player) {
    const key = keyOf(player);
    return key ? here().filter((house) => homes[house.id]?.owner === key) : [];
}

function ownedAnywhere(player) {
    const key = keyOf(player);
    return key ? Object.values(homes).filter((home) => home.owner === key).length : 0;
}

function isOwner(player, house) {
    const key = keyOf(player);
    return Boolean(key && homes[house.id]?.owner === key);
}

function mayEnter(player, house) {
    const home = homes[house.id];
    if (!home) return false;
    const key = keyOf(player);
    if (home.owner === key) return true;
    if (!home.locked) return true;
    if (key && home.guests?.[key]) return true;
    const tag = player.state.get("fr.crew")?.tag;
    return Boolean(tag && home.crew && tag === home.crew && ownerCrew(home) === home.crew);
}

// The crew the owner is in now: from their state bag when online, from the
// crews' saved roster otherwise (lib/store.js "crews", members by account
// name), read at most every ten seconds. A home opened to a crew stays shut
// to it once its owner has left that crew.
let roster = {at: 0, crews: []};
function ownerCrew(home) {
    const online = World.getPlayers().find((candidate) => keyOf(candidate) === home.owner);
    if (online) return online.state.get("fr.crew")?.tag ?? null;
    if (Date.now() - roster.at > 10_000) roster = {at: Date.now(), crews: store.load("crews")?.crews ?? []};
    const name = String(home.ownerName ?? "").toLowerCase();
    return roster.crews.find((crew) => (crew.members ?? []).includes(name))?.tag ?? null;
}

// Everyone inside who may no longer be there goes out.
function evict(house, reason) {
    for (const visitor of occupants(house)) {
        if (pending.has(visitor.id) || mayEnter(visitor, house)) continue;
        toast(visitor, reason, {seconds: 4});
        leave(visitor);
    }
}

// ---- Who is where ---------------------------------------------------------
// playerId -> {houseId, room}
const inside = new Map();
const pending = new Set();
// playerId -> {houseId, at}
const confirming = new Map();
const sellConfirm = new Map();
// playerId -> {houseId, until}
const browsing = new Map();
// playerId -> {houseId}
const editing = new Map();
// playerId -> {houseId, entry (decor row, live), item, previous (row copy) | null, isNew, object}
const carrying = new Map();
// world -> {objects: Map(n -> MapObject), fire}
const spawned = new Map();
const lastPrompt = new Map();
const lastMarkers = new Map();
// Errors already logged from the timers, by message.
const tickErrors = new Set();
let timer = null;
let carryTimer = null;
let ticks = 0;

function insideHouse(player) {
    const entry = inside.get(player.id);
    return entry ? ALL.get(entry.houseId) ?? null : null;
}

function occupants(house) {
    return World.getPlayers().filter((player) => inside.get(player.id)?.houseId === house.id);
}

function blocked(player) {
    const current = life.activity(player);
    if (current && BLOCKING_ACTIVITIES.has(current.kind)) return life.busyMessage(player);
    if ((Police.getRecord(player)?.level ?? 0) > 0) return "Not with the police on your tail.";
    return null;
}

// ---- Decorations ----------------------------------------------------------
// A decoration is kept as the floor point under its box centre (room-local
// x, z), its height above the floor and its yaw; the model's origin follows
// from its box, turned as lib/house_rooms.js turns pieces.
function decorPose(entry, item) {
    const room = rooms.ROOMS[entry.room];
    const [x0, x1, y0, , z0, z1] = item.box;
    const cx = (x0 + x1) / 2;
    const cz = (z0 + z1) / 2;
    const yaw = entry.yaw;
    const wx = cx * Math.cos(yaw) + cz * Math.sin(yaw);
    const wz = -cx * Math.sin(yaw) + cz * Math.cos(yaw);
    const local = {x: entry.x - wx, y: room.bounds.floor + entry.lift - y0, z: entry.z - wz};
    return {position: rooms.toWorld(entry.room, local), rotation: {x: 0, y: yaw, z: 0}};
}

function createDecor(entry, world, collision) {
    const item = decor.byId(entry.item);
    if (!item) return null;
    const {position, rotation} = decorPose(entry, item);
    return rooms.create(item.model, position, rotation, {virtualWorld: world, collision: collision && !decor.PASSABLE.has(item.id)});
}

function spawnHome(house) {
    if (spawned.has(house.world) || typeof MapObject === "undefined") return;
    const record = {objects: new Map(), fire: null};
    spawned.set(house.world, record);
    for (const entry of homes[house.id]?.decor ?? []) {
        const object = createDecor(entry, house.world, true);
        if (object) record.objects.set(entry.n, object);
    }
    const fire = rooms.KINDS[house.tier].rooms.map((key) => rooms.ROOMS[key].fire ? rooms.toWorld(key, rooms.ROOMS[key].fire) : null).find(Boolean);
    if (fire && typeof Fire !== "undefined") {
        try {
            record.fire = Fire.create(fire, {radius: 0.35, height: 0, intensity: 0.3, damage: 0, virtualWorld: house.world});
        } catch (error) {
            record.fire = null;
        }
    }
}

function despawnHome(house) {
    const record = spawned.get(house.world);
    if (!record) return;
    for (const object of record.objects.values()) object.destroy();
    record.fire?.destroy();
    spawned.delete(house.world);
}

// After someone leaves: an empty home packs its decorations away.
function settle(house) {
    if (house && !occupants(house).length && ![...carrying.values()].some((entry) => entry.houseId === house.id)) despawnHome(house);
}

function decorCount(house) {
    return homes[house.id]?.decor?.length ?? 0;
}

function decorValue(home) {
    return (home.decor ?? []).reduce((sum, entry) => sum + (decor.byId(entry.item)?.price ?? 0), 0);
}

// ---- Moving between the street and the rooms --------------------------------
function transfer(player, pose, world, then) {
    const {spawnGeneration} = player;
    pending.add(player.id);
    emit(player, "fr:fade", {out: true, seconds: FADE_MS / 1000});
    setTimeout(() => {
        pending.delete(player.id);
        if (!player.alive || player.spawnGeneration !== spawnGeneration) return;
        const previous = player.virtualWorld;
        player.setVirtualWorld(world);
        if (!life.teleport(player, pose.position, pose.heading)) {
            player.setVirtualWorld(previous);
            emit(player, "fr:fade", {out: false, seconds: 0.3});
            return;
        }
        emit(player, "fr:fade", {out: false, seconds: 0.6});
        then();
    }, FADE_MS);
}

function doorMarkers(player, house, roomKey) {
    const room = rooms.ROOMS[roomKey];
    markers(player, "home-doors", room.doors.map((door, index) => ({id: `d${index}`, position: rooms.toWorld(roomKey, door.at), label: door.label,
        color: door.to === "street" ? COLORS.green : COLORS.gold, model: "sipka.i3d", range: 14, near: 10, map: false})));
}

function enter(player, house) {
    const problem = !rooms.isBuilt() ? "The door is stuck. Try again in a moment." : blocked(player);
    if (problem) {
        toast(player, problem, {tone: "bad", seconds: 3});
        return;
    }
    const kind = rooms.KINDS[house.tier];
    transfer(player, rooms.pose(kind.entry, rooms.ROOMS[kind.entry].arrive), house.world, () => {
        inside.set(player.id, {houseId: house.id, room: kind.entry});
        // Only once they are in: a transfer that never arrives leaves nothing behind.
        spawnHome(house);
        doorMarkers(player, house, kind.entry);
        const home = homes[house.id];
        if (home?.owner === keyOf(player)) {
            toast(player, decorCount(house) ? "Home. F2 → Homes to decorate." : "Your new home is bare. F2 → Homes has furniture and pictures to buy.", {title: title(house), tone: "good", seconds: 5});
        } else {
            toast(player, `${home?.ownerName ?? "Someone"}'s ${kind.title.toLowerCase()}.`, {title: title(house), seconds: 4});
            const owner = home && World.getPlayers().find((candidate) => keyOf(candidate) === home.owner);
            if (owner && owner.id !== player.id) toast(owner, `${player.nickname} came into your home.`, {seconds: 4});
        }
    });
}

function leave(player, {to = null} = {}) {
    const house = insideHouse(player);
    if (!house) return;
    stopEditing(player);
    const pose = to ?? house.out;
    transfer(player, pose, MAIN_WORLD, () => {
        inside.delete(player.id);
        markers(player, "home-doors", []);
        settle(house);
    });
}

// Through a door between two rooms of a villa.
function walkThrough(player, house, fromKey, door) {
    const target = rooms.ROOMS[door.to];
    const pose = target.returns?.[fromKey] ? rooms.pose(door.to, target.returns[fromKey]) : rooms.pose(door.to, target.arrive);
    stopCarrying(player, {keep: true});
    transfer(player, pose, house.world, () => {
        inside.set(player.id, {houseId: house.id, room: door.to});
        doorMarkers(player, house, door.to);
    });
}

// Out of the rooms without a walk: a death, a disconnect, a mission change.
function release(player) {
    const house = insideHouse(player);
    stopEditing(player);
    inside.delete(player.id);
    pending.delete(player.id);
    markers(player, "home-doors", []);
    if (house && player.virtualWorld === house.world) player.setVirtualWorld(MAIN_WORLD);
    settle(house);
}

// ---- Buying and selling -----------------------------------------------------
function buy(player, house) {
    if (homes[house.id]) return toast(player, "Somebody beat you to it: this one is sold.", {tone: "bad"});
    if (ownedAnywhere(player) >= MAX_HOMES) return toast(player, `You own ${MAX_HOMES} homes already. Sell one first (F2 → Homes).`, {tone: "bad"});
    if (!player.trySpendMoney(house.price)) return toast(player, `${title(house)} costs ${formatMoney(house.price)}; you have ${formatMoney(player.money)}.`, {tone: "bad"});
    const key = keyOf(player);
    homes[house.id] = {owner: key, ownerName: player.nickname, paid: house.price, bought: Date.now(), locked: true, crew: null, guests: {}, decor: [], next: 1};
    save();
    profiles.bump(player, "homesBought");
    toast(player, `Yours for ${formatMoney(house.price)}. G at the door lets you in; F2 → Homes to lock, invite and decorate.` +
        (house.parking ? " Park a car of yours at the kerb outside with G." : ""), {title: title(house), tone: "gold", seconds: 9});
    if (house.tier !== "poor") feed("home", [name(player, COLORS.gold), text(` bought a ${house.tier === "rich" ? house.style.toLowerCase() : "flat"}`)]);
    browsing.delete(player.id);
    compass(player, "home", 18, null);
    publish(player, true);
}

function sell(player, house) {
    const home = homes[house.id];
    if (!home || home.owner !== keyOf(player)) return toast(player, "That is not your home.", {tone: "bad"});
    for (const visitor of occupants(house)) leave(visitor);
    for (const [id, entry] of carrying) if (entry.houseId === house.id) carrying.delete(id);
    const refund = Math.round((home.paid ?? house.price) * SELL_FRACTION) + decorValue(home);
    delete homes[house.id];
    save();
    despawnHome(house);
    player.giveMoney(refund);
    toast(player, `Sold back for ${formatMoney(refund)} (60% of the price, the furnishings at full value).`, {title: title(house), tone: "good", seconds: 7});
    publish(player, true);
}

// ---- The decorating editor ----------------------------------------------------
function startEditing(player, house) {
    editing.set(player.id, {houseId: house.id});
    emit(player, "fr:home:edit", {on: true});
}

function stopEditing(player) {
    stopCarrying(player, {keep: true});
    if (editing.delete(player.id)) emit(player, "fr:home:edit", {on: false});
}

function forward(player) {
    const heading = headingOf(player.rotation);
    return {heading, x: Math.sin(heading), z: Math.cos(heading)};
}

// The carried piece, if any, follows the player: in front of them on the
// floor (raised by its lift), turned by its yaw, kept inside the walls.
function carryTarget(player, entry, item) {
    const roomKey = inside.get(player.id)?.room;
    const room = rooms.ROOMS[roomKey];
    if (!room) return null;
    const [x0, x1, y0, y1, z0, z1] = item.box;
    const hx = (x1 - x0) / 2;
    const hz = (z1 - z0) / 2;
    const cos = Math.abs(Math.cos(entry.yaw));
    const sin = Math.abs(Math.sin(entry.yaw));
    const ex = cos * hx + sin * hz;
    const ez = sin * hx + cos * hz;
    const ahead = 0.9 + Math.max(ex, ez);
    const local = rooms.toLocal(roomKey, player.position);
    const dir = forward(player);
    const {bounds} = room;
    // Pictures, mirrors and trophies lie flat against a wall; everything else keeps clear of it.
    const limit = decor.WALL.has(item.id) ? {x0: room.walls.x0 + 0.01, x1: room.walls.x1 - 0.01, z0: room.walls.z0 + 0.01, z1: room.walls.z1 - 0.01} : bounds;
    const clamp = (value, low, high) => low > high ? (low + high) / 2 : Math.min(high, Math.max(low, value));
    entry.room = roomKey;
    entry.x = clamp(local.x + dir.x * ahead, limit.x0 + ex, limit.x1 - ex);
    entry.z = clamp(local.z + dir.z * ahead, limit.z0 + ez, limit.z1 - ez);
    entry.lift = Math.round(clamp(entry.lift, 0, Math.max(0, bounds.ceiling - bounds.floor - (y1 - y0) - 0.05)) * 100) / 100;
    return decorPose(entry, item);
}

function pickUp(player, house, entry, isNew) {
    const record = spawned.get(house.world);
    const object = record?.objects.get(entry.n);
    if (object) object.destroy();
    record?.objects.delete(entry.n);
    const item = decor.byId(entry.item);
    const ghost = createDecor(entry, house.world, false);
    carrying.set(player.id, {houseId: house.id, entry, item, previous: isNew ? null : {...entry}, isNew, object: ghost});
}

// How close a solid piece may come to where people arrive and to the door arrows.
const DOORWAY_CLEAR = 1.2;

function blocksDoorway(entry, item) {
    if (decor.PASSABLE.has(item.id)) return false;
    const room = rooms.ROOMS[entry.room];
    const [x0, x1, , , z0, z1] = item.box;
    const half = Math.max(x1 - x0, z1 - z0) / 2;
    return [room.arrive.position, ...room.doors.map((door) => door.at)]
        .some((point) => Math.hypot(point.x - entry.x, point.z - entry.z) - half < DOORWAY_CLEAR);
}

function place(player) {
    const carried = carrying.get(player.id);
    if (!carried) return;
    const house = ALL.get(carried.houseId);
    const home = homes[carried.houseId];
    if (!house || !home) {
        carrying.delete(player.id);
        carried.object?.destroy();
        return;
    }
    // Where it follows the player now, within the walls and under the ceiling.
    if (insideHouse(player)?.id === house.id) carryTarget(player, carried.entry, carried.item);
    if (blocksDoorway(carried.entry, carried.item)) {
        toast(player, "Keep the doorway clear: put it further into the room.", {tone: "bad", seconds: 3});
        return;
    }
    if (carried.isNew && !player.trySpendMoney(carried.item.price)) {
        toast(player, `${carried.item.name}: ${formatMoney(carried.item.price)}. You have ${formatMoney(player.money)}.`, {tone: "bad"});
        return;
    }
    carrying.delete(player.id);
    carried.object?.destroy();
    if (carried.isNew) home.decor.push(carried.entry);
    save();
    const record = spawned.get(house.world);
    const object = createDecor(carried.entry, house.world, true);
    if (record && object) record.objects.set(carried.entry.n, object);
    toast(player, `${carried.item.name} placed.`, {seconds: 2});
}

// keep: put a carried piece back where it was (a new one is refunded).
function stopCarrying(player, {keep = true} = {}) {
    const carried = carrying.get(player.id);
    if (!carried) return;
    carrying.delete(player.id);
    carried.object?.destroy();
    const house = ALL.get(carried.houseId);
    const home = homes[carried.houseId];
    // A new piece is paid for only when it is put down.
    if (!house || !home || carried.isNew) return;
    if (keep && carried.previous) Object.assign(carried.entry, carried.previous);
    const record = spawned.get(house.world);
    const object = createDecor(carried.entry, house.world, true);
    if (record && object) record.objects.set(carried.entry.n, object);
}

function sellCarried(player) {
    const carried = carrying.get(player.id);
    if (!carried) return;
    carrying.delete(player.id);
    carried.object?.destroy();
    if (carried.isNew) {
        toast(player, `${carried.item.name} left in the shop.`, {seconds: 2});
        return;
    }
    const home = homes[carried.houseId];
    if (home) home.decor = home.decor.filter((entry) => entry !== carried.entry);
    save();
    player.giveMoney(carried.item.price);
    toast(player, `${carried.item.name} sold back for ${formatMoney(carried.item.price)}.`, {tone: "good", seconds: 3});
}

function nearestDecor(player, house) {
    const room = inside.get(player.id)?.room;
    const dir = forward(player);
    const probe = {x: player.position.x + dir.x * 0.9, y: player.position.y, z: player.position.z + dir.z * 0.9};
    let best = null;
    for (const entry of homes[house.id]?.decor ?? []) {
        if (entry.room !== room) continue;
        const center = rooms.toWorld(entry.room, {x: entry.x, y: 0, z: entry.z});
        const gap = flatDistance(probe, center);
        if (gap <= 1.8 && (!best || gap < best.gap)) best = {entry, gap};
    }
    return best?.entry ?? null;
}

function buyDecor(player, house, item) {
    const home = homes[house.id];
    if (!decor.sells(house.tier, item)) return toast(player, `A ${item.name.toLowerCase()} belongs in a grander home.`, {tone: "bad"});
    if (home.decor.length >= decor.LIMITS[house.tier]) return toast(player, `This home holds ${decor.LIMITS[house.tier]} pieces. Sell one back first (pick it up, Delete).`, {tone: "bad"});
    if (carrying.has(player.id)) stopCarrying(player, {keep: true});
    if (player.money < item.price) return toast(player, `${item.name}: ${formatMoney(item.price)}. You have ${formatMoney(player.money)}.`, {tone: "bad"});
    const room = inside.get(player.id).room;
    const entry = {n: home.next++, item: item.id, room, x: 0, z: 0, lift: 0, yaw: forward(player).heading};
    if (!editing.has(player.id)) startEditing(player, house);
    spawnHome(house);
    pickUp(player, house, entry, true);
    toast(player, `${item.name}, ${formatMoney(item.price)} when you put it down. Walk it into place and press G. ${KEYS_HINT}.`, {seconds: 8});
}

function carryTick() {
    for (const [id, carried] of carrying) {
        try {
            const player = playerById(id);
            const house = ALL.get(carried.houseId);
            if (!player || !house || insideHouse(player)?.id !== house.id || !carried.object) continue;
            const pose = carryTarget(player, carried.entry, carried.item);
            if (pose) carried.object.moveTo(pose.position, 6, pose.rotation);
        } catch (error) {
            const key = String(error?.message);
            if (!tickErrors.has(key)) console.error(`[freeride] Carrying a home piece failed: ${error?.stack ?? error}`);
            tickErrors.add(key);
        }
    }
}

onClient("fr:home:edit", (player, request) => {
    try {
        editKey(player, request);
    } catch (error) {
        console.error(`[freeride] Decorating key failed for ${player.nickname}: ${error?.stack ?? error}`);
    }
});

function editKey(player, request) {
    const carried = carrying.get(player.id);
    if (!editing.has(player.id) || !insideHouse(player)) return;
    if (request?.sell) return carried ? sellCarried(player) : undefined;
    if (request?.cancel) {
        if (carried) stopCarrying(player, {keep: true});
        else {
            stopEditing(player);
            toast(player, "Done decorating.", {seconds: 2});
        }
        return;
    }
    if (!carried) return;
    const turn = Math.sign(Number(request?.turn) || 0);
    const lift = Math.sign(Number(request?.lift) || 0);
    if (turn) carried.entry.yaw = (carried.entry.yaw + turn * TURN_STEP) % (2 * Math.PI);
    if (lift) carried.entry.lift = Math.round((carried.entry.lift + lift * LIFT_STEP) * 100) / 100;
    // Kept between the floor and the ceiling at once.
    carryTarget(player, carried.entry, carried.item);
}

// ---- Cars at the kerb ---------------------------------------------------------
function parkingNear(player, reach) {
    for (const house of ownedBy(player)) {
        if (house.parking && flatDistance(player.position, house.parking.position) <= reach) return house;
    }
    return null;
}

// /park beside the car on a home's spot parks it at that home.
dealership.setHomeSpot((player, vehicle) => ownedBy(player).find((house) => house.parking &&
    flatDistance(vehicle.position, house.parking.position) <= PARK_REACH)?.id ?? null);

function parkHere(player, house) {
    const out = dealership.carOut(player);
    if (!out || out.vehicle.id !== player.getVehicle()?.id) return toast(player, "Only a car from your garage parks here (/garage).", {tone: "bad"});
    if (!dealership.parkAtHome(player, house.id)) return toast(player, "Someone is still sitting in the car.", {tone: "bad"});
    toast(player, `Into the garage of ${title(house)}, as it is, restarts and all. G on this spot brings it out again.`, {tone: "good", seconds: 6});
}

function takeOut(player, house, slot = null) {
    const parked = dealership.parkedAt(player, house.id);
    const pick = slot === null ? parked[0] : parked.find((car) => car.slot === slot);
    if (!pick) return toast(player, "No car of yours is parked here.", {tone: "bad"});
    const problem = blocked(player);
    if (problem) return toast(player, problem, {tone: "bad"});
    if (!dealership.fromHome(player, pick.slot, house.parking)) return toast(player, "The car could not be brought out. Try again.", {tone: "bad"});
    toast(player, "Your car is at the kerb.", {tone: "good", seconds: 3});
}

// ---- The job key ------------------------------------------------------------------
function streetDoor(player) {
    let best = null;
    for (const house of here()) {
        const gap = distance(player.position, house.door);
        if (gap <= DOOR_REACH && (!best || gap < best.gap)) best = {house, gap};
    }
    return best?.house ?? null;
}

function roomDoor(player) {
    const entry = inside.get(player.id);
    const room = entry && rooms.ROOMS[entry.room];
    if (!room) return null;
    return room.doors.find((door) => distance(player.position, rooms.toWorld(entry.room, door.at)) <= INSIDE_REACH) ?? null;
}

function knock(player, house) {
    const home = homes[house.id];
    const owner = home && World.getPlayers().find((candidate) => keyOf(candidate) === home.owner);
    if (!owner) return toast(player, "Nobody answers. The door is locked.", {seconds: 3});
    toast(player, `You knock. ${owner.nickname} lives here.`, {seconds: 3});
    toast(owner, `${player.nickname} is knocking at ${title(house)}. /home invite ${player.nickname} lets them in.`, {seconds: 8});
}

// A press that fails is logged and taken, never thrown into the key handler.
jobkey.register({press(player) {
    try {
        return pressHome(player);
    } catch (error) {
        console.error(`[freeride] Homes key failed for ${player.nickname}: ${error?.stack ?? error}`);
        return false;
    }
}});

function pressHome(player) {
    if (!playable(player) || pending.has(player.id)) return false;
    const house = insideHouse(player);
    if (house) {
        if (carrying.has(player.id)) {
            place(player);
            return true;
        }
        const door = roomDoor(player);
        if (door) {
            if (door.to === "street") leave(player);
            else walkThrough(player, house, inside.get(player.id).room, door);
            return true;
        }
        if (editing.has(player.id) && isOwner(player, house)) {
            const entry = nearestDecor(player, house);
            if (entry) pickUp(player, house, entry, false);
            else toast(player, "Nothing to pick up there. Stand in front of a piece you bought.", {seconds: 3});
            return true;
        }
        return false;
    }
    if (player.virtualWorld !== MAIN_WORLD) return false;
    const vehicle = player.getVehicle();
    if (vehicle) {
        const spot = parkingNear(player, PARK_REACH);
        if (!spot || player.getSeat() !== 0) return false;
        parkHere(player, spot);
        return true;
    }
    const door = streetDoor(player);
    if (door) {
        const home = homes[door.id];
        if (!home) {
            const ask = confirming.get(player.id);
            if (ask?.houseId === door.id && Date.now() - ask.at <= CONFIRM_MS) {
                confirming.delete(player.id);
                buy(player, door);
            } else {
                confirming.set(player.id, {houseId: door.id, at: Date.now()});
                toast(player, `${title(door)}: ${rooms.KINDS[door.tier].title.toLowerCase()}, ${formatMoney(door.price)}. Press G again to buy.`, {title: "For sale", seconds: 6});
            }
        } else if (mayEnter(player, door)) enter(player, door);
        else knock(player, door);
        return true;
    }
    const spot = parkingNear(player, TAKE_OUT_REACH);
    if (spot && dealership.parkedAt(player, spot.id).length) {
        takeOut(player, spot);
        return true;
    }
    return false;
}

function promptFor(player) {
    const house = insideHouse(player);
    if (house) {
        const carried = carrying.get(player.id);
        if (carried) return carried.isNew ? `G buy ${formatMoney(carried.item.price)} · Home/End turn · PgUp/PgDn raise · Num0 back` : "G place · Home/End turn · PgUp/PgDn raise · Del sell";
        const door = roomDoor(player);
        if (door) return door.to === "street" ? "G · STEP OUTSIDE" : `G · ${door.label}`;
        if (editing.has(player.id)) return "Decorating · G picks up a piece · Num 0 when done";
        return null;
    }
    if (player.virtualWorld !== MAIN_WORLD) return null;
    if (player.getVehicle()) {
        const spot = parkingNear(player, PARK_REACH);
        return spot && dealership.carOut(player)?.vehicle.id === player.getVehicle().id ? "G · PARK IT HERE" : null;
    }
    const door = streetDoor(player);
    if (door) {
        const home = homes[door.id];
        if (!home) {
            const ask = confirming.get(player.id);
            return ask?.houseId === door.id && Date.now() - ask.at <= CONFIRM_MS ? `G AGAIN · BUY FOR ${formatMoney(door.price)}` : `G · BUY · ${formatMoney(door.price)}`;
        }
        return mayEnter(player, door) ? "G · GO IN" : "G · KNOCK";
    }
    const spot = parkingNear(player, TAKE_OUT_REACH);
    const parked = spot ? dealership.parkedAt(player, spot.id) : [];
    return parked.length ? "G · TAKE OUT YOUR CAR" : null;
}

// ---- Markers ------------------------------------------------------------------------
function streetMarkers(player) {
    const key = keyOf(player);
    const items = [];
    const browse = browsing.get(player.id);
    for (const house of here()) {
        const home = homes[house.id];
        const mine = home?.owner === key;
        const near = flatDistance(player.position, house.door) <= MARKER_RANGE;
        if (mine) {
            items.push({id: house.id, position: house.door, label: "HOME", color: COLORS.green, model: "sipka.i3d", range: 60, near: 30,
                map: {icon: "square", label: "Home", size: 5}});
            if (house.parking && near) items.push({id: `${house.id}/p`, position: house.parking.position, label: "PARKING", color: COLORS.blue, range: 35, near: 18, map: false});
        } else if (home && mayEnter(player, house)) {
            items.push({id: house.id, position: house.door, label: `${String(home.ownerName).toUpperCase()}'S PLACE`, color: COLORS.blue, model: "sipka.i3d", range: 50, near: 25,
                map: home.locked ? {icon: "square", label: `${home.ownerName}'s place`, size: 4} : false});
        } else if (!home && near) {
            items.push({id: house.id, position: house.door, label: `FOR SALE · ${KIND_WORD[house.tier]} · ${formatMoney(house.price)}`, color: COLORS.gold,
                model: "sipka.i3d", range: 40, near: 25, map: false});
        }
        if (browse?.houseId === house.id && !home) {
            items.push({id: `${house.id}/b`, position: house.door, color: COLORS.gold, map: {icon: "diamond", label: `For sale ${formatMoney(house.price)}`, size: 6, edge: true}});
        }
    }
    return items;
}

function publish(player, force = false) {
    if (insideHouse(player)) return;
    const items = streetMarkers(player);
    const payload = JSON.stringify(items);
    if (!force && lastMarkers.get(player.id) === payload) return;
    lastMarkers.set(player.id, payload);
    markers(player, "homes", items);
}

function tickPlayer(player, now) {
    let house = insideHouse(player);
    // Another module moved them out (the jail, a blackout): the home no longer holds them.
    if (house && player.virtualWorld !== house.world && !pending.has(player.id)) {
        release(player);
        house = null;
    }
    if (house && !pending.has(player.id) && !mayEnter(player, house)) {
        toast(player, "You are not welcome here any more.", {seconds: 4});
        leave(player);
        return;
    }
    const roomKey = house ? inside.get(player.id)?.room : null;
    const room = roomKey ? rooms.ROOMS[roomKey] : null;
    if (room && player.alive && !pending.has(player.id)) {
        const floor = rooms.toWorld(roomKey, {x: 0, y: room.bounds.floor, z: 0}).y;
        if (player.position.y < floor - FALL_GUARD) life.teleport(player, rooms.pose(roomKey, room.arrive).position, 0);
    }
    const browse = browsing.get(player.id);
    if (browse && (now > browse.until || flatDistance(player.position, ALL.get(browse.houseId).door) < 12)) {
        browsing.delete(player.id);
        compass(player, "home", 18, null);
    }
    const textNow = playable(player) && !pending.has(player.id) ? promptFor(player) : null;
    if (lastPrompt.get(player.id) !== textNow) {
        lastPrompt.set(player.id, textNow);
        jobkey.prompt(player, textNow, null, "home");
    }
    if (ticks % MARKER_EVERY === 0 && playable(player)) publish(player);
}

// One player's trouble never stops the others' homes, or the resource.
function tick() {
    ticks++;
    const now = Date.now();
    for (const player of World.getPlayers()) {
        try {
            tickPlayer(player, now);
        } catch (error) {
            const key = String(error?.message);
            if (!tickErrors.has(key)) console.error(`[freeride] Homes tick failed for ${player.nickname}: ${error?.stack ?? error}`);
            tickErrors.add(key);
        }
    }
}

// ---- Commands and the menu ------------------------------------------------------------
function target(player, id) {
    if (id) return ALL.get(id) ?? null;
    return insideHouse(player) ?? ownedBy(player).sort((a, b) => flatDistance(player.position, a.door) - flatDistance(player.position, b.door))[0] ?? null;
}

function mine(player, id) {
    const house = target(player, id);
    if (!house || !isOwner(player, house)) {
        toast(player, "That is not one of your homes.", {tone: "bad"});
        return null;
    }
    return house;
}

const subcommands = {
    buy(player) {
        const door = playable(player) && player.virtualWorld === MAIN_WORLD ? streetDoor(player) : null;
        if (!door || homes[door.id]) return toast(player, "Stand at the door of a home for sale (the gold arrow).", {tone: "bad"});
        buy(player, door);
    },
    show(player, [id]) {
        const house = ALL.get(id);
        if (!house || !here().includes(house)) return toast(player, "No such home on this map.", {tone: "bad"});
        browsing.set(player.id, {houseId: house.id, until: Date.now() + BROWSE_MS});
        compass(player, "home", 18, {position: house.door});
        publish(player, true);
        toast(player, `${title(house)} is marked on the city map (TAB); your compass points there.`, {seconds: 5});
    },
    sell(player, [id]) {
        const house = mine(player, id);
        if (!house) return;
        const ask = sellConfirm.get(player.id);
        if (ask?.houseId === house.id && Date.now() - ask.at <= SELL_CONFIRM_MS) {
            sellConfirm.delete(player.id);
            sell(player, house);
            return;
        }
        sellConfirm.set(player.id, {houseId: house.id, at: Date.now()});
        const refund = Math.round((homes[house.id].paid ?? house.price) * SELL_FRACTION) + decorValue(homes[house.id]);
        toast(player, `Sell ${title(house)} for ${formatMoney(refund)}? Press Sell again (or /home sell ${house.id}) within 15 seconds.`, {tone: "bad", seconds: 8});
    },
    lock(player, [id]) {
        const house = mine(player, id);
        if (!house) return;
        const home = homes[house.id];
        home.locked = !home.locked;
        save();
        if (home.locked) evict(house, `${player.nickname} locked up and showed you out.`);
        toast(player, home.locked ? "Locked: only you, your guests and your crew (if you let them) come in." : "Open house: anyone may walk in.", {seconds: 4});
    },
    crew(player, [id]) {
        const house = mine(player, id);
        if (!house) return;
        const home = homes[house.id];
        const tag = player.state.get("fr.crew")?.tag ?? null;
        if (!home.crew && !tag) return toast(player, "You are not in a crew.", {tone: "bad"});
        home.crew = home.crew ? null : tag;
        save();
        if (!home.crew) evict(house, `${player.nickname} showed the crew out.`);
        toast(player, home.crew ? `Your crew [${home.crew}] may come and go.` : "Your crew needs an invitation again.", {seconds: 4});
    },
    invite(player, [who, id]) {
        const guest = findPlayer(who);
        const house = mine(player, id);
        if (!house) return;
        if (!guest || guest.id === player.id || !keyOf(guest)) return toast(player, "Use /home invite <player>.", {tone: "bad"});
        homes[house.id].guests = {...homes[house.id].guests, [keyOf(guest)]: guest.nickname};
        save();
        toast(player, `${guest.nickname} may come into ${title(house)} now.`, {tone: "good", seconds: 4});
        toast(guest, `${player.nickname} invited you to ${title(house)}. It is marked on your city map (TAB).`, {tone: "good", seconds: 7});
        publish(guest, true);
    },
    uninvite(player, [who, id]) {
        const house = mine(player, id);
        if (!house) return;
        const home = homes[house.id];
        const wanted = String(who ?? "").toLowerCase();
        const key = Object.keys(home.guests ?? {}).find((guestKey) => String(home.guests[guestKey]).toLowerCase() === wanted);
        if (!key) return toast(player, "No guest by that name.", {tone: "bad"});
        delete home.guests[key];
        save();
        evict(house, `${player.nickname} took your key back.`);
        toast(player, `${who} is no longer a guest.`, {seconds: 3});
    },
    leave(player) {
        if (!insideHouse(player) || pending.has(player.id)) return toast(player, "You are not in a home.", {tone: "bad"});
        leave(player);
    },
    kick(player) {
        const house = insideHouse(player);
        if (!house || !isOwner(player, house)) return toast(player, "Stand in your own home to show people out.", {tone: "bad"});
        const visitors = occupants(house).filter((visitor) => visitor.id !== player.id);
        for (const visitor of visitors) {
            toast(visitor, `${player.nickname} showed you out.`, {seconds: 3});
            leave(visitor);
        }
        toast(player, visitors.length ? "Everyone else has gone." : "Nobody else is here.", {seconds: 3});
    },
    edit(player) {
        const house = insideHouse(player);
        if (!house || !isOwner(player, house)) return toast(player, "Decorate inside your own home.", {tone: "bad"});
        if (editing.has(player.id)) {
            stopEditing(player);
            toast(player, "Done decorating.", {seconds: 2});
            return;
        }
        startEditing(player, house);
        toast(player, "Decorating: face a piece you bought and press G to pick it up. Num 0 when done.", {seconds: 6});
    },
    decor(player, [itemId]) {
        const house = insideHouse(player);
        const item = decor.byId(itemId);
        if (!house || !isOwner(player, house)) return toast(player, "Buy furnishings inside your own home.", {tone: "bad"});
        if (!item) return toast(player, "No such piece.", {tone: "bad"});
        buyDecor(player, house, item);
    },
    car(player, [slot]) {
        const spot = playable(player) && player.virtualWorld === MAIN_WORLD && !player.getVehicle() ? parkingNear(player, TAKE_OUT_REACH) : null;
        if (!spot) return toast(player, "Stand on your home's parking spot.", {tone: "bad"});
        takeOut(player, spot, slot === undefined ? null : Number(slot) - 1);
    },
};

commands.register({name: "home", aliases: ["house"], usage: "/home [buy | show <id> | sell | lock | crew | invite <player> | uninvite <name> | kick | leave | edit | decor <item> | car]",
    help: "Your homes: buy at the door, lock, invite, decorate, sell.", category: "City"}, (player, [action, ...args]) => {
    const handler = subcommands[String(action ?? "").toLowerCase()];
    if (!handler) {
        emit(player, "fr:menu:open", {tab: "homes"});
        return;
    }
    handler(player, args);
});

commands.register({name: "homes", usage: "/homes", help: "Homes for sale near you, on the city map.", category: "City"}, (player) => {
    const from = insideHouse(player)?.door ?? player.position;
    const free = here().filter((house) => !homes[house.id]).sort((a, b) => flatDistance(from, a.door) - flatDistance(from, b.door));
    if (!free.length) return toast(player, "Every home on this map is sold.", {seconds: 4});
    const nearest = free[0];
    for (const house of free.slice(0, 5)) {
        player.sendMessage(`${title(house)} · ${rooms.KINDS[house.tier].title} · ${formatMoney(house.price)} · ${Math.round(flatDistance(from, house.door))} m · /home show ${house.id}`, 0xd9cdb4);
    }
    subcommands.show(player, [nearest.id]);
});

// Distances count from the street: from the door of the home one is in.
function view(player, house) {
    const home = homes[house.id];
    const from = insideHouse(player)?.door ?? player.position;
    return {id: house.id, name: title(house), kind: rooms.KINDS[house.tier].title, tier: house.tier, price: house.price,
        distance: Math.round(flatDistance(from, house.door)), parking: Boolean(house.parking),
        owner: home?.ownerName ?? null};
}

menu.provide("homes", (player) => {
    const key = keyOf(player);
    const list = here();
    const current = insideHouse(player);
    const mineList = ownedBy(player).map((house) => {
        const home = homes[house.id];
        return {...view(player, house), locked: home.locked, crew: home.crew, guests: Object.values(home.guests ?? {}),
            decor: home.decor.length, limit: decor.LIMITS[house.tier], sellFor: Math.round((home.paid ?? house.price) * SELL_FRACTION) + decorValue(home),
            cars: house.parking ? dealership.parkedAt(player, house.id).map((car) => ({slot: car.slot + 1, model: car.model})) : [],
            here: current?.id === house.id};
    });
    const visiting = list.filter((house) => homes[house.id] && homes[house.id].owner !== key && mayEnter(player, house)).map((house) => view(player, house));
    const forSale = list.filter((house) => !homes[house.id]).map((house) => view(player, house)).sort((a, b) => a.distance - b.distance);
    const counts = {poor: 0, middle: 0, rich: 0};
    for (const house of forSale) counts[house.tier]++;
    const own = current && isOwner(player, current);
    return {
        max: MAX_HOMES, owned: ownedAnywhere(player), mine: mineList, visiting, forSale: forSale.slice(0, 12), counts, total: list.length,
        browsing: browsing.get(player.id)?.houseId ?? null,
        inside: current ? {id: current.id, name: title(current), mine: own, owner: homes[current.id]?.ownerName ?? null, editing: editing.has(player.id),
            decor: decorCount(current), limit: decor.LIMITS[current.tier],
            catalogue: own ? decor.forTier(current.tier).map((item) => ({id: item.id, name: item.name, price: item.price, category: item.category})) : []} : null,
        prices: Object.fromEntries(Object.entries({poor: [], middle: [], rich: []}).map(([tier]) => {
            const prices = list.filter((house) => house.tier === tier).map((house) => house.price);
            return [tier, prices.length ? [Math.min(...prices), Math.max(...prices)] : null];
        })),
    };
}, ["homes"]);

// ---- Lifecycle ------------------------------------------------------------------------
onStart(() => {
    load();
    rooms.build();
    timer = setInterval(tick, TICK_MS);
    carryTimer = setInterval(carryTick, CARRY_TICK_MS);
});

onStop(() => {
    clearInterval(timer);
    clearInterval(carryTimer);
    for (const player of World.getPlayers()) {
        const house = insideHouse(player);
        stopCarrying(player, {keep: true});
        if (!house) continue;
        if (player.virtualWorld === house.world) player.setVirtualWorld(MAIN_WORLD);
        if (player.alive) life.teleport(player, house.out.position, house.out.heading);
    }
    for (const house of ALL.values()) despawnHome(house);
    rooms.teardown();
    save();
});

Events.on("playerSpawn", (player) => {
    if (!life.isTransit(player)) {
        lastMarkers.delete(player.id);
        publish(player, true);
    }
});

Events.on("playerDeath", (player) => {
    stopCarrying(player, {keep: true});
    if (inside.has(player.id)) release(player);
});

Events.on("playerDisconnect", (player) => {
    stopCarrying(player, {keep: true});
    release(player);
    for (const map of [confirming, sellConfirm, browsing, editing, lastPrompt, lastMarkers]) map.delete(player.id);
});

// The mission change took every map object and world with it.
Events.on("missionChange", () => {
    for (const player of World.getPlayers()) {
        const house = insideHouse(player);
        if (house && player.virtualWorld === house.world) player.setVirtualWorld(MAIN_WORLD);
    }
    inside.clear();
    pending.clear();
    carrying.clear();
    editing.clear();
    spawned.clear();
    lastMarkers.clear();
    rooms.forget();
    rooms.build();
});

module.exports = {insideHouse, release, homesOf: ownedBy, ALL};
