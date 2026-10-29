// The rum distillery: a hidden still at an old woodcutters' camp in the
// lowland woods, on Lost Heaven Extended. Nothing marks it on the map until a
// player learns of it. A short quest chain leads there:
//   1. Ernesto, the sailor who drinks in Salieri's billiard room, tells of a
//      Cuban moonshiner hiding in the woods west of the lowland road.
//   2. The player finds the camp; old Hank runs it and needs copper.
//   3. The player unbolts the copper worm off a hardware dealer's truck in
//      the village (the police are called) and brings it to Hank.
//   4. First fire: molasses from the village store, a batch on the still,
//      the cask ages, the rum is bottled.
//   5. The first load sold. Then Paco, Ernesto's cousin at the docks, buys.
// After that the loop repeats: fetch molasses (G in a car stopped at the
// village store), fire the still (G) and keep its pressure gauge in the
// green by tapping G to vent (it blows up past the top), let the cask age,
// bottle it (hold G), load a car in the clearing and drive the cases to
// Luigi at Salieri's (safe, pays least), the speakeasy at Morello's (the
// police may notice) or Paco at the docks (pays most, the city is told and
// anyone may drive the car in). Every batch and sale raises the operation's
// heat, which cools by itself, by a bribe at Salieri's (/rum bribe) or by
// paying Sergeant Doyle when he turns up at the camp; at full heat the
// revenue men come for the still (after a warning: defend it or bribe them
// off). Morello's boys come for a big stock of cases,
// another player may steal cases while an owner is online, and a wedding in
// the village sometimes orders rum. A crew shares one operation.
//
// Where it is. The camp is the stock FREEKRAJINA scene2.bin's woodcutters'
// clearing: the shed "@bouda00" (models/@bouda06.4ds) at (-1735.92, 15.22,
// 1260.16), the shed "@bouda02" at (-1729.97, 15.91, 1239.16) and the log
// piles "klady7" (-1750.18, 14.56, 1256.02), "klady00" (-1739.65, 15.10,
// 1240.18), "klady03" (-1726.37, 15.21, 1268.30) and "klady8" (-1740.02,
// 14.51, 1277.88) (models/klady.4ds, 8.3 x 3.6 m), with the pole "Sloup43"
// at (-1739.61, 14.69, 1260.21). Freeride Extended's scene2.bin places the
// same sheds, log piles and pole at the same positions and heights. Ground
// heights below are sampled from the FREEKRAJINA scene.4ds ground mesh; the
// still and its gear stand in the open ground between the log piles, at
// least 5.4 m from any of them. The nearest road is the road graph line from
// crossing (-1850.7, -4.0, 1843.7) south past (-1610.3, 15.5, 1259.4),
// 130 m east, beyond a fenced pasture whose fences (plot3/plot4) run from
// (-1678, 1279) south-west to (-1734, 1225): cars reach the clearing round
// its north side. Stock FREEKRAJINA also has barrels here that the mod's
// scene lacks, so the distillery runs on Lost Heaven Extended only.
//
// The props are stock models (sizes from their a2.dta meshes): the pot
// "xv sud" (0.64 x 0.77 m, scaled 1.5), the pipe "xv trubka4" (1.5 m long),
// the worm tub "xsud 01" (0.84 x 1.0 m), mash vats "tutsud" (0.78 x 0.98 m,
// centred), molasses sacks "xpytel2", casks "sud01" (base 0.5 m below the
// origin) and crates "bedna01" (0.8 m, centred).
//
// Elsewhere: Ernesto is bars.js's sailor (sailor03, the MISE16-SALIERY
// frame at (-1778.47, -5.56, 19.44) in Salieri's billiard room). The
// village store is the countryside main street crossing (-1484.1, 14.6,
// 725.1), on the road graph of both scenes (farm.js); the hardware truck
// parks on the shoulder of the road graph line north out of that crossing
// (x -1484.1, z 733 to 840), 3.6 m east of it, where the scene.4ds ground
// is 14.44 in both scenes. Luigi's stop is the road graph crossing
// (-1781.1, -5.5, -8.2) by Salieri's (as bootleg.js); Morello's is the road
// point (-404.0, 17.0, 440.3) 12 m from its door arrow (lib/maps.js); Paco
// waits at the container docks crossing (lib/maps.js).
//
// Stock, casks, heat and quest progress are kept in the database; casks age
// and heat cools on the server clock while this map is loaded. A batch on
// the still, a load in a car and cases in a thief's hands go back to their
// operation when the map changes or the resource stops.
const maps = require("./lib/maps.js");
const profiles = require("./lib/profiles.js");
const menu = require("./lib/menu.js");
const commands = require("./lib/commands.js");
const jobkey = require("./lib/jobkey.js");
const store = require("./lib/store.js");
const life = require("./lib/life.js");
const interiors = require("./interiors.js");
const {toast, toastAll, card, compass, markers, markersAll, feed, name, text, COLORS} = require("./lib/notify.js");
const {MAIN_WORLD, flatDistance, distance, playable, playerById, vehicleById, speedOf, formatMoney, formatMinutes, headingFrom, emit,
    onStart, onStop, inFreeRide, pick} = require("./lib/util.js");

const MAP_ID = "freeride_extended";
const STORE = "distillery";
const TITLE = "Rum distillery";
const RUM = "#d08a3c";
const LAYER = "side";

/* Places */

// The places around the still are chosen spots in the clearing's open
// ground, each given by its offset from the pot or from a scene2.bin object
// named in the header, at the height of the FREEKRAJINA scene.4ds ground
// mesh ("Object211") sampled there:
// - CASKS: the middle cask prop (cask2), 2.3 m east and 4.6 m north of the
//   pot; ground 14.90.
// - CRATES: between the crate props crate1 and crate3, 3.1 m west and 3.7 m
//   south of the pot; ground 14.70.
// - YARD, where cars stop: the open ground east of the sheds toward the
//   pasture's north corner, 10.4 m south-east of the shed "@bouda00" and
//   14.3 m south of the log pile "klady03", with no scene2.bin object within
//   10 m; ground 15.50.
// - HANK_SPOT: 1.4 m east and 2.8 m south of the pot, 7.6 m from the log
//   pile "klady00"; ground 15.00.
// - REVEAL_CAMERA (below): 5.1 m south of "@bouda00" (ground 15.22), 3.3 m
//   up, looking at the pot 0.8 m above its base.
const POT = {x: -1744.0, y: 14.81, z: 1250.0};
const CASKS = {x: -1741.7, y: 14.9, z: 1254.6};
const CRATES = {x: -1747.1, y: 14.7, z: 1246.3};
const YARD = {x: -1727.5, y: 15.5, z: 1254.0};
const HANK_SPOT = {x: -1742.6, y: 15.0, z: 1247.2};
// Open ground at the clearing's edges, 26 and 26 m from the still.
const RAID_POINTS = [{x: -1715.0, y: 15.69, z: 1272.0}, {x: -1757.0, y: 14.58, z: 1272.0}];
const SAILOR = {x: -1778.47, y: -5.56, z: 19.44};
// Salieri's front door (lib/maps.js), where Luigi takes a bribe for the precinct.
const SALIERI_DOOR = maps.landmark("salieri").position;
const VILLAGE = {x: -1484.1, y: 14.6, z: 725.1};
// On the shoulder north of the store (see the header); its driver smokes
// 2.7 m east of it, on the same ground (14.49).
const TRUCK_SPOT = {x: -1480.5, y: 14.5, z: 764.0};
const DRIVER_SPOT = {x: -1477.8, y: 14.5, z: 762.0};
const REVEAL_CAMERA = {position: {x: -1733.0, y: 18.5, z: 1256.0}, target: {x: -1744.0, y: 15.6, z: 1250.0}};

const PROPS = [
    {id: "pot", model: "xv sud.i3d", position: POT, scale: 1.5},
    {id: "pipe", model: "xv trubka4.i3d", position: {x: -1743.0, y: 15.85, z: 1250.0}},
    {id: "worm", model: "xsud 01.i3d", position: {x: -1742.0, y: 14.99, z: 1250.0}},
    {id: "vat1", model: "tutsud.i3d", position: {x: -1746.4, y: 15.06, z: 1251.0}},
    {id: "vat2", model: "tutsud.i3d", position: {x: -1746.4, y: 15.09, z: 1249.2}},
    {id: "sack1", model: "xpytel2.i3d", position: {x: -1745.0, y: 14.64, z: 1253.4}, yaw: 0.3},
    {id: "sack2", model: "xpytel2.i3d", position: {x: -1745.0, y: 14.85, z: 1253.4}, yaw: -0.25},
    {id: "sack3", model: "xpytel2.i3d", position: {x: -1745.0, y: 14.64, z: 1254.4}, yaw: 0.1},
    {id: "cask1", model: "sud01.i3d", position: {x: -1741.0, y: 15.49, z: 1254.0}},
    {id: "cask2", model: "sud01.i3d", position: {x: -1741.7, y: 15.4, z: 1254.6}},
    {id: "cask3", model: "sud01.i3d", position: {x: -1742.4, y: 15.3, z: 1255.2}},
    {id: "crate1", model: "bedna01.i3d", position: {x: -1747.5, y: 15.0, z: 1246.5}, yaw: 0.2},
    {id: "crate2", model: "bedna01.i3d", position: {x: -1747.5, y: 15.8, z: 1246.5}, yaw: 0.5},
    {id: "crate3", model: "bedna01.i3d", position: {x: -1746.5, y: 15.15, z: 1245.9}, yaw: -0.3},
];
// The burst pot lies on its side; the pipe is gone.
const WRECK = {id: "pot", model: "xv sud.i3d", position: {x: -1744.0, y: 15.29, z: 1250.0}, rotation: {x: 1.45, y: 0.7, z: 0}, scale: 1.5};

// A full car (12 cases) of the best rum: $360 at Luigi's, $540 at the
// speakeasy, $720 and 72 heat at the docks.
const OUTLETS = [
    {id: "salieri", who: "Luigi", name: "Luigi at Salieri's", label: "LUIGI · SALIERI'S", mapLabel: "Luigi (rum)", position: {x: -1781.1, y: -5.5, z: -8.2},
        price: 30, heat: 1, tail: 0, unlock: 4},
    {id: "morello", who: "The speakeasy", name: "the speakeasy at Morello's", label: "MORELLO'S SPEAKEASY", mapLabel: "Speakeasy (rum)",
        position: {x: -404.0, y: 17.0, z: 440.3}, price: 45, heat: 2, tail: 0.35, unlock: 4},
    {id: "docks", who: "Paco", name: "Paco at the docks", label: "PACO · THE DOCKS", mapLabel: "Paco (rum)", position: maps.landmark("docks").position,
        price: 60, heat: 6, tail: 0, unlock: 5, rumour: true},
];

/* Models and voices (retail voice ids, Npc.spawn) */

const HANK = {model: "Homeless01.i3d", voice: 27};
const DRIVER = {model: "Delnik01.i3d", voice: 26};
const DOYLE = {model: "pol01.i3d", voice: 10};
const RIVALS = [{model: "Enemy04K.i3d", weapon: 10, ammo: 50, reserve: 100}, {model: "Enemy12K.i3d", weapon: 6, ammo: 6, reserve: 30},
    {model: "Gangster01.i3d", weapon: 6, ammo: 6, reserve: 30}];
const AGENTS = [{model: "pol01.i3d", weapon: 11, ammo: 8, reserve: 24}, {model: "pol02.i3d", weapon: 6, ammo: 8, reserve: 32},
    {model: "pol01.i3d", weapon: 6, ammo: 8, reserve: 32}];
const RIVAL_VOICE = 14;
const AGENT_VOICE = 11;
// Retail lines (Npc.say): 6-8 a group event, 33-36 ready to fight, 45-50 a
// fight spotted, 66-69 panic, 75-78 giving up, 132-134 a car steal
// refused, 139-144 police halt.
const LINE = {chat: 6, nod: 7, fight: 33, spotted: 45, panic: 66, shrug: 75, robbed: 132, halt: 139};
const SMOKING = "KoureniAutoStativ.i3d";
const TRUCK_MODEL = "truckb00.i3d";

/* Sounds (stock sounds directory) */

const FIRE_LOOP = "08c_ohen.wav";
const WHISTLE = "14b_parnipiskani.wav";
const VENT_SOUND = "15g_lokoplnykotel.wav";
const BLAST = "16b_vybuch.wav";
const POUR = "014a_nalevani.wav";

/* Rules */

const QUEST = {ASK: 0, FIND: 1, COPPER: 2, FIRST_FIRE: 3, FIRST_SALE: 4, DONE: 5};
const QUESTS = [
    {title: "A sailor's tale", step: "Ask Ernesto, the sailor in Salieri's billiard room, about rum"},
    {title: "The woodcutters' clearing", step: "Find Hank's camp in the woods west of the lowland road"},
    {title: "Copper", step: "Unbolt the copper worm off the hardware truck in the village and bring it to Hank"},
    {title: "First fire", step: "Fetch molasses, run the still and bottle your first rum"},
    {title: "Luigi's thirst", step: "Load the cases into a car in the clearing and sell them in town"},
];

