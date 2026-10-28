// The stock Free Ride traffic roads (data/roads.js, exported from road.bin by
// tools/export_road_graph.py). Races, starting grids and world event spots
// are built from these points, so every one of them lies on a street.
const graph = require("../data/roads.js");

const crossings = graph.crossings.map(([x, y, z]) => ({x, y, z}));
const adjacency = crossings.map(() => []);
for (const [from, to, length, points] of graph.edges) {
    const road = points.map(([x, y, z]) => ({x, y, z}));
    adjacency[from].push({to, length, points: road});
    adjacency[to].push({to: from, length, points: [...road].reverse()});
}

function flat(a, b) {
    return Math.hypot(a.x - b.x, a.z - b.z);
}

function nearestCrossing(position) {
    let best = 0;
    let bestDistance = Infinity;
    crossings.forEach((crossing, index) => {
        const distance = flat(crossing, position);
        if (distance < bestDistance) {
            best = index;
            bestDistance = distance;
        }
    });
    return best;
}

function shortestPath(from, to) {
    const distance = new Array(crossings.length).fill(Infinity);
    const previous = new Array(crossings.length).fill(-1);
    const done = new Array(crossings.length).fill(false);
    distance[from] = 0;
    for (;;) {
        let current = -1;
        for (let index = 0; index < crossings.length; index++) {
            if (!done[index] && distance[index] < Infinity && (current < 0 || distance[index] < distance[current])) current = index;
        }
        if (current < 0 || current === to) break;
        done[current] = true;
        for (const road of adjacency[current]) {
            const next = distance[current] + road.length;
            if (next < distance[road.to]) {
                distance[road.to] = next;
                previous[road.to] = current;
            }
        }
    }
    if (distance[to] === Infinity) return null;
    const path = [to];
    while (path[0] !== from) path.unshift(previous[path[0]]);
    return path;
}

// The full street line through a list of crossings, road points included.
function polyline(path) {
    const points = [crossings[path[0]]];
    for (let index = 1; index < path.length; index++) {
        const road = adjacency[path[index - 1]].find((candidate) => candidate.to === path[index]);
        if (road) points.push(...road.points);
        points.push(crossings[path[index]]);
    }
    return points.filter((point, index) => index === 0 || flat(point, points[index - 1]) > 0.5);
}

function lineLength(points) {
    let length = 0;
    for (let index = 1; index < points.length; index++) length += flat(points[index], points[index - 1]);
    return length;
}

// A position and travel direction a given distance along a street line.
function pointAlong(points, metres) {
    let left = Math.max(0, metres);
    for (let index = 1; index < points.length; index++) {
        const a = points[index - 1];
        const b = points[index];
        const length = flat(a, b);
        if (left <= length || index === points.length - 1) {
            const t = length > 0 ? Math.min(1, left / length) : 0;
            return {
                position: {x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t},
                direction: length > 0 ? {x: (b.x - a.x) / length, z: (b.z - a.z) / length} : {x: 0, z: 1},
            };
        }
        left -= length;
    }
    return {position: points[0], direction: {x: 0, z: 1}};
}

// Checkpoints roughly every `spacing` metres; the last one is the finish.
function checkpoints(points, spacing = 170, firstAfter = 140) {
    const result = [];
    let travelled = 0;
    let since = spacing - firstAfter;
    for (let index = 1; index < points.length; index++) {
        const step = flat(points[index], points[index - 1]);
        travelled += step;
        since += step;
        if (since >= spacing && index < points.length - 1) {
            result.push(points[index]);
            since = 0;
        }
    }
    const finish = points[points.length - 1];
    if (result.length && flat(result[result.length - 1], finish) < spacing * 0.45) result.pop();
    result.push(finish);
    return result;
}

// Two columns of grid slots facing down the street from the start line.
function gridSlots(points, count) {
    const slots = [];
    for (let slot = 0; slot < count; slot++) {
        const row = Math.floor(slot / 2);
        const {position, direction} = pointAlong(points, 4 + row * 8);
        const side = slot % 2 === 0 ? -2.3 : 2.3;
        slots.push({
            position: {x: position.x + direction.z * side, y: position.y + 0.6, z: position.z - direction.x * side},
            heading: Math.atan2(direction.x, direction.z),
        });
    }
    return slots;
}

// Street route through several waypoints, each snapped to its nearest crossing.
function route(waypoints) {
    const stops = waypoints.map((point) => typeof point === "number" ? point : nearestCrossing(point));
    const path = [stops[0]];
    for (let index = 1; index < stops.length; index++) {
        if (stops[index] === path[path.length - 1]) continue;
        const leg = shortestPath(path[path.length - 1], stops[index]);
        if (!leg) return null;
        path.push(...leg.slice(1));
    }
    return path.length > 1 ? polyline(path) : null;
}

// A random crossing whose distance from `origin` lies in [minimum, maximum].
function crossingAround(origin, minimum, maximum) {
    const candidates = crossings.map((_, index) => index)
        .filter((index) => {
            const distance = flat(crossings[index], origin);
            return distance >= minimum && distance <= maximum;
        });
    if (!candidates.length) return null;
    return candidates[Math.floor(Math.random() * candidates.length)];
}

// A spot on a road and the heading along it, for parking a car or a crate.
function roadside(index) {
    const road = adjacency[index][Math.floor(Math.random() * adjacency[index].length)];
    const line = [crossings[index], ...road.points, crossings[road.to]];
    return pointAlong(line, Math.min(25, lineLength(line) / 2));
}

module.exports = {crossings, nearestCrossing, shortestPath, polyline, lineLength, pointAlong,
    checkpoints, gridSlots, route, crossingAround, roadside};
