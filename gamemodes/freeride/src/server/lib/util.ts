// Small helpers every Free Ride server module shares: geometry, formatting,
// entity lookups and client messaging.
import * as maps from './maps';
import { events } from './lifecycle';

export { RESOURCE, onStart, onStop } from './lifecycle';

export const MAIN_WORLD = 0;

/** A world position, as the API takes and returns them (Mafia is y-up). */
export interface Point {
    x: number;
    y: number;
    z: number;
}

/** A spot and the heading something stands at there. */
export interface Placement {
    position: Point;
    heading: number;
}

/** Whether the loaded mission is one of the maps in lib/maps.ts. */
export function inFreeRide(): boolean {
    return maps.current() !== null;
}

export function distance(a: Point, b: Point): number {
    return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

export function flatDistance(a: Point, b: Point): number {
    return Math.hypot(a.x - b.x, a.z - b.z);
}

export function speedOf(vehicle: Vehicle): number {
    const { x, y, z } = vehicle.velocity;

    return Math.hypot(x, y, z);
}

/** A plain copy of a point, safe to keep and to send as JSON. */
export function plain(point: Point): Point {
    return { x: point.x, y: point.y, z: point.z };
}

export function formatMoney(amount: number): string {
    return `$${Math.round(amount).toLocaleString('en-US')}`;
}

/** m:ss.hh, a race time. */
export function formatDuration(ms: number): string {
    const total = Math.max(0, Math.round(ms / 10));
    const minutes = Math.floor(total / 6000);
    const seconds = Math.floor(total / 100) % 60;
    const hundredths = total % 100;

    return `${minutes}:${String(seconds).padStart(2, '0')}.${String(hundredths).padStart(2, '0')}`;
}

/** m:ss, a countdown. */
export function formatMinutes(ms: number): string {
    const seconds = Math.max(0, Math.ceil(ms / 1000));

    return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
}

/** "1 patient", "2 patients". */
export function plural(count: number, noun: string, many = `${noun}s`): string {
    return `${count} ${count === 1 ? noun : many}`;
}

/** The heading the server spawn APIs take, from a replicated quaternion. */
export function headingOf(rotation: { w: number; x: number; y: number; z: number }): number {
    const { w, x, y, z } = rotation;
    const forwardX = 2 * (x * z + w * y);
    const forwardZ = 1 - 2 * (x * x + y * y);

    return Math.atan2(forwardX, forwardZ);
}

export function headingFrom(from: Point, to: Point): number {
    return Math.atan2(to.x - from.x, to.z - from.z);
}

/** `metres` ahead of a heading (0 faces +z, PI/2 faces +x). */
export function ahead(from: Point, heading: number, metres: number, rise = 0): Point {
    return {
        x: from.x + Math.sin(heading) * metres,
        y: from.y + rise,
        z: from.z + Math.cos(heading) * metres,
    };
}

/** A living player in the shared city world of the current mission. */
export function playable(player: Player | null | undefined): player is Player {
    return Boolean(
        player &&
        player.spawned &&
        player.alive &&
        World.isReady() &&
        inFreeRide() &&
        player.missionGeneration === World.getMissionGeneration(),
    );
}

/** playable(), and in the city's main world rather than an interior. */
export function onStreet(player: Player | null | undefined): player is Player {
    return playable(player) && player.virtualWorld === MAIN_WORLD;
}

// World.getPlayers() and getVehicles() wrap every entity (traffic cars too)
// afresh, and tick handlers look many up by id: one index serves a whole
// synchronous run. An id missing from it is looked up afresh, for an entity
// created earlier in the same run.
let playerIndex: Map<number, Player> | null = null;
let vehicleIndex: Map<number, Vehicle> | null = null;
let indexReset = false;

function dropIndexesLater(): void {
    if (indexReset) {
        return;
    }

    indexReset = true;
    setImmediate(() => {
        playerIndex = null;
        vehicleIndex = null;
        indexReset = false;
    });
}

export function playerById(id: number | null | undefined): Player | null {
    if (id === undefined || id === null) {
        return null;
    }

    if (playerIndex === null) {
        playerIndex = new Map(World.getPlayers().map((player) => [player.id, player]));
        dropIndexesLater();
    }

    return playerIndex.get(id) ?? World.getPlayers().find((player) => player.id === id) ?? null;
}

export function vehicleById(id: number | null | undefined): Vehicle | null {
    if (id === undefined || id === null) {
        return null;
    }

    if (vehicleIndex === null) {
        vehicleIndex = new Map(World.getVehicles().map((vehicle) => [vehicle.id, vehicle]));
        dropIndexesLater();
    }

    return vehicleIndex.get(id) ?? World.getVehicles().find((vehicle) => vehicle.id === id) ?? null;
}

/** Whether the player is still connected (a handle kept from earlier may not be). */
export function connected(player: Player): boolean {
    return playerById(player.id) !== null;
}

/** "#4" picks a player ID; otherwise an exact nickname wins over a unique prefix. */
export function findPlayer(query: unknown): Player | null {
    const text = String(query ?? '')
        .trim()
        .toLowerCase();

    if (!text) {
        return null;
    }

    const players = World.getPlayers();

    if (/^#\d+$/.test(text)) {
        return players.find((player) => player.id === Number(text.slice(1))) ?? null;
    }

    const exact = players.find((player) => player.nickname.toLowerCase() === text);

    if (exact) {
        return exact;
    }

    const matches = players.filter((player) => player.nickname.toLowerCase().startsWith(text));

    return matches.length === 1 ? matches[0] : null;
}

export function emit(player: Player, name: string, data?: unknown): void {
    player.emit(name, JSON.stringify(data ?? {}));
}

export function broadcast(name: string, data?: unknown): void {
    Events.emitAllClients(name, data ?? {});
}

/**
 * Handles a client request. Requests from a connection that is not a player
 * (the simulation host runs the client scripts too) carry no sender and are
 * dropped. The payload is whatever the client sent: validate it.
 */
export function onClient(name: string, handler: (player: Player, data: unknown) => void): void {
    events.onClient(name, (player, data) => {
        if (player instanceof Player) {
            handler(player, data);
        }
    });
}

export function pick<T>(list: readonly T[]): T {
    return list[Math.floor(Math.random() * list.length)];
}
