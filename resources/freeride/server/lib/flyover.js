// The login camera's flight over each map: a closed loop of ground-track
// points (x, z) laid over the map's own roads and river, each lifted above
// the highest geometry near it. The skylines (data/skyline_<map>.js, from
// tools/export_skyline.py) give the highest static geometry of every 32 m
// cell from the mission's scene.4ds, scene2.bin and cache.bin models.
//
// Heights are never guessed: every control point flies CLEARANCE metres above
// the highest cell within REACH of it and of the stretches to its neighbours,
// the loop is resampled every STEP metres along the centripetal Catmull-Rom
// curve (lib/spline.js), and any sample with less than CLEARANCE above the
// cells within REACH of it raises the nearby control points until none
// does. The camera looks at a second loop of the same shape, LOOK_AHEAD
// metres in front and PITCH below, and banks into the turns.
const zlib = require("node:zlib");
const spline = require("./spline.js");

const CELL_EMPTY = -32768;
// Metres above the highest geometry, and how far round a point counts.
const CLEARANCE = 28;
const REACH = 48;
const STEP = 8;
const SPEED = 17;
const LOOK_AHEAD = 150;
const PITCH = 0.24;
const BANK = {gain: 1.0, maxRoll: 0.26, smoothMetres: 60};
const MAX_PASSES = 40;

// Ground tracks per map, in world x and z, in flying order. See
// docs/freeride.md ("Entering Lost Heaven") for the route of each.
const ROUTES = {
    freeride: {skyline: "skyline_freeride.js", track: [
        [-2250, -150], [-1850, 400], [-1300, 520], [-700, 600], [-150, 650], [450, 600], [900, 300],
        [750, -250], [300, -700], [-300, -850], [-900, -900], [-1450, -600], [-2000, -650],
    ]},
    freeride_extended: {skyline: "skyline_freeride_extended.js", track: [
        [-2250, -150], [-2050, 450], [-1650, 850], [-1250, 1350], [-650, 1800], [50, 1850], [600, 1300], [750, 700],
        [900, 300], [750, -250], [300, -700], [-300, -850], [-900, -900], [-1450, -600], [-2000, -650],
    ]},
    freekrajina: {skyline: "skyline_freekrajina.js", track: [
        [-1450, 1000], [-2000, 1500], [-2050, 2150], [-1500, 2650], [-900, 3150], [-150, 3200], [800, 2900],
        [600, 2100], [300, 1400], [-200, 900], [-800, 900],
    ]},
};
// The stock night map is the same city.
const ALIASES = {freeridenoc: "freeride"};

function decode(skyline) {
    const {cell, originX, originZ, cols, rows} = skyline;
    // base64 of zlib-deflated int16 values (see the header of each skyline).
    let raw = Buffer.from(skyline.heights, "base64");
    if (raw.length !== cols * rows * 2) raw = zlib.inflateSync(raw);
    if (raw.length !== cols * rows * 2) throw new Error("skyline size does not match its grid");
    // The highest cell whose square comes within `radius` of (x, z); cells
    // off the grid or without geometry count as nothing.
    function highest(x, z, radius) {
        const c0 = Math.max(0, Math.floor((x - radius - originX) / cell));
        const c1 = Math.min(cols - 1, Math.floor((x + radius - originX) / cell));
        const r0 = Math.max(0, Math.floor((z - radius - originZ) / cell));
        const r1 = Math.min(rows - 1, Math.floor((z + radius - originZ) / cell));
        let best = -Infinity;
        for (let row = r0; row <= r1; row++) {
            for (let col = c0; col <= c1; col++) {
                const value = raw.readInt16LE(2 * (row * cols + col));
                if (value === CELL_EMPTY || value <= best) continue;
                const dx = Math.max(0, Math.abs(x - (originX + (col + 0.5) * cell)) - cell / 2);
                const dz = Math.max(0, Math.abs(z - (originZ + (row + 0.5) * cell)) - cell / 2);
                if (dx * dx + dz * dz <= radius * radius) best = value;
            }
        }
        return best;
    }
    return {highest};
}