const BATCH_MS = 45_000;
const BAND_LOW = 0.45;
const BAND_HIGH = 0.8;
const LOW_SPEED = 0.4;
const RISE_PER_S = 0.055;
const SURGE = 0.16;
const SURGE_MIN_MS = 7000;
const SURGE_MAX_MS = 12_000;
const VENT = 0.22;
const VENT_GAP_MS = 350;
const START_PRESSURE = 0.4;
const AGE_MS = 150_000;
const CASES_PER_CASK = 4;
const MAX_CASKS = 4;
const MAX_CASES = 24;
const MAX_MOLASSES = 6;
const SACKS_PER_RUN = 3;
const MOLASSES_PRICE = 40;
const CAR_CAPACITY = 12;
const BOTTLE_MS = 3000;
const COIL_MS = 4000;
const REPAIR_MS = 4000;
const REPAIR_PRICE = 80;
const COPPER_PAY = 100;
const WRECK_MS = 120_000;
const BANKED_MS = 3 * 60_000;
const STEAL_MS = 8000;
const STEAL_CASES = 4;
const LOAD_MS = 5000;
const UNLOAD_MS = 4000;
const BLAST_RADIUS = 7;
const BLAST_DAMAGE = 60;

const TOUCH = 2.2;
const STILL_LEAVE = 4;
const DISCOVER_RADIUS = 40;
const YARD_RADIUS = 9;
const STOP_RADIUS = 16;
const SAILOR_REACH = 2.2;
const TRUCK_REACH = 4.5;
const DELIVER_COIL = 8;
const CAMP_RADIUS = 60;
const NEAR_CAMP = 300;
const HANK_RANGE = 150;
const RUMOUR_RADIUS = 900;

const HEAT_BATCH = 5;
const HEAT_BLAST = 15;
const HEAT_REFUSED = 10;
const HEAT_DECAY_MS = 20_000;
const HEAT_NERVOUS = 75;
const NERVOUS_PRICE = 0.85;
const HEAT_DOYLE = 55;
// At full heat the revenue men give a member on the street this long to
// reach the still (time to drive there from the docks) or buy them off; a
// member at the camp meets them once the first minute is up.
const RAID_WINDOW_MS = 4.5 * 60_000;
const RAID_PREP_MS = 60_000;
const RAID_RESUME_MS = 2 * 60_000;
const RAID_DEFEND = 250;
const RAID_TIMEOUT_MS = 240_000;
const LOST_GRACE_MS = 30_000;
const ARRIVE = 3;
const BRIBE_REACH = 30;
const BRIBE_GAP_MS = 5 * 60_000;
// An envelope at Salieri's costs 3 x heat (40 points off), Doyle 2 x heat
// (50 points off).
const BRIBE_PER_HEAT = 3;
const BRIBE_MIN = 100;
const DOYLE_PER_HEAT = 2;
const DOYLE_MIN = 80;

const EVENT_ROLL_MS = 60_000;
const RIVAL_CHANCE = 0.15;
const RIVAL_MIN_CASES = 6;
const RIVAL_STEAL = 6;
const DOYLE_CHANCE = 0.25;
const DOYLE_WAIT_MS = 60_000;
const OP_EVENT_GAP_MS = 12 * 60_000;
const WEDDING_CHANCE = 0.3;
const WEDDING_CASES = 4;
const WEDDING_PAY = 260;
const WEDDING_OFFER_MS = 2 * 60_000;
const WEDDING_MS = 8 * 60_000;
const PARTY_EVERY_MS = 15 * 60_000;
const PARTY_MS = 5 * 60_000;
const PARTY_PRICE = 1.5;
const THEFT_GAP_MS = 10 * 60_000;
const THEFT_TOLD_MS = 30_000;
const BLAST_FIRE_MS = 30_000;
// Activities (lib/life.js) that keep a member from answering a raid.
const AWAY = new Set(["duel", "race", "jail", "visit"]);

const XP = {lead: 40, find: 60, copper: 120, batch: 20, bottle: 8, sell: 6, defend: 80, wedding: 60};
const TICK_MS = 250;
const PRESENT_MS = 1000;
const SAVE_MS = 15_000;
const MAX_STEP_MS = 2000;
const NOTICE_MS = 30_000;
const COMPASS_PRIORITY = 46;

/* State */

// player record key -> {name, stage, copper, done}: copper once they brought
// Hank his worm (paid once, and any operation they are in runs the still),
// done once an operation of theirs made its first sale.
const players = new Map();
// operation key ("crew:TAG" or "p:<record key>") -> its stock, heat and progress
const ops = new Map();
// vehicle id -> {kind: "rum" | "molasses", opKey, opName, n, q, rumoured}
const cargo = new Map();
// player id -> {coil?: true, cases?: {opKey, opName, n, q}}
const hands = new Map();
// player id -> {kind, startedAt, vehicleId?, refused?} for the timed holds and car stops
const actions = new Map();
// player id -> {markers, compass, card, prompt}
const shown = new Map();
const notices = new Map();
// player id -> when they were last near the camp; the compass keeps helping a while after.
const lastAtCamp = new Map();
// Players whose compass /rum pointed at Salieri's.
const pointed = new Set();
// Players who put the quest aside with /rum hide: no card, compass or markers
// until /rum, unless they carry something.
const hidden = new Set();
const ENGAGED_MS = 10 * 60_000;
// opKey -> {doyleAt, raidAt, theftAt, toldAt, weddingAt}: cooldowns, not saved
const cooldowns = new Map();
// opKey -> {startedAt, dueAt, pausedAt}: revenue men on their way, not saved
const raidCalls = new Map();
// opKey -> {state: "offered" | "accepted", until}
const weddings = new Map();
const still = {batch: null, wreckedUntil: 0, sound: null, fire: null, blast: null, blastUntil: 0, lastWhistleAt: 0};
const hank = {npc: null, ready: false, idleSince: 0, respawnAt: 0, lastSayAt: 0};
const truck = {vehicleId: null, driver: null, driverReady: false, coilTaken: false, restockAt: 0, idleSince: 0};
let doyle = null;
let raid = null;
let party = {until: 0, nextAt: 0};
let props = new Map();
let propsGeneration = -1;
let lastRumours = "";
let rumourItems = [];
let lastTick = 0;
let lastPresentAt = 0;
let lastEventRollAt = 0;
let lastSaveAt = 0;
let heatClock = 0;
let dirty = false;
let timer = null;

/* Records */

function available() {
    return inFreeRide() && World.getMission() === MAP_ID && typeof MapObject !== "undefined";
}

function npcsAvailable() {
    return typeof Npc !== "undefined" && typeof Npc.spawn === "function";
}

function freshOp(opName) {
    return {name: opName, unlocked: false, progress: QUEST.FIRST_FIRE, molasses: 0, casks: [], cases: {n: 0, q: 0}, heat: 0, sold: 0};
}

function load() {
    const saved = store.load(STORE) ?? {};
    for (const [key, record] of Object.entries(saved.players ?? {})) {
        players.set(key, {name: record.name ?? "", stage: Math.max(0, Math.min(QUEST.COPPER, Number(record.stage) || 0)), copper: Boolean(record.copper),
            done: Boolean(record.done)});
    }
    for (const [key, op] of Object.entries(saved.ops ?? {})) {
        const {raidDueAt, ...kept} = op;
        ops.set(key, {...freshOp(op.name ?? ""), ...kept});
    }
}

function persist() {
    const kept = [...ops].filter(([, op]) => op.unlocked || op.molasses || op.casks.length || op.cases.n || op.heat);
    const started = [...players].filter(([, record]) => record.stage > QUEST.ASK || record.copper);
    store.save(STORE, {players: Object.fromEntries(started), ops: Object.fromEntries(kept)});
    dirty = false;
    lastSaveAt = Date.now();
}

function crewOf(player) {
    return player.state.get("fr.crew") ?? null;
}

function recordOf(player) {
    const key = profiles.keyOf(player);
    if (!key) return null;
    let record = players.get(key);
    if (!record) {
        record = {name: player.nickname, stage: QUEST.ASK, copper: false, done: false};
        players.set(key, record);
    }
    record.name = player.nickname;
    return record;
}

function soloKeyOf(player) {
    const key = profiles.keyOf(player);
    return key ? `p:${key}` : null;
}

// A player who joins a crew brings their own operation into the crew's: its
// loads on the road, its progress and as much of its stock as the crew's
// shed has room for (the rest stays in their own, theirs again if they
// leave). Its heat comes along: hot rum does not cool by changing hands.
function mergeSolo(player, opKey, op) {
    const soloKey = soloKeyOf(player);
    const solo = soloKey ? ops.get(soloKey) : null;
    if (!solo?.unlocked) return;
    for (const entry of cargo.values()) {
        if (entry.opKey !== soloKey) continue;
        entry.opKey = opKey;
        entry.opName = op.name;
    }
    // A batch of theirs on the still finishes into their own casks first.
    if (still.batch?.opKey === soloKey) return;
    const sacks = Math.max(0, Math.min(solo.molasses, MAX_MOLASSES - op.molasses));
    const room = Math.max(0, Math.min(solo.casks.length, MAX_CASKS - op.casks.length));
    const cases = Math.max(0, Math.min(solo.cases.n, MAX_CASES - op.cases.n));
    if (op.unlocked && !sacks && !room && !cases) return;
    op.progress = op.unlocked ? Math.max(op.progress, solo.progress) : solo.progress;
    op.unlocked = true;
    op.molasses += sacks;
    solo.molasses -= sacks;
    const casks = solo.casks.splice(0, room);
    op.casks.push(...casks);
    if (cases) {
        const each = quality(solo.cases);
        op.cases.n += cases;
        op.cases.q += cases * each;
        solo.cases.n -= cases;
        solo.cases.q -= cases * each;
        if (!solo.cases.n) solo.cases.q = 0;
    }
    op.heat = Math.max(op.heat, solo.heat);
    dirty = true;
    const brought = [sacks ? plural(sacks, "sack") : null, casks.length ? plural(casks.length, "cask") : null, cases ? plural(cases, "case") : null].filter(Boolean);
    const left = solo.molasses || solo.casks.length || solo.cases.n;
    tellOp(opKey, `${player.nickname} brought their rum into the crew's still${brought.length ? `: ${brought.join(", ")}` : ""}.` +
        (left ? " The rest stays in their own stock until there is room." : ""), "gold", 8);
}

function opKeyOf(player) {
    const key = profiles.keyOf(player);
    if (!key) return null;
    const tag = crewOf(player)?.tag;
    return tag ? `crew:${tag}` : `p:${key}`;
}

function opOf(player) {
    const opKey = opKeyOf(player);
    if (!opKey) return null;
    let op = ops.get(opKey);
    const crew = crewOf(player);
    const opName = crew?.tag ? `[${crew.tag}]` : player.nickname;
    if (!op) {
        op = freshOp(opName);
        ops.set(opKey, op);
    }
    op.name = opName;
    if (crew?.tag) mergeSolo(player, opKey, op);
    // Whoever once brought Hank his copper runs the still in any operation
    // they are in; the story is not told twice.
    const record = recordOf(player);
    if (record && !op.unlocked && record.copper) {
        op.unlocked = true;
        op.progress = record.done ? QUEST.DONE : QUEST.FIRST_FIRE;
        dirty = true;
    }
    if (record && op.unlocked && op.progress >= QUEST.DONE && !record.done) {
        record.done = true;
        dirty = true;
    }
    return op;
}

function members(opKey) {
    return World.getPlayers().filter((player) => opKeyOf(player) === opKey);
}

function stageOf(player) {
    const op = opOf(player);
    if (op?.unlocked) return Math.max(QUEST.FIRST_FIRE, op.progress);
    return recordOf(player)?.stage ?? QUEST.ASK;
}

// Whether the player knows where the still is: found it, or their crew runs it.
function knows(player) {
    return stageOf(player) >= QUEST.COPPER;
}

function cooldownOf(opKey) {
    let entry = cooldowns.get(opKey);
    if (!entry) {
        entry = {doyleAt: 0, raidAt: 0, theftAt: 0, toldAt: 0, weddingAt: 0};
        cooldowns.set(opKey, entry);
    }
    return entry;
}

function plural(count, word) {
    return `${count} ${word}${count === 1 ? "" : "s"}`;
}

function quality(entry) {
    return entry.n ? Math.round(entry.q / entry.n) : 0;
}

function onStreet(player) {
    return playable(player) && player.virtualWorld === MAIN_WORLD;
}

function near(player, point, reach, level = 3) {
    return flatDistance(player.position, point) <= reach && Math.abs(player.position.y - point.y) <= level;
}

function onFoot(player) {
    return !player.getVehicle();
}

function driving(player) {
    return player.getSeat() === 0 ? player.getVehicle() : null;
}

