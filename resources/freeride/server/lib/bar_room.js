// The back-room bar of stock Lost Heaven. The stock Free Ride scenes have no
// rooms behind Salieri's door (the bar's rooms belong to the story missions'
// own scene.4ds), so this one is built from stock models (MapObject) where
// nobody stumbles on it: 150 m under the fields north of the city, 60 m west
// of the county jail, in its own virtual world. An arrow at Salieri's door
// leads in (lib/maps.js, server/interiors.js).
//
// Local coordinates: x runs along the room, z towards the counter, y up; the
// origin is on the floor in the middle. The room is 18 by 8.1 m and 2.5 m
// high. The models, all from the stock a2.dta:
//  xzed.4ds         a 6 x 2.5 m wall box, finished on both faces: the walls,
//                   and, scaled to 1.05 m high and 0.6 m deep, the counter
//  plotcihly1.4ds   a 12 x 2.7 m plane of bare brick (^PLOT01): the facing
//                   of every wall
//  plotdrev.4ds     the same plane in weathered boards (PRKNA01): the floor
//                   and, scaled, the counter top
//  plotcihly3.4ds   the plane in bare concrete: the ceiling
//  @Rbstol1.4ds     a bar stool, @rstul1.4ds a table and @hqzdl01.4ds a
//                   chair: Salieri's bar furniture of the story missions
//  xskrin 01.4ds    a cupboard: the back bar
//  @flasa2, xflaska 1, 2flask, 9lahvic, Sklenice: bottles and glasses
//  xradio 1.4ds     the radio on the counter, @Rvetr1.4ds the ceiling fan,
//                   @hqsvtl1.4ds the hanging lamps, xpopelnik.4ds ashtrays
//  plotdrev7.4ds    a steel door with a notice: the way out
// The pieces stand upright or lie flat, turned by 0 or a quarter turn.

const ORIGIN = Object.freeze({x: -460, y: -150, z: 1500});
const WORLD = 0x51000000;
const HEIGHT = 2.5;
const ROOM_X = 9;
const ROOM_Z = 4.05;
const QUARTER = Math.PI / 2;
const RETRY_MS = 3000;
const MAX_FAILURES = 10;

const COUNTER = Object.freeze({x: 1, z: 1.95, length: 6.6, height: 1.05});

// Where the staff and the drinkers stand, as in Salieri's bar of the story
// missions: the barman behind the counter facing the room, two men talking,
// one at the end of the counter with a glass, two at a table.
const STAFF = Object.freeze({
    bartender: {position: {x: 1, y: 0.02, z: 3.05}, heading: Math.PI},
    patrons: [
        {position: {x: -2.6, y: 0.02, z: 1.0}, heading: 0.35, slot: "counter"},
        {position: {x: 4.6, y: 0.02, z: -1.9}, heading: -QUARTER, slot: "talk-a"},
        {position: {x: 3.7, y: 0.02, z: -1.9}, heading: QUARTER, slot: "talk-b"},
        // The chairs of the near table, facing it.
        {position: {x: -5.4, y: 0.02, z: -2.2}, heading: -QUARTER, slot: "seat-a"},
        {position: {x: -7.0, y: 0.02, z: -2.2}, heading: QUARTER, slot: "seat-b"},
    ],
});
// Customers order standing by the stools.
const ORDER = Object.freeze({position: {x: 1, y: 0, z: 0.85}, reach: 2.4});
const ARRIVAL = Object.freeze({position: {x: 7.3, y: 0.05, z: -2.9}, heading: -QUARTER});

// A drinker sits on a chair that does not collide, so the host can place
// him on it.
function table(list, x, z, seated = false) {
    // @rstul1 spans x -0.83..0.5 about its origin; the chairs face it from
    // both ends (the chair's back is towards its +x).
    list.push(["@rstul1.i3d", {x: x + 0.165, y: 0.57, z}, 0]);
    list.push(["@hqzdl01.i3d", {x: x + 0.75, y: 0.26, z}, Math.PI, false, null, seated]);
    list.push(["@hqzdl01.i3d", {x: x - 0.85, y: 0.26, z}, 0, false, null, seated]);
    list.push(["xpopelnik.i3d", {x: x + 0.1, y: 0.85, z: z + 0.2}, 0]);
}

