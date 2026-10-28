// The county jail: a cell block built from stock models (MapObject) where
// nobody can stumble on it. It stands 150 m under the empty fields north of
// Lost Heaven, more than 500 m from the nearest road on every map, and exists
// only in its own virtual world, so players on the street never receive it.
//
// Local coordinates: x runs along the corridor, z toward the cells, y up; the
// origin is on the floor in the middle of the hall. The hall is 24 by 8.1 m
// and 2.5 m high; three cells of 7.24 by 3 m line its north side, the
// warden's desk stands at the west end, and the east end of the corridor is
// the visiting area, with a bench and the way out.
//
// The models, all from the stock a2.dta, so every map has them:
//  xzed.4ds        a 6 x 2.5 m wall box painted white over a green dado
//                  (0XEX1), finished on both faces: the walls
//  plotdrev.4ds    a 12 x 2.7 m double-sided plane of weathered boards
//                  (PRKNA01): the floor
//  plotcihly3.4ds  the same plane in bare concrete (^BETZED1): the ceiling;
//                  plotcihly1 (bare brick, ^PLOT01) and plotcihly4 (stained
//                  plaster, ^PLOT03) face the end walls
//  plot08.4ds      a 3.62 x 3.6 m panel of steel bars (colour-keyed 0STADPL):
//                  each cell front is one fixed panel and one sliding door
//  xpostel 3.4ds   a plank bunk hung from the wall on chains, and
//  xv zachod1.4ds  a bare toilet, both furniture of the stock prison scene
//                  MISE17-VEZENI, with its xv spina2 floor stains
//  2zarc.4ds       the bare bulb on a cord from Norman's cell (02NORMANCELA)
//  stulprac.4ds    a steel office desk, and xzidle 01.4ds a chair: the post
//  xlavicka.4ds    a wooden bench, and xbojler.4ds a boiler: the visiting area
//  plotdrev7.4ds   a steel door with a notice: the way out
// No stock model is a plain tiled floor on its own: the tiled floors in
// models/ belong to whole rooms or buildings (mor2, 9dum4), and the prison
// scene's own floor is part of its mission scene.4ds, which map objects
// cannot load. Every piece is turned by 0 or a quarter turn about y, or laid
// flat, and is symmetric about its own origin where the turn's sign would
// matter.
const {distance} = require("./util.js");

const ORIGIN = Object.freeze({x: -400, y: -150, z: 1500});
const WORLD = 0x50000000;
const HEIGHT = 2.5;
const HALL_X = 12;
const HALL_Z = 4.05;
const CELL_FRONT = 1.05;
const CELL_WIDTH = 7.24;
const PANEL = 3.62;
// The bar panel is 3.6 m tall; it stands 1.13 m into the floor so its top
// rail sits just under the ceiling.
const BARS_Y = HEIGHT - 1.8 - 0.03;
// A door runs on the corridor side of the fixed panel it slides over.
const DOOR_Z = CELL_FRONT - 0.14;
const DOOR_SPEED = 1.5;
const QUARTER = Math.PI / 2;
const RETRY_MS = 3000;
const MAX_FAILURES = 10;

const WALL = "xzed.i3d";
const SLAB = "plotcihly3.i3d";
const BOARDS = "plotdrev.i3d";
const BARS = "plot08.i3d";

function cellAt(index) {
    const x0 = -HALL_X + 1.14 + index * CELL_WIDTH;
    return {
        index, x0, x1: x0 + CELL_WIDTH,
        spawn: {position: {x: x0 + 4.2, y: 0.05, z: 2.4}, heading: Math.PI},
        door: {closed: {x: x0 + PANEL * 1.5, y: BARS_Y, z: DOOR_Z}, open: {x: x0 + PANEL / 2, y: BARS_Y, z: DOOR_Z}},
        // Where the warden stands to work the lock, in the corridor.
        lock: {x: x0 + PANEL * 1.5, y: 0, z: CELL_FRONT - 1.3},
    };
}
const CELLS = [0, 1, 2].map(cellAt);
const POST = {position: {x: -9, y: 0, z: -2.1}, heading: 0};
const EXIT = {x: 10, y: 0, z: -3.2};
// Where a visitor arrives, facing down the corridor.
const VISIT = {position: {x: 6.5, y: 0.05, z: -2.0}, heading: -QUARTER};