// The highest geometry along the straight stretch a to b, within radius.
function highestAlong(sky, a, b, radius) {
    const length = Math.hypot(b.x - a.x, b.z - a.z);
    const steps = Math.max(1, Math.ceil(length / 8));
    let best = -Infinity;
    for (let k = 0; k <= steps; k++) {
        best = Math.max(best, sky.highest(a.x + (b.x - a.x) * k / steps, a.z + (b.z - a.z) * k / steps, radius));
    }
    return best;
}

function targetsFor(points) {
    const n = points.length;
    return points.map((point, i) => {
        const before = points[(i - 1 + n) % n];
        const after = points[(i + 1) % n];
        const length = Math.hypot(after.x - before.x, after.z - before.z) || 1;
        return {x: point.x + (after.x - before.x) / length * LOOK_AHEAD, y: point.y - LOOK_AHEAD * Math.tan(PITCH),
            z: point.z + (after.z - before.z) / length * LOOK_AHEAD};
    });
}

// Builds and checks one map's flight. Throws when the track cannot clear
// its skyline, which would be a data error.
function build(route, skyline) {
    const sky = decode(skyline);
    const flat = route.track.map(([x, z]) => ({x, z}));
    const n = flat.length;
    if (n < 4) throw new Error("a flight needs at least four points");
    const floor = flat.map((point, i) => Math.max(
        highestAlong(sky, flat[(i - 1 + n) % n], point, REACH),
        highestAlong(sky, point, flat[(i + 1) % n], REACH)));
    if (floor.some((value) => !Number.isFinite(value))) throw new Error("a flight point is off the map");
    const required = floor.map((value) => value + CLEARANCE);
    let heights = [...required];
    let path = null;
    let worst = null;
    for (let pass = 0; pass < MAX_PASSES; pass++) {
        // Smooth by raising only, so no point drops below what it needs.
        for (let k = 0; k < 6; k++) {
            heights = heights.map((y, i) => Math.max(required[i], 0.5 * y + 0.25 * (heights[(i - 1 + n) % n] + heights[(i + 1) % n])));
        }
        const points = flat.map((point, i) => ({x: point.x, y: heights[i], z: point.z}));
        path = spline.resample(points, targetsFor(points), STEP);
        worst = null;
        for (const sample of path.samples) {
            const {x, y, z} = sample.position;
            const margin = y - sky.highest(x, z, REACH);
            if (margin < CLEARANCE && (!worst || margin < worst.margin)) worst = {margin, s: sample.s};
        }
        if (!worst) break;
        // Lift the two control points either side of the lowest sample.
        const i = Math.floor(worst.s) % n;
        for (const j of [i, (i + 1) % n]) required[j] += CLEARANCE - worst.margin + 1;
    }
    if (worst) throw new Error(`a flight sample stays ${worst.margin.toFixed(1)} m above the skyline`);
    const roll = spline.bank(path, {speed: SPEED, ...BANK});
    return {path, roll, heights, floor};
}

const cache = new Map();

function routeOf(mapId) {
    return ROUTES[ALIASES[mapId] ?? mapId] ?? null;
}

// What the client plays: a flat table, see client/selector.js flightFrom.
function flightFor(mapId) {
    const id = ALIASES[mapId] ?? mapId;
    if (cache.has(id)) return cache.get(id);
    const route = routeOf(id);
    let flight = null;
    if (route?.track.length) {
        try {
            const {path, roll} = build(route, require(`../data/${route.skyline}`));
            const samples = [];
            path.samples.forEach(({position: p, target: t}, i) => {
                samples.push(+p.x.toFixed(1), +p.y.toFixed(1), +p.z.toFixed(1), +t.x.toFixed(1), +t.y.toFixed(1), +t.z.toFixed(1), +roll[i].toFixed(3));
            });
            flight = {step: path.step, speed: SPEED, samples};
        } catch (error) {
            console.error(`[freeride] No login flight for ${id}: ${error.message}`);
        }
    }
    cache.set(id, flight);
    return flight;
}

module.exports = {flightFor, build, decode, routeOf, ROUTES, CLEARANCE, REACH, STEP, SPEED, BANK};