function wantedLevel(player) {
    return typeof Police !== "undefined" ? Police.getRecord(player)?.level ?? 0 : 0;
}

function setWanted(player, level) {
    if (typeof Police !== "undefined" && wantedLevel(player) < level) Police.setWanted(player, level);
}

// A line of dialogue: the text on the player's screen, the voice in the world.
function speak(player, who, line, npc = null, voiceLine = LINE.chat, seconds = 8) {
    toast(player, `"${line}"`, {title: who, tone: "gold", seconds});
    if (npc?.alive) npc.say(voiceLine);
}

// The prompt line of this module's layer; the hint shown last is forgotten so
// the next hint is sent again.
function setPrompt(player, value, progress = null) {
    const last = shown.get(player.id);
    if (last) last.prompt = undefined;
    jobkey.prompt(player, value, progress, LAYER);
}

function notice(player, kind, message, tone = "gold") {
    const id = `${player.id}:${kind}`;
    if (Date.now() - (notices.get(id) ?? 0) < NOTICE_MS) return;
    notices.set(id, Date.now());
    toast(player, message, {title: TITLE, tone, seconds: 6});
}

function tellOp(opKey, message, tone = "gold", seconds = 7) {
    for (const player of members(opKey)) toast(player, message, {title: TITLE, tone, seconds});
}

function heatWord(heat) {
    if (heat >= 100) return "raid!";
    if (heat >= HEAT_NERVOUS) return "hot";
    if (heat >= 40) return "warm";
    return "cool";
}

function addHeat(opKey, amount) {
    const op = ops.get(opKey);
    if (!op || amount <= 0) return;
    const before = op.heat;
    op.heat = Math.min(100, op.heat + amount);
    dirty = true;
    if (before < 50 && op.heat >= 50) tellOp(opKey, "The revenue men are asking about rum in the lowlands. Lay low or pay someone off (/rum bribe at Salieri's).");
    if (before < HEAT_NERVOUS && op.heat >= HEAT_NERVOUS) tellOp(opKey, "Your rum is hot: the buyers pay less until things cool down.", "bad");
    // At 100 the revenue men set out once a member is on the street (tickRevenue).
}

/* Props */

function createProp(spec) {
    const rotation = spec.rotation ?? {x: 0, y: spec.yaw ?? 0, z: 0};
    const options = {virtualWorld: MAIN_WORLD};
    if (spec.scale) options.scale = spec.scale;
    return MapObject.create(spec.model, spec.position, rotation, options) ?? null;
}

function wrecked(now = Date.now()) {
    return still.wreckedUntil > now;
}

function syncProps() {
    if (!World.isReady()) return;
    const generation = World.getMissionGeneration();
    if (generation !== propsGeneration) {
        // The mission change removed them.
        props = new Map();
        propsGeneration = generation;
    }
    const broken = wrecked();
    for (const spec of PROPS) {
        const want = spec.id === "pot" && broken ? WRECK : spec.id === "pipe" && broken ? null : spec;
        const have = props.get(spec.id);
        if (have && have.spec === want) continue;
        have?.object?.destroy();
        props.delete(spec.id);
        if (want) props.set(spec.id, {spec: want, object: createProp(want)});
    }
}

function clearProps() {
    for (const record of props.values()) record.object?.destroy();
    props = new Map();
}

/* Hank, the truck and its driver */

function hankWanted() {
    return World.getPlayers().some((player) => onStreet(player) && stageOf(player) >= QUEST.FIND && flatDistance(player.position, POT) <= HANK_RANGE);
}

function tickHank(now) {
    if (!npcsAvailable()) return;
    if (hankWanted()) {
        hank.idleSince = 0;
        if (!hank.npc && now >= hank.respawnAt) {
            hank.npc = Npc.spawn(HANK.model, HANK_SPOT, headingFrom(HANK_SPOT, POT), {behaviour: "friendly", voice: HANK.voice, virtualWorld: MAIN_WORLD}) ?? null;
            hank.ready = false;
            if (!hank.npc) hank.respawnAt = now + 30_000;
        }
    } else if (hank.npc) {
        hank.idleSince ||= now;
        if (now - hank.idleSince >= 30_000) {
            hank.npc.remove();
            hank.npc = null;
            hank.ready = false;
        }
    }
}

function hankSays(player, line, voiceLine = LINE.chat, seconds = 8) {
    speak(player, "Hank", line, hank.ready ? hank.npc : null, voiceLine, seconds);
}

// Somebody in town still has to unbolt the copper worm.
function truckWanted() {
    return World.getPlayers().some((player) => onStreet(player) && stageOf(player) === QUEST.COPPER && !hands.get(player.id)?.coil);
}

function removeTruck() {
    const vehicle = vehicleById(truck.vehicleId);
    if (vehicle && !vehicle.getOccupants().some(Boolean)) vehicle.destroy();
    truck.vehicleId = null;
    truck.driver?.remove();
    truck.driver = null;
    truck.driverReady = false;
}

function tickTruck(now) {
    if (!truckWanted()) {
        if (truck.vehicleId !== null || truck.driver) {
            truck.idleSince ||= now;
            if (now - truck.idleSince >= 60_000) removeTruck();
        }
        return;
    }
    truck.idleSince = 0;
    if (truck.coilTaken && now >= truck.restockAt) truck.coilTaken = false;
    if (!vehicleById(truck.vehicleId) && typeof Vehicle !== "undefined") {
        const vehicle = Vehicle.spawn(TRUCK_MODEL, TRUCK_SPOT, 0);
        if (vehicle) {
            vehicle.setEngine(false);
            vehicle.state.set("fr.distillery", "truck");
            truck.vehicleId = vehicle.id;
        }
    }
    if (!truck.driver && npcsAvailable()) {
        truck.driver = Npc.spawn(DRIVER.model, DRIVER_SPOT, headingFrom(DRIVER_SPOT, TRUCK_SPOT), {behaviour: "neutral", voice: DRIVER.voice, virtualWorld: MAIN_WORLD}) ?? null;
        truck.driverReady = false;
    }
}

function truckVehicle() {
    return vehicleById(truck.vehicleId);
}

/* The still */

function batchOf(player) {
    const batch = still.batch;
    return batch && batch.operatorId === player.id ? batch : null;
}

function stopStillEffects() {
    still.sound?.destroy();
    still.sound = null;
    still.fire?.destroy();
    still.fire = null;
}

// The blast's flames, put out a little after they die down.
function clearBlast() {
    still.blast?.destroy();
    still.blast = null;
    still.blastUntil = 0;
}

function startStillEffects() {
    if (!still.sound && typeof Sound !== "undefined") still.sound = Sound.create(FIRE_LOOP, POT, {radius: 25, volume: 0.6});
    if (!still.fire && typeof Fire !== "undefined") {
        still.fire = Fire.create({x: POT.x, y: POT.y, z: POT.z}, {radius: 0.6, height: 0.2, intensity: 0.5, damage: 5, virtualWorld: MAIN_WORLD});
    }
}

function nextSurge(now) {
    return now + SURGE_MIN_MS + Math.random() * (SURGE_MAX_MS - SURGE_MIN_MS);
}

function fireUp(player, op) {
    const opKey = opKeyOf(player);
    const now = Date.now();
    if (still.batch && still.batch.opKey === opKey && still.batch.operatorId === null) {
        still.batch.operatorId = player.id;
        still.batch.pressure = START_PRESSURE;
        still.batch.nextSurgeAt = nextSurge(now);
        startStillEffects();
        toast(player, "You take over the still. Keep the gauge in the green: tap G to vent when it climbs.", {title: TITLE, tone: "gold"});
        return;
    }
    op.molasses--;
    dirty = true;
    still.batch = {opKey, operatorId: player.id, progress: 0, pressure: START_PRESSURE, goodMs: 0, totalMs: 0, nextSurgeAt: nextSurge(now),
        highSince: 0, lastVentAt: 0};
    startStillEffects();
    hankSays(player, "Fire's lit. Watch the gauge: tap G to let off steam when it climbs past the green. Too low and it crawls, too high and she blows.",
        LINE.nod, 9);
}

function vent(player) {
    const batch = batchOf(player);
    const now = Date.now();
    if (!batch || now - batch.lastVentAt < VENT_GAP_MS) return;
    batch.lastVentAt = now;
    batch.pressure = Math.max(0.05, batch.pressure - VENT);
    if (typeof Sound !== "undefined") Sound.play(VENT_SOUND, POT, {radius: 30, volume: 0.5});
}

function bankFire(batch) {
    const operator = playerById(batch.operatorId);
    batch.operatorId = null;
    batch.bankedAt = Date.now();
    stopStillEffects();
    if (operator) {
        setPrompt(operator, null, null);
        toast(operator, "You left the still, so Hank banked the fire. Come back and press G at the still to carry on.", {title: TITLE, tone: "bad", seconds: 6});
    }
}

function blowUp(batch) {
    const operator = playerById(batch.operatorId);
    still.batch = null;
    still.wreckedUntil = Date.now() + WRECK_MS;
    stopStillEffects();
    addHeat(batch.opKey, HEAT_BLAST);
    if (typeof Sound !== "undefined") Sound.play(BLAST, POT, {radius: 200, volume: 1});
    clearBlast();
    if (typeof Fire !== "undefined") {
        still.blast = Fire.create({x: POT.x, y: POT.y, z: POT.z}, {radius: 2.5, height: 2, intensity: 1, damage: 30, lifetime: 25_000, virtualWorld: MAIN_WORLD}) ?? null;
        still.blastUntil = Date.now() + BLAST_FIRE_MS;
    }
    for (const player of World.getPlayers()) {
        if (!onStreet(player)) continue;
        const gap = distance(player.position, POT);
        if (gap > 25) continue;
        emit(player, "fr:rum:flash", {strength: Math.max(0.2, 1 - gap / 25)});
        if (gap <= BLAST_RADIUS) player.setHealth(Math.max(1, player.health - Math.round(BLAST_DAMAGE * (1 - gap / BLAST_RADIUS))));
    }
    if (hank.ready && hank.npc?.alive) hank.npc.say(LINE.panic);
    if (operator) {
        setPrompt(operator, null, null);
        toast(operator, `The still blew! The batch is gone and the whole valley heard it. Hank can patch it in ${formatMinutes(WRECK_MS)}, ` +
            `or hold G at it and pay ${formatMoney(REPAIR_PRICE)} for the parts now.`, {title: TITLE, tone: "bad", seconds: 9});
        feed("rum", [name(operator, COLORS.red), text(" blew up a still in the woods")]);
    }
    syncProps();
}

function finishBatch(batch) {
    const op = ops.get(batch.opKey);
    still.batch = null;
    stopStillEffects();
    const operator = playerById(batch.operatorId);
    if (operator) setPrompt(operator, null, null);
    if (!op) return;
    const grade = Math.max(10, Math.round(batch.goodMs / Math.max(1, batch.totalMs) * 100));
    op.casks.push({q: grade, left: AGE_MS});
    dirty = true;
    addHeat(batch.opKey, HEAT_BATCH);
    if (operator) {
        profiles.addXp(operator, XP.batch, "Distilled rum");
        hankSays(operator, `Runs clear. ${grade >= 80 ? "That's Havana quality." : grade >= 50 ? "Drinkable." : "Rough stuff, but it'll sell."} ` +
            `Into the cask it goes: ${formatMinutes(AGE_MS)} and you can bottle it at the casks.`, LINE.nod);
    }
}

function tickStill(dt, now) {
    if (still.blast && now >= still.blastUntil) clearBlast();
    if (still.wreckedUntil && !wrecked(now)) {
        still.wreckedUntil = 0;
        syncProps();
    }
    const batch = still.batch;
    if (!batch) return;
    if (batch.operatorId === null) {
        // A banked batch nobody comes back to frees the still for others.
        if (now - batch.bankedAt < BANKED_MS) return;
        still.batch = null;
        const op = ops.get(batch.opKey);
        if (op) op.molasses = Math.min(MAX_MOLASSES, op.molasses + 1);
        dirty = true;
        tellOp(batch.opKey, "Nobody came back to the banked still, so Hank tipped the mash back into the sack.");
        return;
    }
    const operator = playerById(batch.operatorId);
    if (!operator || !onStreet(operator) || !onFoot(operator) || !near(operator, POT, STILL_LEAVE)) {
        bankFire(batch);
        return;
    }
    const seconds = dt / 1000;
    batch.pressure += (RISE_PER_S + (Math.random() - 0.4) * 0.03) * seconds;
    if (now >= batch.nextSurgeAt) {
        batch.pressure += SURGE;
        batch.nextSurgeAt = nextSurge(now);
    }
    if (batch.pressure >= 1) {
        blowUp(batch);
        return;
    }
    const good = batch.pressure >= BAND_LOW && batch.pressure <= BAND_HIGH;
    const high = batch.pressure > BAND_HIGH;
    batch.totalMs += dt;
    if (good) batch.goodMs += dt;
    batch.progress += dt / BATCH_MS * (batch.pressure < BAND_LOW ? LOW_SPEED : 1);
    if (high && now - still.lastWhistleAt > 2500 && typeof Sound !== "undefined") {
        still.lastWhistleAt = now;
        Sound.play(WHISTLE, POT, {radius: 60, volume: 0.9});
    }
    const percent = Math.round(batch.pressure * 100);
    const state = high ? "TOO HIGH! TAP G" : good ? "GOOD" : "LOW · LET IT BUILD";
    setPrompt(operator, `PRESSURE ${percent}% · ${state} · KEEP IT 45-80%`, Math.min(1, batch.pressure));
    if (batch.progress >= 1) finishBatch(batch);
}