// [model, local position, yaw, flat?]
function pieces() {
    const list = [];
    for (const z of [-HALL_Z - 0.12, HALL_Z + 0.12]) for (const x of [-9, -3, 3, 9]) list.push([WALL, {x, y: 0, z}, 0]);
    for (const x of [-HALL_X - 0.12, HALL_X + 0.12]) for (const z of [-3, 3]) list.push([WALL, {x, y: 0, z}, QUARTER]);
    for (const x of [-6, 6]) for (const z of [-2.7, 0, 2.7]) {
        list.push([BOARDS, {x, y: 0, z}, 0, true]);
        list.push([SLAB, {x, y: HEIGHT, z}, 0, true]);
    }
    // Facings 5 cm in front of the end walls; they run on through the long
    // walls and the ceiling where nobody sees them.
    list.push(["plotcihly4.i3d", {x: -HALL_X + 0.05, y: 1.35, z: 0}, QUARTER]);
    list.push(["plotcihly1.i3d", {x: HALL_X - 0.05, y: 1.35, z: 0}, QUARTER]);
    // The ends of the cell row: short walls closing the corners, and the
    // partitions, which run on through the back wall where nobody sees them.
    for (const x of [-HALL_X - 1.86, HALL_X + 1.86]) list.push([WALL, {x, y: 0, z: CELL_FRONT}, 0]);
    for (const cell of CELLS) {
        list.push([WALL, {x: cell.x0, y: 0, z: HALL_Z}, QUARTER]);
        list.push([BARS, {x: cell.x0 + PANEL / 2, y: BARS_Y, z: CELL_FRONT}, 0]);
        list.push(["xpostel 3.i3d", {x: cell.x0 + 0.92, y: 0.5, z: 2.6}, 0]);
        list.push(["xv zachod1.i3d", {x: cell.x1 - 0.6, y: 0.33, z: 3.6}, 0]);
        list.push(["xv spina2.i3d", {x: cell.x0 + 5.4, y: 0.01, z: 3.0}, 0]);
        list.push(["2zarc.i3d", {x: cell.x0 + PANEL, y: HEIGHT - 0.41, z: 2.6}, 0]);
    }
    for (const x of [-6, 0, 6]) list.push(["2zarc.i3d", {x, y: HEIGHT - 0.41, z: -1.5}, 0]);
    list.push([WALL, {x: CELLS[CELLS.length - 1].x1, y: 0, z: HALL_Z}, QUARTER]);
    list.push(["stulprac.i3d", {x: -9, y: 0.85, z: -3.3}, 0]);
    list.push(["xzidle 01.i3d", {x: -7.2, y: 0.53, z: -3.4}, 0]);
    list.push(["plotdrev7.i3d", {x: 10.7, y: 1.16, z: -HALL_Z}, 0]);
    list.push(["xlavicka.i3d", {x: 6.5, y: 0, z: -HALL_Z + 0.25}, 0]);
    list.push(["xbojler.i3d", {x: 11.5, y: 0, z: 0.3}, 0]);
    list.push(["xv spina2.i3d", {x: 2, y: 0.01, z: -2.2}, 0]);
    return list;
}

function toWorld(local) {
    return {x: ORIGIN.x + local.x, y: ORIGIN.y + local.y, z: ORIGIN.z + local.z};
}

function toLocal(position) {
    return {x: position.x - ORIGIN.x, y: position.y - ORIGIN.y, z: position.z - ORIGIN.z};
}

const objects = [];
const doors = new Map();
let built = false;
let failures = 0;
let retry = null;

