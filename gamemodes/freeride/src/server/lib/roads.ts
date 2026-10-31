// Each map's traffic roads (data/roads_<map>.js, exported from its road.bin by
// tools/export_road_graph.py). Races, starting grids, jobs and world event
// spots are built from these points, so every one of them lies on a street.
import * as maps from './maps';
import { flatDistance as flat, type Placement, type Point } from './util';
import type { RoadGraph } from '../data/types';

/** A travel direction on the ground, unit length. */
export interface Direction {
    x: number;
    z: number;
}

/** A point on a street and the way along it. */
export interface RoadSpot {
    position: Point;
    direction: Direction;
}

/** The nearest point on a road, `gap` metres from where the search began. */
export interface NearestRoadPoint extends RoadSpot {
    gap: number;
}

/** A road between two crossings, as one street line including both ends. */
export interface RoadLine {
    from: number;
    to: number;
    length: number;
    line: Point[];
}

interface Road {
    to: number;
    length: number;
    /** The points between the crossings, in travel order. */
    points: Point[];
}

/** One map's road network. Crossings are numbered by their index. */
export interface Roads {
    crossings: Point[];
    lines: RoadLine[];
    nearestCrossing(position: Point): number;
    nearestRoadPoint(position: Point): NearestRoadPoint | null;
    /** The crossings from one to the other along the shortest way, or null when none leads there. */
    shortestPath(from: number, to: number): number[] | null;
    /** The full street line through a list of crossings, road points included. */
    polyline(path: readonly number[]): Point[];
    /** Street route through several waypoints (crossings, or points snapped to their nearest one). */
    route(waypoints: readonly (number | Point)[]): Point[] | null;
    /** A random crossing whose distance from `origin` lies in [minimum, maximum]. */
    crossingAround(origin: Point, minimum: number, maximum: number): number | null;
    roadside(index: number, random?: () => number): RoadSpot;
    roadPoint(random?: () => number): RoadSpot;
    lineLength: typeof lineLength;
    pointAlong: typeof pointAlong;
    checkpoints: typeof checkpoints;
    gridSlots: typeof gridSlots;
}

export function lineLength(points: readonly Point[]): number {
    let length = 0;

    for (let index = 1; index < points.length; index++) {
        length += flat(points[index], points[index - 1]);
    }

    return length;
}

/** A position and travel direction a given distance along a street line. */
export function pointAlong(points: readonly Point[], metres: number): RoadSpot {
    let left = Math.max(0, metres);

    for (let index = 1; index < points.length; index++) {
        const a = points[index - 1];
        const b = points[index];
        const length = flat(a, b);

        if (left <= length || index === points.length - 1) {
            const t = length > 0 ? Math.min(1, left / length) : 0;

            return {
                position: {
                    x: a.x + (b.x - a.x) * t,
                    y: a.y + (b.y - a.y) * t,
                    z: a.z + (b.z - a.z) * t,
                },
                direction:
                    length > 0
                        ? { x: (b.x - a.x) / length, z: (b.z - a.z) / length }
                        : { x: 0, z: 1 },
            };
        }

        left -= length;
    }

    return { position: points[0], direction: { x: 0, z: 1 } };
}

/** Checkpoints roughly every `spacing` metres; the last one is the finish. */
export function checkpoints(points: readonly Point[], spacing = 170, firstAfter = 140): Point[] {
    const result: Point[] = [];
    let since = spacing - firstAfter;

    for (let index = 1; index < points.length; index++) {
        since += flat(points[index], points[index - 1]);

        if (since >= spacing && index < points.length - 1) {
            result.push(points[index]);
            since = 0;
        }
    }

    const finish = points[points.length - 1];

    if (result.length && flat(result[result.length - 1], finish) < spacing * 0.45) {
        result.pop();
    }

    result.push(finish);

    return result;
}

/** Two columns of grid slots facing down the street from the start line. */
export function gridSlots(points: readonly Point[], count: number): Placement[] {
    const slots: Placement[] = [];

    for (let slot = 0; slot < count; slot++) {
        const row = Math.floor(slot / 2);
        const { position, direction } = pointAlong(points, 4 + row * 8);
        const side = slot % 2 === 0 ? -2.3 : 2.3;

        slots.push({
            position: {
                x: position.x + direction.z * side,
                y: position.y + 0.6,
                z: position.z - direction.x * side,
            },
            heading: Math.atan2(direction.x, direction.z),
        });
    }

    return slots;
}