/* Casks */

function age(dt) {
    for (const [opKey, op] of ops) {
        if (!op.casks.length) continue;
        let ready = 0;
        for (const cask of op.casks) {
            if (cask.left <= 0) continue;
            cask.left -= dt;
            if (cask.left <= 0) ready++;
        }
        dirty = true;
        if (ready) tellOp(opKey, `${ready === 1 ? "A cask has" : `${ready} casks have`} aged. Hold G at the casks in the clearing to bottle it.`, "good");
    }
}

function agedCask(op) {
    return op.casks.findIndex((cask) => cask.left <= 0);
}

function bottle(player, op) {
    const index = agedCask(op);
    const room = Math.max(0, MAX_CASES - op.cases.n);
    if (index < 0 || !room) return;
    const [cask] = op.casks.splice(index, 1);
    const count = Math.min(CASES_PER_CASK, room);
    op.cases.n += count;
    op.cases.q += count * cask.q;
    dirty = true;
    if (typeof Sound !== "undefined") Sound.play(POUR, CASKS, {radius: 20, volume: 0.8});
    profiles.addXp(player, XP.bottle, "Bottled rum");
    if (op.progress === QUEST.FIRST_FIRE) {
        op.progress = QUEST.FIRST_SALE;
        hankSays(player, "Your first rum! Load it into a car out in the clearing: stop it by the shed and sit still. Luigi at Salieri's buys, " +
            "and so does the speakeasy at Morello's, for more and a risk.", LINE.nod, 10);
        profiles.addXp(player, XP.batch, "First rum bottled");
    } else {
        toast(player, `${plural(count, "case")} bottled (quality ${cask.q}%). ${plural(op.cases.n, "case")} wait in the shed.`, {title: TITLE, tone: "good"});
    }
}

/* Cargo */

function priceOf(outlet, entry, op) {
    const heat = op && op.heat >= HEAT_NERVOUS ? NERVOUS_PRICE : 1;
    const boost = outlet.id === "salieri" && party.until > Date.now() ? PARTY_PRICE : 1;
    return Math.round(outlet.price * (0.4 + 0.6 * quality(entry) / 100) * heat * boost);
}

function presentRumours(force = false) {
    const items = [...cargo].filter(([, entry]) => entry.kind === "rum" && entry.rumoured).map(([vehicleId]) => ({id: `r${vehicleId}`, vehicleId,
        label: "RUM", color: COLORS.red, range: 300, near: 80, map: {icon: "triangle", label: "Rum car", size: 6, flash: true}}));
    const payload = JSON.stringify(items);
    if (!force && payload === lastRumours) return;
    lastRumours = payload;
    rumourItems = items;
    markersAll("rum:cargo", items);
}

function returnToOp(entry) {
    const op = ops.get(entry.opKey);
    if (!op) return;
    if (entry.kind === "molasses") op.molasses = Math.min(MAX_MOLASSES, op.molasses + entry.n);
    else {
        op.cases.n += entry.n;
        op.cases.q += entry.q;
    }
    dirty = true;
}

function returnAll() {
    for (const entry of cargo.values()) returnToOp(entry);
    cargo.clear();
    for (const hand of hands.values()) if (hand.cases) returnToOp({kind: "rum", ...hand.cases});
    hands.clear();
    if (still.batch) {
        const op = ops.get(still.batch.opKey);
        if (op) op.molasses = Math.min(MAX_MOLASSES, op.molasses + 1);
        still.batch = null;
    }
    stopStillEffects();
    dirty = true;
}

function buyMolasses(player, vehicle) {
    const op = opOf(player);
    if (!op) return;
    const aboard = cargo.get(vehicle.id);
    if (aboard && aboard.kind !== "molasses") {
        toast(player, "This car carries rum. Unload it first.", {title: TITLE, tone: "bad"});
        return;
    }
    const carried = aboard?.n ?? 0;
    const room = MAX_MOLASSES - op.molasses - carried;
    if (room <= 0) {
        toast(player, `Hank has no room for more than ${MAX_MOLASSES} sacks.`, {title: TITLE, tone: "bad"});
        return;
    }
    if (!player.trySpendMoney(MOLASSES_PRICE)) {
        toast(player, `Molasses costs ${formatMoney(MOLASSES_PRICE)} for ${SACKS_PER_RUN} sacks.`, {title: TITLE, tone: "bad"});
        return;
    }
    const count = Math.min(SACKS_PER_RUN, room);
    cargo.set(vehicle.id, {kind: "molasses", opKey: opKeyOf(player), opName: op.name, n: carried + count, q: 0, rumoured: false});
    toast(player, `${plural(count, "sack")} of molasses in the back. Drive them to the clearing and stop by the shed.`, {title: TITLE, tone: "good"});
}

function tellThief(owner, driver, outlet, total) {
    toast(driver, `${owner.opName}'s rum fetches ${formatMoney(total)}. They will not be pleased.`, {title: "Rum", tone: "good", seconds: 7});
    feed("rum", [name(driver, COLORS.red), text(` sold ${owner.opName}'s rum to ${outlet.name} · `), text(formatMoney(total), COLORS.gold)]);
    for (const member of members(owner.opKey)) {
        toast(member, `${driver.nickname} sold your rum to ${outlet.name}.`, {title: TITLE, tone: "bad", seconds: 8});
    }
}

function sell(driver, vehicle, entry, outlet) {
    const op = ops.get(entry.opKey);
    const each = priceOf(outlet, entry, op);
    const total = each * entry.n;
    cargo.delete(vehicle.id);
    dirty = true;
    driver.giveMoney(total);
    profiles.bump(driver, "rum", entry.n);
    profiles.addXp(driver, XP.sell * entry.n, "Sold rum");
    const own = opKeyOf(driver) === entry.opKey;
    if (!own) {
        // Stolen rum heats up the thief's own still, if they run one.
        const thiefKey = opKeyOf(driver);
        if (ops.get(thiefKey)?.unlocked) addHeat(thiefKey, outlet.heat * entry.n);
        tellThief(entry, driver, outlet, total);
    } else {
        addHeat(entry.opKey, outlet.heat * entry.n);
        if (op) op.sold += entry.n;
        toast(driver, `${outlet.who} takes ${plural(entry.n, "case")}: ${formatMoney(each)} a case, ${formatMoney(total)} (quality ${quality(entry)}%).`,
            {title: TITLE, tone: "good", seconds: 7});
        feed("rum", [name(driver, RUM), text(` sold ${plural(entry.n, "case")} of rum · `), text(formatMoney(total), COLORS.gold)]);
        if (op && op.progress === QUEST.FIRST_SALE) {
            op.progress = QUEST.DONE;
            for (const member of members(entry.opKey)) {
                const record = recordOf(member);
                if (record) record.done = true;
            }
            profiles.addXp(driver, XP.lead, "First rum sold");
            speak(driver, "Ernesto", "Word travels, amigo. My cousin Paco unloads banana boats at the container docks. He pays double for good rum, " +
                "but the whole city watches a load go down there, and anyone can take it off you.", null, LINE.chat, 12);
        }
    }
    if (outlet.tail && Math.random() < outlet.tail) {
        setWanted(driver, 1);
        toast(driver, "A patrolman saw the crates go in. Lose him.", {title: TITLE, tone: "bad"});
    }
    presentRumours();
}

function weddingDelivery(driver, vehicle, entry) {
    const offer = weddings.get(entry.opKey);
    if (!offer || offer.state !== "accepted" || entry.n < WEDDING_CASES || opKeyOf(driver) !== entry.opKey) return false;
    weddings.delete(entry.opKey);
    entry.q -= quality(entry) * WEDDING_CASES;
    entry.n -= WEDDING_CASES;
    if (entry.n <= 0) cargo.delete(vehicle.id);
    driver.giveMoney(WEDDING_PAY);
    profiles.addXp(driver, XP.wedding, "Rum for a wedding");
    dirty = true;
    toast(driver, `The Kowalskis take ${WEDDING_CASES} cases and pay ${formatMoney(WEDDING_PAY)}. The band strikes up.`, {title: TITLE, tone: "good", seconds: 7});
    feed("rum", [name(driver, RUM), text(" brought the rum to a village wedding · "), text(formatMoney(WEDDING_PAY), COLORS.gold)]);
    return true;
}

// The buyers a load's driver is shown: its owners learn of them as the quest
// goes on; anyone else with the load knows where rum sells.
function outletsFor(player, entry) {
    const own = opKeyOf(player) === entry.opKey;
    const stage = stageOf(player);
    return OUTLETS.filter((outlet) => !own || stage >= outlet.unlock);
}

function tickCargo(now) {
    let changed = false;
    for (const [vehicleId, entry] of [...cargo]) {
        const vehicle = vehicleById(vehicleId);
        if (!vehicle) {
            cargo.delete(vehicleId);
            returnToOp(entry);
            changed = true;
            continue;
        }
        if (vehicle.terminalState !== 0) {
            cargo.delete(vehicleId);
            dirty = changed = true;
            tellOp(entry.opKey, entry.kind === "rum" ? "The car carrying your rum is wrecked. The cases are lost." : "The car with your molasses is wrecked.", "bad");
            if (entry.kind === "rum") feed("rum", [text("Rum", RUM), text(` · ${entry.opName}'s load went up with its car`)]);
            continue;
        }
        const driver = vehicle.getDriver();
        if (entry.kind === "rum" && !entry.rumoured && OUTLETS.some((outlet) => outlet.rumour && flatDistance(vehicle.position, outlet.position) <= RUMOUR_RADIUS)) {
            entry.rumoured = changed = true;
            toastAll("A car loaded with Cuban rum is heading for the docks. Whoever drives it in gets paid for the load.", {title: "Rum", tone: "gold", seconds: 7});
            feed("rum", [text("Rum", RUM), text(" · a loaded car is heading for the docks"), ...(driver ? [text(" · "), name(driver, COLORS.white)] : [])]);
        }
        if (entry.kind !== "rum" || !driver || !playable(driver) || speedOf(vehicle) > 2) continue;
        const atVillage = flatDistance(vehicle.position, VILLAGE) <= STOP_RADIUS;
        if (atVillage && weddingDelivery(driver, vehicle, entry)) {
            changed = true;
            continue;
        }
        const outlet = OUTLETS.find((candidate) => flatDistance(vehicle.position, candidate.position) <= STOP_RADIUS);
        if (!outlet) continue;
        if (wantedLevel(driver) > 0) {
            notice(driver, "wanted", `${outlet.who} won't touch the crates with the police on your tail. Lose them first.`, "bad");
            continue;
        }
        sell(driver, vehicle, entry, outlet);
        changed = true;
    }
    if (changed) presentRumours();
    // Stolen cases in a thief's hands go into the first car they drive.
    for (const [id, hand] of hands) {
        if (!hand.cases) continue;
        const player = playerById(id);
        const vehicle = player ? driving(player) : null;
        if (!vehicle || cargo.has(vehicle.id)) continue;
        cargo.set(vehicle.id, {kind: "rum", ...hand.cases, rumoured: true});
        delete hand.cases;
        toast(player, "The cases are in the car. Their owners can see it on the map: sell them fast.", {title: "Rum", tone: "gold"});
        presentRumours();
    }
}