function available() {
    return typeof MapObject !== "undefined" && typeof MapObject.create === "function";
}

// Hanging bulbs and floor stains are decoration: walking through them is right.
const PASSABLE = new Set(["2zarc.i3d", "xv spina2.i3d"]);

function place(model, local, yaw, flat) {
    return MapObject.create(model, toWorld(local), {x: flat ? QUARTER : 0, y: yaw, z: 0}, {virtualWorld: WORLD, collision: !PASSABLE.has(model)});
}

function clear() {
    for (const object of objects) object.destroy();
    objects.length = 0;
    doors.clear();
    built = false;
}

function attempt() {
    retry = null;
    if (built || !available() || failures >= MAX_FAILURES) return;
    if (!World.isReady()) {
        retry = setTimeout(attempt, RETRY_MS);
        return;
    }
    const doorPieces = CELLS.map((cell) => [BARS, cell.door.closed, 0]);
    for (const [model, local, yaw, flat] of [...pieces(), ...doorPieces]) {
        const object = place(model, local, yaw, flat);
        if (!object) {
            clear();
            if (++failures >= MAX_FAILURES) console.error(`[freeride] Could not build the jail: ${model} was refused`);
            else retry = setTimeout(attempt, RETRY_MS);
            return;
        }
        objects.push(object);
    }
    // The doors are the last pieces, in cell order.
    const first = objects.length - CELLS.length;
    for (const cell of CELLS) doors.set(cell.index, {object: objects[first + cell.index], open: false, since: 0});
    built = true;
    failures = 0;
}

// Builds the jail for the loaded mission once every client can receive it.
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
    doors.clear();
    built = false;
}

function teardown() {
    if (retry !== null) clearTimeout(retry);
    retry = null;
    clear();
}

function setDoor(cell, open) {
    const door = doors.get(cell.index);
    if (!door || door.open === open) return false;
    door.open = open;
    door.since = Date.now();
    door.object.moveTo(toWorld(open ? cell.door.open : cell.door.closed), DOOR_SPEED);
    return true;
}

function doorOpen(cell) {
    return doors.get(cell.index)?.open ?? false;
}

function doorOpenFor(cell) {
    const door = doors.get(cell.index);
    return door?.open ? Date.now() - door.since : 0;
}

// Inside the cell, behind its bars; margin widens the box.
function inCell(cell, position, margin = 0) {
    const local = toLocal(position);
    return local.x >= cell.x0 - margin && local.x <= cell.x1 + margin && local.z >= CELL_FRONT - margin && local.z <= HALL_Z + margin &&
        Math.abs(local.y) <= 3;
}

function inHall(position, margin = 0) {
    const local = toLocal(position);
    return Math.abs(local.x) <= HALL_X + margin && Math.abs(local.z) <= HALL_Z + margin && local.y >= -3 && local.y <= HEIGHT + 3;
}

function atExit(position, reach) {
    return distance(position, toWorld(EXIT)) <= reach;
}

module.exports = {
    WORLD, CELLS, available, build, forget, teardown, isBuilt: () => built, toWorld, inCell, inHall, atExit,
    openDoor: (cell) => setDoor(cell, true), closeDoor: (cell) => setDoor(cell, false), doorOpen, doorOpenFor,
    spawnOf: (cell) => ({position: toWorld(cell.spawn.position), heading: cell.spawn.heading}),
    lockOf: (cell) => toWorld(cell.lock),
    post: () => ({position: toWorld(POST.position), heading: POST.heading}),
    visit: () => ({position: toWorld(VISIT.position), heading: VISIT.heading}),
    inAnyCell: (position, margin = 0) => CELLS.some((cell) => inCell(cell, position, margin)),
    exit: () => toWorld(EXIT),
    // Every piece, doors included, in local coordinates (for a layout preview).
    pieces: () => [...pieces(), ...CELLS.map((cell) => [BARS, cell.door.closed, 0])],
};