// [model, local position, yaw, flat?, scale?, passable?]
function pieces() {
    const list = [];
    for (const z of [-ROOM_Z - 0.12, ROOM_Z + 0.12]) for (const x of [-6, 0, 6]) list.push(["xzed.i3d", {x, y: 0, z}, 0]);
    for (const x of [-ROOM_X - 0.12, ROOM_X + 0.12]) for (const z of [-3, 3]) list.push(["xzed.i3d", {x, y: 0, z}, QUARTER]);
    for (const x of [-6, 6]) for (const z of [-2.7, 0, 2.7]) {
        list.push(["plotdrev.i3d", {x, y: 0, z}, 0, true]);
        list.push(["plotcihly3.i3d", {x, y: HEIGHT, z}, 0, true]);
    }
    // Brick 5 cm in front of every wall; it runs on through the corners and
    // the ceiling where nobody sees it.
    for (const z of [-ROOM_Z + 0.05, ROOM_Z - 0.05]) for (const x of [-6, 6]) list.push(["plotcihly1.i3d", {x, y: 1.35, z}, 0]);
    for (const x of [-ROOM_X + 0.05, ROOM_X - 0.05]) list.push(["plotcihly1.i3d", {x, y: 1.35, z: 0}, QUARTER]);

    // The counter: a wall box cut down to 1.05 x 0.6 m, boards on top.
    list.push(["xzed.i3d", {x: COUNTER.x, y: 0, z: COUNTER.z}, 0, false, {x: COUNTER.length / 6, y: COUNTER.height / 2.5, z: 2.5}]);
    list.push(["plotdrev.i3d", {x: COUNTER.x, y: COUNTER.height + 0.01, z: COUNTER.z}, 0, true, {x: (COUNTER.length + 0.1) / 12, y: 0.8 / 2.7, z: 1}]);
    for (const x of [-1.2, 0, 1.2, 2.4, 3.6]) list.push(["@Rbstol1.i3d", {x, y: 0.73, z: 1.2}, 0]);
    const top = COUNTER.height + 0.02;
    list.push(["@flasa2.i3d", {x: -1.6, y: top, z: 2.05}, 0]);
    list.push(["xflaska 1.i3d", {x: -1.45, y: top, z: 1.9}, 0]);
    list.push(["Sklenice.i3d", {x: 0.4, y: top, z: 1.8}, 0]);
    list.push(["Sklenice.i3d", {x: 1.9, y: top, z: 1.85}, 0]);
    list.push(["9lahvic.i3d", {x: 2.6, y: top + 0.08, z: 2.0}, 0]);
    list.push(["xradio 1.i3d", {x: 4.0, y: top, z: 2.05}, 0]);
    list.push(["xpopelnik.i3d", {x: -0.6, y: top, z: 1.8}, 0]);
    // The back bar, its shelves under bottles.
    for (const x of [-0.6, 2.6]) {
        list.push(["xskrin 01.i3d", {x, y: 1.21, z: ROOM_Z - 0.4}, 0]);
        list.push(["2flask.i3d", {x: x - 0.3, y: 2.35, z: ROOM_Z - 0.45}, 0]);
        list.push(["@flasa2.i3d", {x: x + 0.3, y: 2.35, z: ROOM_Z - 0.45}, 0]);
    }
    table(list, -6.15, -2.2, true);
    table(list, -6.15, 1.6);
    table(list, -2.4, -2.4);
    for (const x of [-5, 0, 5]) list.push(["@hqsvtl1.i3d", {x, y: HEIGHT - 0.55, z: 0.4}, 0]);
    list.push(["@Rvetr1.i3d", {x: -2.5, y: HEIGHT - 0.12, z: -0.6}, 0]);
    list.push(["plotdrev7.i3d", {x: 7.3, y: 1.16, z: -ROOM_Z}, 0]);
    return list;
}

function toWorld(local) {
    return {x: ORIGIN.x + local.x, y: ORIGIN.y + local.y, z: ORIGIN.z + local.z};
}

function toLocal(position) {
    return {x: position.x - ORIGIN.x, y: position.y - ORIGIN.y, z: position.z - ORIGIN.z};
}

const objects = [];
let built = false;
let failures = 0;
let retry = null;

function available() {
    return typeof MapObject !== "undefined" && typeof MapObject.create === "function";
}

function place([model, local, yaw, flat, scale, passable]) {
    const options = {virtualWorld: WORLD, collision: !passable};
    if (scale) options.scale = scale;
    return MapObject.create(model, toWorld(local), {x: flat ? QUARTER : 0, y: yaw, z: 0}, options);
}

function clear() {
    for (const object of objects) object.destroy();
    objects.length = 0;
    built = false;
}

function attempt() {
    retry = null;
    if (built || !available() || failures >= MAX_FAILURES) return;
    if (!World.isReady()) {
        retry = setTimeout(attempt, RETRY_MS);
        return;
    }
    for (const piece of pieces()) {
        const object = place(piece);
        if (!object) {
            clear();
            if (++failures >= MAX_FAILURES) console.error(`[freeride] Could not build the back-room bar: ${piece[0]} was refused`);
            else retry = setTimeout(attempt, RETRY_MS);
            return;
        }
        objects.push(object);
    }
    built = true;
    failures = 0;
}

function build() {
    if (retry !== null) clearTimeout(retry);
    retry = null;
    clear();
    failures = 0;
    attempt();
}

// A mission change has already removed the objects.
function forget() {
    if (retry !== null) clearTimeout(retry);
    retry = null;
    objects.length = 0;
    built = false;
}

function teardown() {
    if (retry !== null) clearTimeout(retry);
    retry = null;
    clear();
}

function inRoom(position, margin = 0) {
    const local = toLocal(position);
    return Math.abs(local.x) <= ROOM_X + margin && Math.abs(local.z) <= ROOM_Z + margin && local.y >= -3 && local.y <= HEIGHT + 3;
}

function pose(entry) {
    return {position: toWorld(entry.position), heading: entry.heading};
}

module.exports = {
    WORLD, available, build, forget, teardown, isBuilt: () => built, toWorld, inRoom,
    arrival: () => pose(ARRIVAL),
    order: () => ({position: toWorld(ORDER.position), reach: ORDER.reach}),
    bartender: () => pose(STAFF.bartender),
    patrons: () => STAFF.patrons.map((entry) => ({...pose(entry), slot: entry.slot})),
    // Every piece in local coordinates, for a layout preview.
    pieces,
};