// Cars stopped in the clearing unload molasses or take on cases.
function tickYard(now) {
    for (const player of World.getPlayers()) {
        const pending = actions.get(player.id);
        const vehicle = onStreet(player) ? driving(player) : null;
        const op = vehicle && stageOf(player) >= QUEST.FIRST_FIRE ? opOf(player) : null;
        const opKey = op ? opKeyOf(player) : null;
        const aboard = vehicle ? cargo.get(vehicle.id) : null;
        const inYard = Boolean(op) && flatDistance(vehicle.position, YARD) <= YARD_RADIUS && speedOf(vehicle) < 1.5;
        const unload = inYard && aboard?.kind === "molasses" && aboard.opKey === opKey;
        const load = inYard && !unload && op.cases.n > 0 && (!aboard || (aboard.kind === "rum" && aboard.opKey === opKey && aboard.n < CAR_CAPACITY));
        if (!unload && !load) {
            if (pending?.kind === "yard") {
                actions.delete(player.id);
                setPrompt(player, null, null);
                if (vehicle && pending.vehicleId === vehicle.id) toast(player, "You pulled away too soon.", {title: TITLE, tone: "bad", seconds: 3});
            }
            continue;
        }
        if (pending?.kind !== "yard" || pending.vehicleId !== vehicle.id) {
            actions.set(player.id, {kind: "yard", vehicleId: vehicle.id, startedAt: now});
            continue;
        }
        const needed = unload ? UNLOAD_MS : LOAD_MS;
        const progress = Math.min(1, (now - pending.startedAt) / needed);
        setPrompt(player, unload ? "HANK UNLOADS THE MOLASSES… HOLD STILL" : "LOADING THE RUM… HOLD STILL", progress);
        if (progress < 1) continue;
        actions.delete(player.id);
        setPrompt(player, null, null);
        if (unload) {
            op.molasses = Math.min(MAX_MOLASSES, op.molasses + aboard.n);
            cargo.delete(vehicle.id);
            dirty = true;
            hankSays(player, `${plural(aboard.n, "sack")} of molasses: that's ${op.molasses} ${op.molasses === 1 ? "batch" : "batches"} on the still. ` +
                "Press G there to fire it up.");
            continue;
        }
        const entry = aboard ?? {kind: "rum", opKey, opName: op.name, n: 0, q: 0, rumoured: false};
        const count = Math.min(op.cases.n, CAR_CAPACITY - entry.n);
        const each = quality(op.cases);
        entry.n += count;
        entry.q += each * count;
        op.cases.n -= count;
        op.cases.q -= each * count;
        if (!op.cases.n) op.cases.q = 0;
        cargo.set(vehicle.id, entry);
        dirty = true;
        const offers = outletsFor(player, entry).map((outlet) => `${outlet.who} ${formatMoney(priceOf(outlet, entry, op))}`).join(", ");
        toast(player, `${plural(entry.n, "case")} loaded. A case pays: ${offers}. Buyers won't deal while the police are on you.`,
            {title: TITLE, tone: "gold", seconds: 9});
        if (Math.random() < op.heat / 200) {
            setWanted(player, 1);
            toast(player, "Someone on the road saw the crates go in. The police are looking for your car.", {title: TITLE, tone: "bad"});
        }
    }
}

/* Events: Doyle, raids, weddings, Luigi's party night */

function spawnHostile(spec, point, index, voice) {
    const offset = {x: point.x + (index % 2 ? 1.5 : -1.5), y: point.y + 0.2, z: point.z + index};
    return Npc.spawn(spec.model, offset, headingFrom(point, POT), {behaviour: "hostile", health: 120, weapon: spec.weapon, ammo: spec.ammo,
        reserve: spec.reserve, voice, virtualWorld: MAIN_WORLD}) ?? null;
}

function defendersOf(opKey, reach) {
    return members(opKey).filter((player) => onStreet(player) && flatDistance(player.position, POT) <= reach);
}

function startRaid(kind, opKey) {
    if (raid || !npcsAvailable()) return false;
    const specs = kind === "revenue" ? AGENTS : RIVALS;
    const point = pick(RAID_POINTS);
    const npcs = specs.map((spec, index) => ({npc: spawnHostile(spec, point, index, kind === "revenue" ? AGENT_VOICE : RIVAL_VOICE), ready: false, targetId: null,
        state: "alive"})).filter((entry) => entry.npc);
    if (!npcs.length) return false;
    raid = {kind, opKey, npcs, startedAt: Date.now(), lostSince: 0};
    cooldownOf(opKey).raidAt = Date.now();
    const op = ops.get(opKey);
    if (kind === "revenue") {
        tellOp(opKey, "The revenue men are at your still! Drive them off, or they seize everything stored there.", "bad", 9);
        for (const player of defendersOf(opKey, RAID_DEFEND)) setWanted(player, 1);
        feed("rum", [text("Revenue raid", COLORS.red), text(` · on ${op?.name ?? "someone"}'s still in the lowland woods`)]);
    } else {
        tellOp(opKey, "Morello's boys heard about your rum and came to take it. Defend the cases!", "bad", 9);
    }
    for (const player of defendersOf(opKey, HANK_RANGE)) hankSays(player, kind === "revenue" ? "Revenuers! Grab a gun!" : "Morello's boys! Here they come!", LINE.fight, 5);
    return true;
}

function endRaid(outcome) {
    const current = raid;
    raid = null;
    for (const entry of current.npcs) if (entry.state === "alive") entry.npc.remove();
    const op = ops.get(current.opKey);
    if (!op) return;
    const revenue = current.kind === "revenue";
    if (outcome === "won") {
        if (revenue) op.heat = 60;
        dirty = true;
        for (const player of defendersOf(current.opKey, RAID_DEFEND)) {
            const reward = revenue ? 100 : 150;
            player.giveMoney(reward);
            profiles.addXp(player, XP.defend, "Defended the still");
            toast(player, revenue ? `The revenue men are down. They'll be back with more if you don't lay low. ${formatMoney(reward)} found on them.`
                : `Morello's boys won't try that again. ${formatMoney(reward)} in their pockets.`, {title: TITLE, tone: "good", seconds: 8});
        }
        feed("rum", [text(op.name, RUM), text(revenue ? " fought off a revenue raid" : " drove Morello's boys away from their still")]);
        return;
    }
    if (outcome === "lost") {
        if (revenue) {
            op.casks = [];
            op.cases = {n: 0, q: 0};
            op.molasses = 0;
            op.heat = 30;
            tellOp(current.opKey, "The revenue men smashed the casks and took every case and sack. The heat is off for now.", "bad", 9);
            feed("rum", [text("Revenue raid", COLORS.red), text(` · ${op.name}'s rum was seized`)]);
        } else {
            const taken = Math.min(RIVAL_STEAL, op.cases.n);
            const each = quality(op.cases);
            op.cases.n -= taken;
            op.cases.q -= taken * each;
            if (!op.cases.n) op.cases.q = 0;
            tellOp(current.opKey, `Morello's boys carried off ${plural(taken, "case")} of your rum.`, "bad");
        }
        dirty = true;
        return;
    }
    // Ran out of time: they leave.
    if (revenue) op.heat = Math.max(op.heat - 30, 70);
    tellOp(current.opKey, revenue ? "The revenue men gave up for today." : "Morello's boys gave up and left.");
}

function tickRaid(now) {
    if (!raid) return;
    const alive = raid.npcs.filter((entry) => entry.state === "alive");
    if (!alive.length) {
        // Retired by the host without a fight counts as them leaving.
        endRaid(raid.npcs.some((entry) => entry.state === "dead") ? "won" : "timeout");
        return;
    }
    if (now - raid.startedAt > RAID_TIMEOUT_MS) {
        endRaid("timeout");
        return;
    }
    const defenders = defendersOf(raid.opKey, 45).filter((player) => player.alive);
    for (const entry of alive) {
        if (!entry.ready) continue;
        const target = defenders.map((player) => ({player, gap: distance(player.position, entry.npc.position)})).sort((a, b) => a.gap - b.gap)[0]?.player;
        if (target) {
            if (entry.targetId !== target.id || !entry.npc.getTask()) {
                entry.targetId = target.id;
                entry.npc.attack(target);
                if (Math.random() < 0.3) entry.npc.say(LINE.spotted);
            }
        } else if (entry.targetId !== "crates" || !entry.npc.getTask()) {
            entry.targetId = "crates";
            entry.npc.walkTo(CRATES, {run: true, direct: true, radius: ARRIVE});
        }
    }
    const atCrates = alive.some((entry) => flatDistance(entry.npc.position, CRATES) <= 6);
    if (atCrates && !defenders.length) {
        raid.lostSince ||= now;
        if (now - raid.lostSince >= LOST_GRACE_MS * (raid.kind === "revenue" ? 1 : 0.67)) endRaid("lost");
    } else {
        raid.lostSince = 0;
    }
}

// Members who can answer a raid: on the street of this map, not in an
// interior and not tied up in a duel, a race or the jail.
function freeMembers(opKey) {
    return members(opKey).filter((player) => onStreet(player) && !interiors.insideOf(player) && !AWAY.has(life.activity(player)?.kind));
}

function raidChoice(ms) {
    return `The revenue men are coming for your still in ${formatMinutes(ms)}. Defend it: be at the camp and fight them off. ` +
        "Or buy them off: /rum bribe at Salieri's door (F2 → Jobs). Nobody there in time and they seize every cask, case and sack stored at the camp.";
}

// At full heat the revenue men set out once a member is on the street to be
// warned; the clock stops while none is. They come early for a member
// already at the camp, after the first minute.
function tickRevenue(now) {
    for (const [opKey, op] of ops) {
        if (!op.unlocked || op.heat < 100) {
            raidCalls.delete(opKey);
            continue;
        }
        if (raid?.opKey === opKey) continue;
        const free = freeMembers(opKey);
        const pending = raidCalls.get(opKey);
        if (!pending) {
            if (!free.length) continue;
            raidCalls.set(opKey, {startedAt: now, dueAt: now + RAID_WINDOW_MS, pausedAt: 0});
            tellOp(opKey, raidChoice(RAID_WINDOW_MS), "bad", 12);
            feed("rum", [text("Rum", RUM), text(` · the revenue men are after ${op.name}'s still`)]);
            continue;
        }
        if (!free.length) {
            pending.pausedAt ||= now;
            continue;
        }
        if (pending.pausedAt) {
            const left = Math.max(RAID_RESUME_MS, pending.dueAt - pending.pausedAt);
            pending.dueAt = now + left;
            // And half a minute at least before they come early.
            pending.startedAt = Math.max(pending.startedAt, now - RAID_PREP_MS / 2);
            pending.pausedAt = 0;
            tellOp(opKey, raidChoice(left), "bad", 12);
            continue;
        }
        // One raid at a time: this one waits for the other to end.
        if (raid) continue;
        const defenders = defendersOf(opKey, RAID_DEFEND);
        const early = defenders.length && now - pending.startedAt >= RAID_PREP_MS;
        if (!early && now < pending.dueAt) continue;
        raidCalls.delete(opKey);
        if (!defenders.length) {
            // Nobody came: they take what is stored at the camp.
            raid = {kind: "revenue", opKey, npcs: [], startedAt: now, lostSince: now};
            endRaid("lost");
            continue;
        }
        if (startRaid("revenue", opKey)) continue;
        // No simulation host to send them: they see armed men and call it in.
        op.heat = 70;
        dirty = true;
        for (const player of defenders) setWanted(player, 2);
        tellOp(opKey, "The revenue men saw you were armed and called the police instead. Run!", "bad");
    }
}

function startDoyle(opKey, target) {
    if (doyle || raid || !npcsAvailable()) return;
    const point = pick(RAID_POINTS);
    const npc = Npc.spawn(DOYLE.model, point, headingFrom(point, POT), {behaviour: "neutral", voice: DOYLE.voice, virtualWorld: MAIN_WORLD});
    if (!npc) return;
    const op = ops.get(opKey);
    doyle = {opKey, npc, ready: false, targetId: target.id, price: Math.max(DOYLE_MIN, Math.round(op.heat * DOYLE_PER_HEAT / 10) * 10), startedAt: Date.now(), walkAt: 0, metAt: 0};
    cooldownOf(opKey).doyleAt = Date.now();
    tellOp(opKey, `Sergeant Doyle of the county police is walking up to the camp. He'll want ${formatMoney(doyle.price)} to forget what he saw: ` +
        "press G next to him to pay.", "gold", 9);
}

function endDoyle(paid) {
    const current = doyle;
    doyle = null;
    const op = ops.get(current.opKey);
    if (paid) {
        op.heat = Math.max(0, op.heat - 50);
        dirty = true;
        if (current.npc.alive) current.npc.say(LINE.shrug);
    } else if (op) {
        addHeat(current.opKey, HEAT_REFUSED);
        const target = playerById(current.targetId);
        if (target && onStreet(target)) {
            setWanted(target, 1);
            toast(target, "Doyle wasn't paid, so he's reporting you. The police are on their way.", {title: TITLE, tone: "bad"});
        }
    }
    if (current.npc.alive) {
        current.npc.walkTo(RAID_POINTS[0], {direct: true});
        setTimeout(() => { if (current.npc.alive) current.npc.remove(); }, 15_000);
    }
}

function tickDoyle(now) {
    if (!doyle) return;
    let target = playerById(doyle.targetId);
    if (!target || !onStreet(target) || flatDistance(target.position, POT) > CAMP_RADIUS * 1.5) {
        target = defendersOf(doyle.opKey, CAMP_RADIUS)[0] ?? null;
        if (target) doyle.targetId = target.id;
    }
    if (!target || now - doyle.startedAt > DOYLE_WAIT_MS * 2 || (doyle.metAt && now - doyle.metAt > DOYLE_WAIT_MS)) {
        endDoyle(false);
        return;
    }
    if (!doyle.ready) return;
    const gap = flatDistance(target.position, doyle.npc.position);
    if (gap > ARRIVE + 1 && now >= doyle.walkAt) {
        doyle.walkAt = now + 3000;
        doyle.npc.walkTo(target.position, {direct: true, radius: ARRIVE});
    } else if (gap <= ARRIVE + 1 && !doyle.metAt) {
        doyle.metAt = now;
        doyle.npc.face(target.position);
        speak(target, "Sgt. Doyle", `Smells like Havana out here. ${formatMoney(doyle.price)} and I never walked up this path.`, doyle.npc, LINE.halt);
    }
}