function build(data: RoadGraph): Roads {
    const crossings = data.crossings.map(([x, y, z]) => ({ x, y, z }));
    const adjacency: Road[][] = crossings.map(() => []);
    const lines: RoadLine[] = [];

    for (const [from, to, length, points] of data.edges) {
        const road = points.map(([x, y, z]) => ({ x, y, z }));

        adjacency[from].push({ to, length, points: road });
        adjacency[to].push({ to: from, length, points: [...road].reverse() });
        lines.push({ from, to, length, line: [crossings[from], ...road, crossings[to]] });
    }

    function nearestCrossing(position: Point): number {
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

    function shortestPath(from: number, to: number): number[] | null {
        const distance = new Array<number>(crossings.length).fill(Infinity);
        const previous = new Array<number>(crossings.length).fill(-1);
        const done = new Array<boolean>(crossings.length).fill(false);

        distance[from] = 0;

        for (;;) {
            let current = -1;

            for (let index = 0; index < crossings.length; index++) {
                if (
                    !done[index] &&
                    distance[index] < Infinity &&
                    (current < 0 || distance[index] < distance[current])
                ) {
                    current = index;
                }
            }

            if (current < 0 || current === to) {
                break;
            }

            done[current] = true;

            for (const road of adjacency[current]) {
                const next = distance[current] + road.length;

                if (next < distance[road.to]) {
                    distance[road.to] = next;
                    previous[road.to] = current;
                }
            }
        }

        if (distance[to] === Infinity) {
            return null;
        }

        const path = [to];

        while (path[0] !== from) {
            path.unshift(previous[path[0]]);
        }

        return path;
    }

    function polyline(path: readonly number[]): Point[] {
        const points = [crossings[path[0]]];

        for (let index = 1; index < path.length; index++) {
            const road = adjacency[path[index - 1]].find(
                (candidate) => candidate.to === path[index],
            );

            if (road) {
                points.push(...road.points);
            }

            points.push(crossings[path[index]]);
        }

        return points.filter((point, index) => index === 0 || flat(point, points[index - 1]) > 0.5);
    }

    function route(waypoints: readonly (number | Point)[]): Point[] | null {
        const stops = waypoints.map((point) =>
            typeof point === 'number' ? point : nearestCrossing(point),
        );
        const path = [stops[0]];

        for (let index = 1; index < stops.length; index++) {
            if (stops[index] === path[path.length - 1]) {
                continue;
            }

            const leg = shortestPath(path[path.length - 1], stops[index]);

            if (!leg) {
                return null;
            }

            path.push(...leg.slice(1));
        }

        return path.length > 1 ? polyline(path) : null;
    }

    function crossingAround(origin: Point, minimum: number, maximum: number): number | null {
        const candidates = crossings
            .map((_, index) => index)
            .filter((index) => {
                const distance = flat(crossings[index], origin);

                return distance >= minimum && distance <= maximum;
            });

        if (!candidates.length) {
            return null;
        }

        return candidates[Math.floor(Math.random() * candidates.length)];
    }

    // A spot on a road and the heading along it, for parking a car or a crate.
    // `random` picks the road; pass a seeded one for repeatable spots.
    function roadside(index: number, random = Math.random): RoadSpot {
        const road = adjacency[index][Math.floor(random() * adjacency[index].length)];
        const line = [crossings[index], ...road.points, crossings[road.to]];

        return pointAlong(line, Math.min(25, lineLength(line) / 2));
    }

    // Any point along any road, longer roads more likely; for maps whose
    // long country roads have few crossings.
    function roadPoint(random = Math.random): RoadSpot {
        const total = lines.reduce((sum, road) => sum + road.length, 0);
        let pick = random() * total;
        const road =
            lines.find((candidate) => (pick -= candidate.length) <= 0) ?? lines[lines.length - 1];

        return pointAlong(road.line, random() * lineLength(road.line));
    }

    // The closest point on any road, and the direction along it.
    function nearestRoadPoint(position: Point): NearestRoadPoint | null {
        let best: NearestRoadPoint | null = null;

        for (const road of lines) {
            for (let index = 1; index < road.line.length; index++) {
                const a = road.line[index - 1];
                const b = road.line[index];
                const dx = b.x - a.x;
                const dz = b.z - a.z;
                const span = dx * dx + dz * dz;
                const t =
                    span > 0
                        ? Math.max(
                              0,
                              Math.min(
                                  1,
                                  ((position.x - a.x) * dx + (position.z - a.z) * dz) / span,
                              ),
                          )
                        : 0;
                const point = { x: a.x + dx * t, y: a.y + (b.y - a.y) * t, z: a.z + dz * t };
                const gap = flat(point, position);

                if (!best || gap < best.gap) {
                    const length = Math.sqrt(span) || 1;

                    best = { gap, position: point, direction: { x: dx / length, z: dz / length } };
                }
            }
        }

        return best;
    }

    return {
        crossings,
        lines,
        nearestCrossing,
        nearestRoadPoint,
        shortestPath,
        polyline,
        route,
        crossingAround,
        roadside,
        roadPoint,
        lineLength,
        pointAlong,
        checkpoints,
        gridSlots,
    };
}

const graphs = new Map<maps.MapId, Roads>();

/** The road network of a map entry from lib/maps.ts. */
export function forMap(map: maps.MapDefinition): Roads;
export function forMap(map: maps.MapDefinition | null | undefined): Roads | null;
export function forMap(map: maps.MapDefinition | null | undefined): Roads | null {
    if (!map) {
        return null;
    }

    let graph = graphs.get(map.id);

    if (!graph) {
        graph = build(map.roads);
        graphs.set(map.id, graph);
    }

    return graph;
}

/** The current mission's roads; stock Lost Heaven's outside Free Ride. */
export function current(): Roads {
    return forMap(maps.current()) ?? forMap(maps.MAPS.freeride);
}
