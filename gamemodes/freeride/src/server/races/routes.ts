// The route book: each map's built-in routes and circuit races, the routes
// players recorded, and the record time on each. Recorded routes and records
// persist between sessions.
import * as store from '../lib/store';
import * as roads from '../lib/roads';
import * as maps from '../lib/maps';
import * as log from '../lib/log';
import { flatDistance, type Placement, type Point } from '../lib/util';
import { Delay } from '../lib/timers';

export const MAX_RACERS = 12;
const SAVE_DELAY_MS = 1000;

export interface Route {
    id: string;
    name: string;
    description: string;
    builtIn: boolean;
    /** Who recorded it; null for a built-in one. */
    author: string | null;
    /** Put every entrant on the grid in the same car (see lobby.ts). */
    circuit?: boolean;
    laps?: number;
    checkpoints: Point[];
    grid: Placement[];
    start: Point;
    length: number;
}

/** A route a player recorded, as the route book keeps it. */
export interface StoredRoute {
    id: string;
    name: string;
    author: string;
    start: Placement;
    checkpoints: Point[];
    /** The map it was recorded on; none on routes recorded before maps existed. */
    mission?: string;
    created: number;
}

export interface RaceRecord {
    ms: number;
    name: string;
    at: number;
}

interface RacesDocument {
    version: number;
    routes?: StoredRoute[];
    records?: Record<string, RaceRecord>;
}

// Each map lists its built-in routes in lib/maps.ts as landmark waypoints;
// the street line between them is the map's road graph's shortest path.
const routesByMap = new Map<string, Route[]>();

/** The routes players recorded, on every map. */
export const customRoutes: StoredRoute[] = [];
let records: Record<string, RaceRecord> = {};
const saveDelay = new Delay({ runOnStop: true });

export function slug(value: string): string {
    return value
        .toLowerCase()
        .replace(/[^a-z0-9]+/g, '-')
        .replace(/^-|-$/g, '');
}

/** A map with an official race track (lib/maps.ts). */
export type CircuitMap = maps.MapDefinition & { circuit: maps.Circuit };

export function hasCircuit(map: maps.MapDefinition | null | undefined): map is CircuitMap {
    return Boolean(map?.circuit);
}

// A race of `laps` laps on the map's circuit, for the route book and the
// championship.
export function circuitRoute(
    map: CircuitMap,
    laps: number,
    name: string,
    description: string,
    id = `b:${map.id}:circuit-${laps}`,
): Route {
    const { lap, grid, heading } = map.circuit;
    let length = 0;

    for (let index = 0; index < lap.length; index++) {
        length += flatDistance(lap[index], lap[(index + 1) % lap.length]);
    }

    const slots = grid.map((point) => ({
        position: { x: point.x, y: point.y + 0.6, z: point.z },
        heading,
    }));

    return {
        id,
        name,
        description,
        builtIn: true,
        author: null,
        circuit: true,
        laps,
        checkpoints: Array.from({ length: laps }, () => lap).flat(),
        grid: slots,
        start: slots[0].position,
        length: length * laps,
    };
}

function builtIn(map: maps.MapDefinition): Route[] {
    const cached = routesByMap.get(map.id);

    if (cached) {
        return cached;
    }

    const graph = roads.forMap(map);
    const built: Route[] = [];

    if (hasCircuit(map)) {
        built.push(
            circuitRoute(
                map,
                1,
                'Autodrom Sprint',
                'One lap of the official circuit, in identical cars.',
            ),
        );

        built.push(
            circuitRoute(
                map,
                3,
                'Autodrom Grand Prix',
                'Three laps of the official circuit, in identical cars.',
            ),
        );
    }

    for (const spec of map.routes) {
        const line = graph.route(spec.via.map((key) => maps.landmark(key).position));

        if (!line) {
            log.error(`Race route ${spec.name} has no street path on ${map.id}`);
            continue;
        }

        built.push({
            id: `b:${map.id}:${spec.id}`,
            name: spec.name,
            description: spec.description,
            builtIn: true,
            author: null,
            checkpoints: roads.checkpoints(line),
            grid: roads.gridSlots(line, MAX_RACERS),
            start: line[0],
            length: roads.lineLength(line),
        });
    }

    routesByMap.set(map.id, built);

    return built;
}

/** The length of a line from `start` through every point, along the ground. */
export function pathLength(start: Point, points: readonly Point[]): number {
    let length = 0;
    let previous = start;

    for (const point of points) {
        length += flatDistance(previous, point);
        previous = point;
    }

    return length;
}

// A recorded route starts where its author stood. The grid lines up behind
// that spot on the road the author drove in on, facing the way the car faced.
function customRoute(stored: StoredRoute): Route {
    const start = stored.start.position;
    const dx = Math.sin(stored.start.heading);
    const dz = Math.cos(stored.start.heading);
    const grid: Placement[] = [];

    for (let slot = 0; slot < MAX_RACERS; slot++) {
        const row = Math.floor(slot / 2);
        const side = slot % 2 === 0 ? -2.3 : 2.3;

        grid.push({
            position: {
                x: start.x - dx * row * 8 + dz * side,
                y: start.y + 0.6,
                z: start.z - dz * row * 8 - dx * side,
            },
            heading: stored.start.heading,
        });
    }

    return {
        id: `c:${stored.id}`,
        name: stored.name,
        description: `Recorded by ${stored.author}.`,
        builtIn: false,
        author: stored.author,
        checkpoints: stored.checkpoints,
        grid,
        start,
        length: pathLength(start, stored.checkpoints),
    };
}

// The routes of the loaded map; routes recorded before maps existed belong to
// stock Lost Heaven.
export function allRoutes(): Route[] {
    const map = maps.current();

    if (!map) {
        return [];
    }

    return [
        ...builtIn(map),
        ...customRoutes
            .filter((route) => (route.mission ?? 'freeride') === map.id)
            .map(customRoute),
    ];
}

export function load(): void {
    const stored = store.load<RacesDocument>('races');

    customRoutes.splice(
        0,
        customRoutes.length,
        ...(Array.isArray(stored?.routes) ? stored.routes : []),
    );

    records = stored?.records && typeof stored.records === 'object' ? stored.records : {};
}

// Gathered over SAVE_DELAY_MS; the resource's stop writes a save still
// waiting (lib/timers.ts), so a restart keeps the records and routes.
export function save(): void {
    saveDelay.start(SAVE_DELAY_MS, () => {
        store.save('races', {
            version: 1,
            routes: customRoutes,
            records,
        } satisfies RacesDocument);
    });
}

export function recordOf(routeId: string): RaceRecord | null {
    return records[routeId] ?? null;
}

export function setRecord(routeId: string, record: RaceRecord): void {
    records[routeId] = record;
}

export function deleteRecord(routeId: string): void {
    delete records[routeId];
}

/** A route by its number in /race list, its id or its name (a name's start will do). */
export function findRoute(query: unknown): Route | null {
    const list = allRoutes();
    const wanted = String(query ?? '')
        .trim()
        .toLowerCase();

    if (!wanted) {
        return null;
    }

    if (/^\d+$/.test(wanted)) {
        return list[Number(wanted) - 1] ?? null;
    }

    return (
        list.find(
            (route) => route.id.toLowerCase() === wanted || route.name.toLowerCase() === wanted,
        ) ??
        list.find((route) => slug(route.name).startsWith(slug(wanted))) ??
        null
    );
}

export function lengthText(route: Route): string {
    return `${(route.length / 1000).toFixed(1)} km`;
}