function rollEvents(now) {
    for (const [opKey, op] of ops) {
        if (!op.unlocked) continue;
        const atCamp = defendersOf(opKey, CAMP_RADIUS);
        const wait = cooldownOf(opKey);
        if (atCamp.length && !raid && !doyle) {
            if (op.cases.n >= RIVAL_MIN_CASES && now - wait.raidAt > OP_EVENT_GAP_MS && Math.random() < RIVAL_CHANCE) {
                startRaid("rivals", opKey);
            } else if (op.heat >= HEAT_DOYLE && op.heat < 100 && now - wait.doyleAt > OP_EVENT_GAP_MS && Math.random() < DOYLE_CHANCE) {
                startDoyle(opKey, atCamp[0]);
            }
        }
        if (!weddings.has(opKey) && op.cases.n >= WEDDING_CASES && members(opKey).length && now - wait.weddingAt > OP_EVENT_GAP_MS &&
            Math.random() < WEDDING_CHANCE) {
            wait.weddingAt = now;
            weddings.set(opKey, {state: "offered", until: now + WEDDING_OFFER_MS});
            tellOp(opKey, `The Kowalskis are marrying off their daughter in the village and want ${WEDDING_CASES} cases of rum, ${formatMoney(WEDDING_PAY)}. ` +
                "Type /rum wedding (or F2 → Jobs) to take the order.", "gold", 10);
        }
    }
    for (const [opKey, offer] of [...weddings]) {
        if (now < offer.until) continue;
        weddings.delete(opKey);
        tellOp(opKey, offer.state === "accepted" ? "The wedding went dry: the Kowalskis found someone else." : "The Kowalskis' order went to someone else.", "bad");
    }
    if (now >= party.nextAt) {
        party = {until: now + PARTY_MS, nextAt: now + PARTY_EVERY_MS * (0.8 + Math.random() * 0.4)};
        for (const player of World.getPlayers()) {
            if (stageOf(player) >= QUEST.FIRST_SALE) {
                toast(player, `Party night at Salieri's: Luigi pays half again for rum for the next ${formatMinutes(PARTY_MS)}.`, {title: TITLE, tone: "gold", seconds: 7});
            }
        }
    }
}

/* The job key */

function camp(player) {
    return onStreet(player) && flatDistance(player.position, POT) <= CAMP_RADIUS;
}

// Strangers who know the camp may take cases from an operation with an owner in town.
function theftTarget(player) {
    const own = opKeyOf(player);
    let best = null;
    for (const [opKey, op] of ops) {
        if (opKey === own || op.cases.n <= 0 || !members(opKey).length) continue;
        if (Date.now() - cooldownOf(opKey).theftAt < THEFT_GAP_MS) continue;
        if (!best || op.cases.n > best.op.cases.n) best = {opKey, op};
    }
    return best;
}

// What G does where the player stands, strongest first.
function chooseAction(player) {
    if (!onStreet(player)) return null;
    const stage = stageOf(player);
    const vehicle = driving(player);
    if (vehicle) {
        if (stage >= QUEST.FIRST_FIRE && flatDistance(vehicle.position, VILLAGE) <= STOP_RADIUS && speedOf(vehicle) < 1.5) return {kind: "molasses", vehicle};
        return null;
    }
    if (player.getVehicle()) return null;
    // Tending a batch, G vents it anywhere the operator may stand: nothing
    // else near the still (Doyle's bribe, the casks) takes the press.
    if (batchOf(player) && near(player, POT, STILL_LEAVE)) return {kind: "vent"};
    if (near(player, SAILOR, SAILOR_REACH, 1.5)) return {kind: "sailor"};
    const truckCar = truckVehicle();
    if (stage === QUEST.COPPER && truckCar && !hands.get(player.id)?.coil && flatDistance(player.position, truckCar.position) <= TRUCK_REACH) return {kind: "coil"};
    if (!camp(player)) return null;
    if (doyle && doyle.ready && doyle.opKey === opKeyOf(player) && flatDistance(player.position, doyle.npc.position) <= ARRIVE + 1.5) return {kind: "bribe"};
    if (stage === QUEST.COPPER && hands.get(player.id)?.coil && flatDistance(player.position, POT) <= DELIVER_COIL) return {kind: "deliver"};
    if (near(player, POT, TOUCH)) {
        if (wrecked()) return {kind: "repair"};
        return {kind: "still"};
    }
    if (stage >= QUEST.FIRST_FIRE && near(player, CASKS, TOUCH)) return {kind: "casks"};
    if (knows(player) && near(player, CRATES, TOUCH)) {
        const target = theftTarget(player);
        if (target) return {kind: "steal", target};
    }
    if (hank.ready && hank.npc?.alive && flatDistance(player.position, hank.npc.position) <= TOUCH + 0.5) return {kind: "hank"};
    return null;
}

function stillHint(player) {
    const op = opOf(player);
    const stage = stageOf(player);
    const batch = still.batch;
    if (stage < QUEST.FIRST_FIRE) return knows(player) ? "HANK'S STILL · IT NEEDS A COPPER WORM" : "SOMEBODY'S STILL";
    if (batch && batch.operatorId !== null) return `IN USE · ${(playerById(batch.operatorId)?.nickname ?? "SOMEONE").toUpperCase()}`;
    if (batch && batch.opKey !== opKeyOf(player)) return `${(ops.get(batch.opKey)?.name ?? "SOMEONE").toUpperCase()}'S BATCH IS BANKED`;
    if (batch) return "[G] TAKE OVER THE STILL";
    if (op.casks.length >= MAX_CASKS) return "THE CASKS ARE FULL · BOTTLE ONE FIRST";
    if (!op.molasses) return "NO MOLASSES · BUY SOME AT THE VILLAGE STORE";
    return `[G] FIRE UP THE STILL · 1 OF ${op.molasses} SACKS`;
}

function hintFor(player, action) {
    if (!action) return null;
    switch (action.kind) {
        case "molasses": return `[G] BUY MOLASSES · ${SACKS_PER_RUN} SACKS · ${formatMoney(MOLASSES_PRICE)}`;
        case "sailor": return "[G] TALK TO THE SAILOR";
        case "coil": return truck.coilTaken ? "THE WORM IS GONE · THE DEALER RESTOCKS SOON" : "[G] HOLD TO UNBOLT THE COPPER WORM";
        case "bribe": return `[G] PAY DOYLE ${formatMoney(doyle.price)}`;
        case "deliver": return "[G] GIVE HANK THE COPPER WORM";
        case "repair": return `[G] HOLD · PATCH THE STILL · ${formatMoney(REPAIR_PRICE)}`;
        case "vent": return null;
        case "still": return stillHint(player);
        case "casks": {
            const op = opOf(player);
            if (agedCask(op) >= 0) return op.cases.n >= MAX_CASES ? "THE SHED IS FULL · LOAD A CAR FIRST" : "[G] HOLD TO BOTTLE A CASK";
            const ageing = op.casks.filter((cask) => cask.left > 0);
            return ageing.length ? `AGEING · READY IN ${formatMinutes(Math.min(...ageing.map((cask) => cask.left)))}` : null;
        }
        case "steal": return `[G] HOLD TO STEAL ${action.target.op.name.toUpperCase()}'S RUM`;
        case "hank": return "[G] TALK TO HANK";
        default: return null;
    }
}

function talkToSailor(player) {
    const record = recordOf(player);
    const sailor = npcsAvailable() ? Npc.getInRange(SAILOR, 2).find((npc) => npc.model.toLowerCase() === "sailor03.i3d") ?? null : null;
    sailor?.face(player.position);
    pointed.delete(player.id);
    if (record && record.stage === QUEST.ASK && !opOf(player)?.unlocked) {
        record.stage = QUEST.FIND;
        dirty = true;
        profiles.addXp(player, XP.lead, "A sailor's tale");
        speak(player, "Ernesto", "Rum? Real Cuban rum, not this bathtub poison? There's an old man, Hank, cooks it in the woods west of the lowland road, " +
            "past the fenced pasture where the woodcutters left their logs. Tell him Ernesto sent you.", sailor, LINE.chat, 12);
        toast(player, "Your compass points the way Ernesto described. Hold TAB for the map.", {title: TITLE, tone: "gold", seconds: 6});
        return;
    }
    if (record?.stage === QUEST.FIND) {
        speak(player, "Ernesto", "West of the lowland road, past the fenced pasture, where the woodcutters left their logs. Follow your nose.", sailor, LINE.chat, 8);
        return;
    }
    const lines = ["The sea's no place for an old man. Neither is Lost Heaven.", "Hank still alive? Tell him he owes me a bottle.",
        "My cousin Paco works the container docks. Ask him about rum when you've got some."];
    speak(player, "Ernesto", pick(lines), sailor, LINE.chat, 6);
}

function talkToHank(player) {
    const op = opOf(player);
    const stage = stageOf(player);
    if (stage === QUEST.COPPER) {
        hankSays(player, hands.get(player.id)?.coil ? "That's the worm! Bring it here to the still." :
            "The revenue men smashed my copper worm. A hardware dealer's truck stands in the village north of the store: unbolt the worm off its bed.");
    } else if (still.batch && still.batch.operatorId === null && still.batch.opKey === opKeyOf(player)) {
        hankSays(player, "The fire's banked. Press G at the still and keep the gauge in the green.");
    } else if (agedCask(op) >= 0) {
        hankSays(player, "A cask's ready. Bottle it at the casks.");
    } else if (op.cases.n) {
        hankSays(player, `${plural(op.cases.n, "case")} in the shed. Drive a car into the clearing, stop by the shed and we'll load it.`);
    } else if (!op.molasses) {
        hankSays(player, `No molasses. The village store sells it: stop a car out front and press G. ${formatMoney(MOLASSES_PRICE)} for ${SACKS_PER_RUN} sacks.`);
    } else {
        hankSays(player, `${plural(op.molasses, "sack")} of molasses. Fire up the still whenever you're ready. Heat's ${heatWord(op.heat)}.`);
    }
    if (hank.ready && hank.npc?.alive) hank.npc.face(player.position);
}

function deliverCoil(player) {
    const hand = hands.get(player.id);
    const op = opOf(player);
    const record = recordOf(player);
    if (!op || !record) return;
    delete hand.coil;
    // Paid once per player, whichever operation they are in.
    const first = !record.copper;
    record.copper = true;
    op.unlocked = true;
    op.progress = QUEST.FIRST_FIRE;
    dirty = true;
    if (!first) {
        hankSays(player, "Good copper. The still's yours to run now, you and yours.", LINE.nod);
        return;
    }
    op.molasses = Math.max(op.molasses, 1);
    profiles.addXp(player, XP.copper, "Copper for Hank");
    player.giveMoney(COPPER_PAY);
    hankSays(player, "Ha! Good copper. The still's yours to run now, you and yours. I put a sack of molasses aside: press G at the still to fire it up.",
        LINE.nod, 10);
    feed("rum", [name(player, RUM), text(" started a rum operation somewhere in the woods")]);
}

function startHold(player, kind, extra = {}) {
    actions.set(player.id, {kind, startedAt: Date.now(), ...extra});
}

const HOLDS = {
    coil: {ms: COIL_MS, text: "UNBOLTING THE COPPER WORM…"},
    repair: {ms: REPAIR_MS, text: "PATCHING THE STILL…"},
    casks: {ms: BOTTLE_MS, text: "BOTTLING…"},
    steal: {ms: STEAL_MS, text: "STEALING CASES…"},
};

function finishHold(player, action) {
    if (action.kind === "coil") {
        if (truck.coilTaken) return;
        truck.coilTaken = true;
        truck.restockAt = Date.now() + 60_000;
        hands.set(player.id, {...hands.get(player.id), coil: true});
        setWanted(player, 1);
        if (truck.driverReady && truck.driver?.alive) truck.driver.say(LINE.robbed);
        toast(player, "You have the copper worm! The driver is shouting for the police. Get it to Hank at the still, on foot or in any car.",
            {title: TITLE, tone: "gold", seconds: 8});
        return;
    }
    if (action.kind === "repair") {
        if (!wrecked()) return;
        if (!player.trySpendMoney(REPAIR_PRICE)) {
            toast(player, `The parts cost ${formatMoney(REPAIR_PRICE)}.`, {title: TITLE, tone: "bad"});
            return;
        }
        still.wreckedUntil = 0;
        syncProps();
        hankSays(player, "Good as new. Mind the gauge this time.");
        return;
    }
    if (action.kind === "casks") {
        bottle(player, opOf(player));
        return;
    }
    if (action.kind === "steal") {
        const op = ops.get(action.opKey);
        if (!op || !op.cases.n || !members(action.opKey).length) return;
        // Another thief may have got there first.
        if (Date.now() - cooldownOf(action.opKey).theftAt < THEFT_GAP_MS) {
            toast(player, "Somebody beat you to it: the rest is watched now.", {title: "Rum", tone: "bad"});
            return;
        }
        const count = Math.min(STEAL_CASES, op.cases.n);
        const each = quality(op.cases);
        op.cases.n -= count;
        op.cases.q -= count * each;
        if (!op.cases.n) op.cases.q = 0;
        dirty = true;
        cooldownOf(action.opKey).theftAt = Date.now();
        hands.set(player.id, {...hands.get(player.id), cases: {opKey: action.opKey, opName: op.name, n: count, q: count * each}});
        toast(player, `You carry off ${plural(count, "case")}. Get into a car: they go in the first one you drive.`, {title: "Rum", tone: "gold"});
        tellOp(action.opKey, `${player.nickname} stole ${plural(count, "case")} of your rum! Their car will show on the map.`, "bad", 8);
    }
}

function tickHolds(now) {
    for (const [id, action] of [...actions]) {
        const hold = HOLDS[action.kind];
        if (!hold) continue;
        const player = playerById(id);
        if (!player || !jobkey.isHeld(player) || !onStreet(player) || player.getVehicle()) {
            actions.delete(id);
            if (player) setPrompt(player, null, null);
            continue;
        }
        const current = chooseAction(player);
        if (current?.kind !== action.kind) {
            actions.delete(id);
            setPrompt(player, null, null);
            continue;
        }
        const progress = Math.min(1, (now - action.startedAt) / hold.ms);
        setPrompt(player, hold.text, progress);
        if (progress < 1) continue;
        actions.delete(id);
        setPrompt(player, null, null);
        finishHold(player, action);
    }
}

jobkey.register({
    press(player) {
        if (!available()) return false;
        const action = chooseAction(player);
        if (!action) return false;
        const stage = stageOf(player);
        switch (action.kind) {
            case "molasses":
                buyMolasses(player, action.vehicle);
                return true;
            case "sailor":
                talkToSailor(player);
                return true;
            case "coil":
                if (truck.coilTaken) return true;
                startHold(player, "coil");
                return true;
            case "bribe":
                if (!player.trySpendMoney(doyle.price)) {
                    toast(player, `Doyle wants ${formatMoney(doyle.price)}. You don't have it.`, {title: TITLE, tone: "bad"});
                    return true;
                }
                speak(player, "Sgt. Doyle", "Pleasure doing business. I was never here.", doyle.npc, LINE.shrug, 5);
                endDoyle(true);
                return true;
            case "deliver":
                deliverCoil(player);
                return true;
            case "repair":
                startHold(player, "repair");
                return true;
            case "vent":
                vent(player);
                return true;
            case "still": {
                const op = opOf(player);
                if (stage < QUEST.FIRST_FIRE) {
                    if (knows(player)) hankSays(player, "Not without a copper worm, friend. The village has what we need.");
                    else toast(player, "Somebody runs a still out here. Whoever it is, they're not around. Maybe someone in town knows.", {title: TITLE});
                    return true;
                }
                const batch = still.batch;
                if (batch && (batch.operatorId !== null || batch.opKey !== opKeyOf(player))) return true;
                if (!batch && (op.casks.length >= MAX_CASKS || !op.molasses)) {
                    toast(player, stillHint(player).toLowerCase().replace(/^\w/, (letter) => letter.toUpperCase()) + ".", {title: TITLE, tone: "bad", seconds: 4});
                    return true;
                }
                fireUp(player, op);
                return true;
            }
            case "casks": {
                const op = opOf(player);
                if (agedCask(op) >= 0 && op.cases.n < MAX_CASES) startHold(player, "casks");
                return true;
            }
            case "steal": {
                startHold(player, "steal", {opKey: action.target.opKey});
                // Once in a while, not on every press of a thief's G.
                const wait = cooldownOf(action.target.opKey);
                if (Date.now() - wait.toldAt >= THEFT_TOLD_MS) {
                    wait.toldAt = Date.now();
                    tellOp(action.target.opKey, "Someone is stealing your rum at the still!", "bad", 6);
                }
                return true;
            }
            case "hank":
                talkToHank(player);
                return true;
            default:
                return false;
        }
    },
    release(player) {
        const action = actions.get(player.id);
        if (action && HOLDS[action.kind]) {
            actions.delete(player.id);
            setPrompt(player, null, null);
        }
    },
});

/* Discovery and the quest */

function tickQuest(now) {
    for (const player of World.getPlayers()) {
        if (!onStreet(player)) continue;
        const record = recordOf(player);
        // On foot or stopped: a locked shot of the still is no way to end a drive.
        const car = player.getVehicle();
        const settled = !car || speedOf(car) < 2;
        if (settled && record?.stage === QUEST.FIND && !opOf(player)?.unlocked && flatDistance(player.position, POT) <= DISCOVER_RADIUS) {
            record.stage = QUEST.COPPER;
            dirty = true;
            profiles.addXp(player, XP.find, "Found the still");
            emit(player, "fr:rum:reveal", {...REVEAL_CAMERA, title: "THE STILL IN THE WOODS"});
            setTimeout(() => {
                if (!onStreet(player)) return;
                hankSays(player, "Ernesto sent you? Then you can help. The revenue men smashed my copper worm, and without one this pot makes nothing. " +
                    "A hardware dealer's truck stands in the village, north of the store. Bring me the worm off its bed.", LINE.nod, 12);
            }, 4500);
        }
        // A coil carried into the clearing in a car counts too.
        const vehicle = driving(player);
        if (vehicle && hands.get(player.id)?.coil && stageOf(player) === QUEST.COPPER && flatDistance(vehicle.position, YARD) <= YARD_RADIUS &&
            speedOf(vehicle) < 1.5) {
            deliverCoil(player);
        }
    }
}

/* What each player sees */

function nextStep(player, stage, op) {
    if (stage < QUEST.FIRST_FIRE) {
        if (stage === QUEST.COPPER && hands.get(player.id)?.coil) return "Bring the copper worm to Hank at the still";
        return QUESTS[stage].step;
    }
    const batch = still.batch;
    if (batchOf(player)) return "Tap G to vent: keep the gauge between 45% and 80%";
    if (raidCalls.has(opKeyOf(player))) return "The revenue men are coming: defend the still, or /rum bribe at Salieri's";
    if (batch && batch.operatorId === null && batch.opKey === opKeyOf(player)) return "Your batch is banked: press G at the still";
    if (hands.get(player.id)?.cases) return "Get the stolen cases into a car";
    const entry = cargo.get(driving(player)?.id);
    if (entry?.kind === "molasses") return "Drive the molasses to the clearing and stop by the shed";
    if (entry?.kind === "rum") return weddings.get(entry.opKey)?.state === "accepted" ? "Bring 4 cases to the wedding in the village" : "Drive the rum to a buyer and stop there";
    if (agedCask(op) >= 0) return "A cask is ready: hold G at the casks to bottle it";
    if (op.cases.n) return stage === QUEST.FIRST_SALE ? QUESTS[QUEST.FIRST_SALE].step : "Stop a car by the shed to load the cases";
    if (wrecked()) return "The still is wrecked: patch it or wait for Hank";
    if (op.molasses && op.casks.length < MAX_CASKS) return "Press G at the still to fire it up";
    if (op.casks.length) return "The rum is ageing in the casks";
    return "Fetch molasses: stop a car at the village store and press G";
}

function cardRows(player, stage, op) {
    const rows = [];
    if (stage < QUEST.DONE) rows.push(["Quest", `${stage + 1}/5 · ${QUESTS[Math.min(stage, QUESTS.length - 1)].title}`]);
    if (stage < QUEST.FIRST_FIRE) return rows;
    const batch = batchOf(player);
    if (batch) rows.push(["Batch", `${Math.round(batch.progress * 100)}% · ${Math.round(batch.goodMs / Math.max(1, batch.totalMs) * 100)}% in the green`]);
    rows.push(["Molasses", `${op.molasses}/${MAX_MOLASSES} sacks`]);
    if (op.casks.length) {
        const ready = op.casks.filter((cask) => cask.left <= 0).length;
        const ageing = op.casks.filter((cask) => cask.left > 0);
        rows.push(["Casks", [ready ? `${ready} ready` : null, ageing.length ? `${ageing.length} ageing · ${formatMinutes(Math.min(...ageing.map((cask) => cask.left)))}` : null]
            .filter(Boolean).join(" · ")]);
    }
    if (op.cases.n) rows.push(["Cases", `${op.cases.n} · quality ${quality(op.cases)}%`]);
    rows.push(["Heat", `${Math.round(op.heat)}% · ${heatWord(op.heat)}`]);
    const pending = raidCalls.get(opKeyOf(player));
    if (pending) rows.push(["Revenue men", pending.pausedAt ? "waiting for you" : `in ${formatMinutes(Math.max(0, pending.dueAt - Date.now()))} · defend or bribe`]);
    const offer = weddings.get(opKeyOf(player));
    if (offer) rows.push(["Wedding", `${offer.state === "accepted" ? "deliver" : "offered"} · ${formatMinutes(offer.until - Date.now())}`]);
    if (party.until > Date.now()) rows.push(["Salieri's", `party night · ${formatMinutes(party.until - Date.now())}`]);
    return rows;
}

function objective(player, stage, op) {
    const vehicle = driving(player);
    const entry = vehicle ? cargo.get(vehicle.id) : null;
    if (entry?.kind === "rum") {
        const wedding = weddings.get(entry.opKey)?.state === "accepted" && opKeyOf(player) === entry.opKey;
        const places = wedding ? [{id: "wedding", position: VILLAGE, label: "THE WEDDING", mapLabel: "Wedding"}] :
            outletsFor(player, entry).map((outlet) => ({id: outlet.id, position: outlet.position, label: `${outlet.label} · ${formatMoney(priceOf(outlet, entry, ops.get(entry.opKey)) * entry.n)}`,
                mapLabel: outlet.mapLabel}));
        const nearest = places.map((place) => ({place, gap: flatDistance(place.position, vehicle.position)})).sort((a, b) => a.gap - b.gap)[0]?.place;
        return {target: nearest?.position ?? null, items: places.map((place) => ({id: place.id, position: place.position, label: place.label, color: RUM, primary: true,
            map: {icon: "diamond", label: place.mapLabel, edge: true}}))};
    }
    if (entry?.kind === "molasses") return {target: YARD, items: [yardItem()]};
    if (stage === QUEST.ASK) return {target: pointed.has(player.id) ? SALIERI_DOOR : null, items: []};
    if (stage === QUEST.FIND) return {target: POT, items: []};
    if (stage === QUEST.COPPER) {
        if (hands.get(player.id)?.coil) return {target: POT, items: [stillItem("HANK")]};
        const vehicleId = truck.vehicleId;
        return {target: truckVehicle()?.position ?? TRUCK_SPOT, items: vehicleId === null ? [] : [{id: "truck", vehicleId, label: "COPPER WORM", color: RUM, near: 60,
            map: {icon: "diamond", label: "Hardware truck", edge: true}}]};
    }
    const items = [];
    const inCamp = camp(player);
    let target = null;
    if (hands.get(player.id)?.cases) return {target: null, items};
    if (!op.molasses && !op.casks.length && !op.cases.n && !still.batch) {
        target = VILLAGE;
        items.push({id: "store", position: VILLAGE, label: "MOLASSES", hint: "STOP A CAR · PRESS G", color: RUM, near: 60, map: {icon: "diamond", label: "Molasses", edge: true}});
    } else if (!inCamp) {
        target = op.cases.n ? YARD : POT;
    }
    if (inCamp && !batchOf(player)) {
        if (agedCask(op) >= 0) items.push({id: "casks", position: CASKS, label: "CASKS", color: RUM, model: "sipka.i3d", range: 40, near: 25, map: false});
        else if (op.molasses && !still.batch && !wrecked() && op.casks.length < MAX_CASKS) items.push(stillItem("STILL"));
        if (op.cases.n && !driving(player)) items.push(yardItem());
    }
    return {target, items};
}

function stillItem(label) {
    return {id: "still", position: POT, label, color: RUM, model: "sipka.i3d", range: 40, near: 25, map: false};
}

function yardItem() {
    return {id: "yard", position: YARD, label: "STOP A CAR HERE", color: RUM, range: 80, near: 30, map: false};
}

function present(player, force = false) {
    const last = shown.get(player.id) ?? {markers: null, compass: null, card: false, prompt: undefined};
    if (force) {
        last.markers = null;
        last.compass = null;
    }
    const active = available() && onStreet(player) && Boolean(profiles.keyOf(player));
    const stage = active ? stageOf(player) : QUEST.ASK;
    const op = active ? opOf(player) : null;
    const goal = active ? objective(player, stage, op) : {target: null, items: []};
    const carrying = Boolean(cargo.get(driving(player)?.id)) || Boolean(hands.get(player.id)?.coil || hands.get(player.id)?.cases);
    if (active && flatDistance(player.position, POT) <= NEAR_CAMP) lastAtCamp.set(player.id, Date.now());
    // Once the story is done the compass and objectives help only while the
    // player is at it: near the camp lately, or with something on board.
    const engaged = carrying || (!hidden.has(player.id) && (stage < QUEST.DONE || Date.now() - (lastAtCamp.get(player.id) ?? 0) < ENGAGED_MS));
    if (!engaged) {
        goal.target = null;
        goal.items = [];
    }
    if (active && knows(player)) goal.items.push({id: "camp", position: POT, color: RUM, range: 1, map: {icon: "square", label: "Hank's still", size: 5}});
    const payload = JSON.stringify(goal.items);
    if (payload !== last.markers) {
        last.markers = payload;
        markers(player, "rum", goal.items);
    }
    const aim = JSON.stringify(goal.target);
    if (aim !== last.compass) {
        last.compass = aim;
        compass(player, "rum", COMPASS_PRIORITY, goal.target ? {position: goal.target} : null);
    }
    const relevant = active && stage > QUEST.ASK && (engaged || weddings.has(opKeyOf(player)) || raidCalls.has(opKeyOf(player)));
    if (relevant) {
        card(player, "rum", {title: TITLE, subtitle: nextStep(player, stage, op), rows: cardRows(player, stage, op), tone: "gold",
            progress: batchOf(player)?.progress});
        last.card = true;
    } else if (last.card || force) {
        card(player, "rum", null);
        last.card = false;
    }
    shown.set(player.id, last);
}

function prompts() {
    for (const player of World.getPlayers()) {
        if (actions.has(player.id) || batchOf(player)) continue;
        const hint = available() ? hintFor(player, chooseAction(player)) : null;
        const last = shown.get(player.id) ?? {markers: null, compass: null, card: false, prompt: undefined};
        if (hint === last.prompt) continue;
        last.prompt = hint;
        shown.set(player.id, last);
        jobkey.prompt(player, hint, null, LAYER);
    }
}

/* The clock */

function tick() {
    const now = Date.now();
    const dt = Math.min(MAX_STEP_MS, Math.max(0, now - lastTick));
    lastTick = now;
    if (!available() || !World.isReady()) return;
    syncProps();
    tickStill(dt, now);
    age(dt);
    heatClock += dt;
    while (heatClock >= HEAT_DECAY_MS) {
        heatClock -= HEAT_DECAY_MS;
        for (const op of ops.values()) {
            // At full heat the raid is coming: only a bribe or the raid itself cools it.
            if (op.heat > 0 && op.heat < 100) {
                op.heat = Math.max(0, op.heat - 1);
                dirty = true;
            }
        }
    }
    tickHolds(now);
    tickYard(now);
    tickCargo(now);
    prompts();
    if (now - lastPresentAt >= PRESENT_MS) {
        lastPresentAt = now;
        tickQuest(now);
        tickHank(now);
        tickTruck(now);
        tickRaid(now);
        tickDoyle(now);
        tickRevenue(now);
        for (const player of World.getPlayers()) present(player);
    }
    if (now - lastEventRollAt >= EVENT_ROLL_MS) {
        lastEventRollAt = now;
        rollEvents(now);
    }
    if (dirty && now - lastSaveAt >= SAVE_MS) persist();
}

/* Commands and the menu */

function guide(player) {
    const stage = stageOf(player);
    hidden.delete(player.id);
    if (stage === QUEST.ASK) {
        if (pointed.delete(player.id)) {
            present(player);
            return;
        }
        pointed.add(player.id);
        present(player);
        toast(player, "Word is somebody cooks real Cuban rum out in the woods. Ernesto, the sailor who drinks in Salieri's billiard room, might know. " +
            "Your compass points to Salieri's.", {title: TITLE, tone: "gold", seconds: 9});
        return;
    }
    const last = shown.get(player.id);
    if (last) last.compass = null;
    present(player);
    toast(player, nextStep(player, stage, opOf(player)) + ". Your compass shows the way.", {title: TITLE, tone: "gold", seconds: 7});
}

function bribe(player) {
    const op = opOf(player);
    if (!op?.unlocked) {
        toast(player, "You have nothing to hide yet.", {title: TITLE});
        return;
    }
    if (op.heat < 5) {
        toast(player, "Nobody is asking about you. Save your money.", {title: TITLE});
        return;
    }
    if (!onStreet(player) || flatDistance(player.position, SALIERI_DOOR) > BRIBE_REACH) {
        toast(player, "Luigi at Salieri's knows a man at the precinct. Come to Salieri's bar and ask again.", {title: TITLE, tone: "bad"});
        return;
    }
    const wait = cooldownOf(opKeyOf(player));
    if (Date.now() - (wait.bribeAt ?? 0) < BRIBE_GAP_MS) {
        toast(player, `Luigi's man was paid ${formatMinutes(Date.now() - wait.bribeAt)} ago. Too soon to ask again.`, {title: TITLE, tone: "bad"});
        return;
    }
    const price = Math.max(BRIBE_MIN, Math.round(op.heat * BRIBE_PER_HEAT / 10) * 10);
    if (!player.trySpendMoney(price)) {
        toast(player, `The envelope needs ${formatMoney(price)}.`, {title: TITLE, tone: "bad"});
        return;
    }
    wait.bribeAt = Date.now();
    op.heat = Math.max(0, op.heat - 40);
    const called = raidCalls.delete(opKeyOf(player));
    dirty = true;
    speak(player, "Luigi", called ? "An envelope for the precinct. The revenue men just got new orders: they turn back." :
        "An envelope for the precinct. Consider it forgotten, for a while.", null, LINE.chat, 6);
}

function acceptWedding(player) {
    const opKey = opKeyOf(player);
    const offer = opKey ? weddings.get(opKey) : null;
    if (!offer) {
        toast(player, "There is no wedding order for you right now.", {title: TITLE});
        return;
    }
    if (offer.state === "accepted") {
        toast(player, "Your crew already took the order: bring 4 cases to the village crossing.", {title: TITLE});
        return;
    }
    offer.state = "accepted";
    offer.until = Date.now() + WEDDING_MS;
    tellOp(opKey, `${player.nickname} took the wedding order: ${WEDDING_CASES} cases to the village crossing within ${formatMinutes(WEDDING_MS)}, ${formatMoney(WEDDING_PAY)}.`, "gold", 8);
}

commands.register({name: "rum", aliases: ["distillery", "still"], usage: "/rum [bribe|wedding|hide]",
    help: "The hidden rum distillery: show the way, bribe the revenue men at Salieri's, take a wedding order or put it aside.", category: "City"}, (player, args) => {
    if (!available()) {
        toast(player, "The rum distillery is on Lost Heaven Extended (/map).", {tone: "bad"});
        return;
    }
    const sub = args[0]?.toLowerCase();
    if (sub === "hide") {
        hidden.add(player.id);
        pointed.delete(player.id);
        present(player);
        toast(player, "You put the rum business aside. /rum (or F2 → Jobs) picks it up again.", {title: TITLE});
    } else if (sub === "bribe") bribe(player);
    else if (sub === "wedding" || sub === "accept") acceptWedding(player);
    else guide(player);
});

menu.provide("rum", (player) => {
    if (!available() || !profiles.keyOf(player)) return {available: false};
    const stage = stageOf(player);
    const op = opOf(player);
    return {available: true, stage, quests: QUESTS.length, quest: QUESTS[Math.min(stage, QUESTS.length - 1)].title, step: nextStep(player, stage, op),
        unlocked: Boolean(op?.unlocked), molasses: op?.molasses ?? 0, casks: op?.casks.length ?? 0, ready: op ? op.casks.filter((cask) => cask.left <= 0).length : 0,
        cases: op?.cases.n ?? 0, heat: Math.round(op?.heat ?? 0), heatWord: heatWord(op?.heat ?? 0), wedding: weddings.get(opKeyOf(player))?.state ?? null,
        docks: stage >= QUEST.DONE, hidden: hidden.has(player.id), raid: raidCalls.has(opKeyOf(player)),
        prices: Object.fromEntries(OUTLETS.map((outlet) => [outlet.id, outlet.price]))};
});

/* Lifecycle */

load();

onStart(() => {
    lastTick = Date.now();
    party.nextAt = Date.now() + PARTY_EVERY_MS;
    timer = setInterval(tick, TICK_MS);
});

function dropAll() {
    if (raid) {
        for (const entry of raid.npcs) if (entry.state === "alive") entry.npc.remove();
        raid = null;
    }
    if (doyle?.npc.alive) doyle.npc.remove();
    doyle = null;
    hank.npc?.remove();
    hank.npc = null;
    hank.ready = false;
    removeTruck();
}

onStop(() => {
    clearInterval(timer);
    for (const player of World.getPlayers()) {
        setPrompt(player, null, null);
        card(player, "rum", null);
        compass(player, "rum", COMPASS_PRIORITY, null);
        markers(player, "rum", []);
    }
    dropAll();
    returnAll();
    clearBlast();
    persist();
    clearProps();
});

Events.on("npcSpawn", (npc) => {
    if (hank.npc?.id === npc.id) {
        hank.ready = true;
        npc.playAnimation(SMOKING, {loop: true});
    }
    if (truck.driver?.id === npc.id) {
        truck.driverReady = true;
        npc.playAnimation(SMOKING, {loop: true});
    }
    if (doyle?.npc.id === npc.id) doyle.ready = true;
    const raider = raid?.npcs.find((entry) => entry.npc.id === npc.id);
    if (raider) raider.ready = true;
});

function raidDeath(npc, killer) {
    const raider = raid?.npcs.find((entry) => entry.npc.id === npc.id);
    if (raider) raider.state = "dead";
    if (doyle?.npc.id === npc.id) {
        // Shooting a policeman is worse than not paying him.
        doyle = null;
        const shooter = killer ? World.getPlayers().find((player) => player === killer) : null;
        if (shooter && onStreet(shooter)) setWanted(shooter, 2);
    }
}

function lostNpc(npc) {
    if (hank.npc?.id === npc.id) {
        hank.npc = null;
        hank.ready = false;
        // Hank went to fetch a doctor.
        hank.respawnAt = Date.now() + 60_000;
    }
    if (truck.driver?.id === npc.id) {
        truck.driver = null;
        truck.driverReady = false;
    }
}

Events.on("npcDeath", (npc, killer) => {
    lostNpc(npc);
    raidDeath(npc, killer);
});
Events.on("npcRetire", (npc) => {
    lostNpc(npc);
    const raider = raid?.npcs.find((entry) => entry.npc.id === npc.id);
    if (raider && raider.state === "alive") raider.state = "gone";
    if (doyle?.npc.id === npc.id) doyle = null;
});

Events.on("playerDeath", (player) => {
    const hand = hands.get(player.id);
    if (hand?.coil) toast(player, "You dropped the copper worm. The dealer will have another on the truck soon.", {title: TITLE, tone: "bad"});
    if (hand?.cases) toast(player, "The stolen cases smashed when you went down.", {title: "Rum", tone: "bad"});
    hands.delete(player.id);
    actions.delete(player.id);
    if (still.batch?.operatorId === player.id) bankFire(still.batch);
});

Events.on("playerSpawn", (player) => {
    shown.delete(player.id);
    if (rumourItems.length) markers(player, "rum:cargo", rumourItems);
});

Events.on("playerDisconnect", (player) => {
    const hand = hands.get(player.id);
    if (hand?.cases) returnToOp({kind: "rum", ...hand.cases});
    hands.delete(player.id);
    actions.delete(player.id);
    shown.delete(player.id);
    lastAtCamp.delete(player.id);
    pointed.delete(player.id);
    hidden.delete(player.id);
    if (still.batch?.operatorId === player.id) {
        still.batch.operatorId = null;
        still.batch.bankedAt = Date.now();
        stopStillEffects();
    }
});

Events.on("missionChange", () => {
    for (const player of World.getPlayers()) {
        setPrompt(player, null, null);
        card(player, "rum", null);
        compass(player, "rum", COMPASS_PRIORITY, null);
    }
    returnAll();
    clearBlast();
    // The revenue men set out afresh, with a fresh warning, once the map is back.
    raidCalls.clear();
    // The mission's objects, vehicles and NPCs went with it.
    raid = null;
    doyle = null;
    hank.npc = null;
    hank.ready = false;
    truck.vehicleId = null;
    truck.driver = null;
    truck.driverReady = false;
    props = new Map();
    actions.clear();
    shown.clear();
    weddings.clear();
    lastRumours = "";
    rumourItems = [];
    persist();
});

module.exports = {stageOf, knows};
