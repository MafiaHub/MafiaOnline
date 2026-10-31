// The races under way and the racers in them, and taking a racer out of one.
import * as life from '../lib/life';
import { card, compass, markers, toast, COLORS, type Marker } from '../lib/notify';
import { emit, connected, type Placement } from '../lib/util';
import { destroyWhenEmpty } from '../lib/vehicles';
import type { Route } from './routes';

// Circuit racers go back to where they were this long after their race.
const CIRCUIT_RETURN_MS = 10_000;

export interface Racer {
    player: Player;
    /** The next checkpoint, an index into the route's. */
    cp: number;
    /** The time from the start to the finish line; null while still racing. */
    finishedMs: number | null;
    dnf: boolean;
    /** When the racer left the driver's seat, if they are out of it. */
    outOfCarSince: number | null;
    slot: Placement | null;
    /** Where a circuit took the racer from, until they are sent back. */
    returnTo: Placement | null;
    /** The car a circuit race gave the racer. */
    raceCarId: number | null;
}

/** A racer across the line, as a race's onConclude receives them. */
export interface Finisher {
    player: Player;
    ms: number;
}

export type ConcludeHandler = (finishers: Finisher[], entrants: number) => void;

export interface Race {
    id: number;
    route: Route;
    /** Who opened it, or the title of the event that did. */
    host: string;
    generation: number;
    phase: 'lobby' | 'countdown' | 'running';
    /** By player id. */
    racers: Map<number, Racer>;
    lobbyMs: number;
    lobbyEndsAt: number;
    /** When the lights go green (set at the countdown). */
    startedAt: number;
    firstFinishAt: number;
    lastStateAt: number;
    /** How many took the start. */
    entrants: number;
    onConclude: ConcludeHandler | null;
}

export const races = new Map<number, Race>();

// A finished racer stays in the standings until the race is paid out but is
// free to do something else; only an unfinished entry counts as racing.
export function raceOf(player: Player): Race | null {
    for (const race of races.values()) {
        if (race.racers.get(player.id)?.finishedMs === null) {
            return race;
        }
    }

    return null;
}

export function openLobby(): Race | null {
    return [...races.values()].find((race) => race.phase === 'lobby') ?? null;
}

export function racerList(race: Race): Racer[] {
    return [...race.racers.values()];
}

export function ordinal(place: number): string {
    const tens = place % 100;

    if (tens >= 11 && tens <= 13) {
        return `${place}th`;
    }

    return `${place}${['th', 'st', 'nd', 'rd'][place % 10] ?? 'th'}`;
}

// Back to where the player was before the circuit took them, once they are
// out of the race; the race car goes.
export function sendHome(racer: Racer): void {
    const { player, returnTo, raceCarId } = racer;

    if (!returnTo) {
        return;
    }

    racer.returnTo = null;
    setTimeout(() => {
        if (connected(player) && player.alive && !raceOf(player) && !life.activity(player)) {
            if (player.getVehicle()) {
                player.removeFromVehicle();
            }

            life.teleport(player, returnTo.position, returnTo.heading);
        }
    }, CIRCUIT_RETURN_MS);

    destroyWhenEmpty(raceCarId, CIRCUIT_RETURN_MS);
}

/** Shows the racer's next two checkpoints and points the compass at the first. */
export function routeMarkers(race: Race, racer: Racer): void {
    const cps = race.route.checkpoints;
    const items: Marker[] = [];

    for (let index = racer.cp; index < Math.min(cps.length, racer.cp + 2); index++) {
        const last = index === cps.length - 1;

        items.push({
            id: `cp${index}`,
            position: cps[index],
            label: last ? 'FINISH' : `CHECKPOINT ${index + 1}/${cps.length}`,
            color: index === racer.cp ? (last ? COLORS.green : COLORS.gold) : COLORS.muted,
            beacon: true,
            primary: index === racer.cp,
        });
    }

    markers(racer.player, 'race', items);
    compass(racer.player, 'race', 100, cps[racer.cp] ? { position: cps[racer.cp] } : null);
}

export function leaveRace(race: Race, racer: Racer, message: string | null): void {
    race.racers.delete(racer.player.id);
    sendHome(racer);
    life.end(racer.player, 'race-lobby');
    life.end(racer.player, 'race');
    card(racer.player, 'race', null);
    markers(racer.player, 'race', []);
    compass(racer.player, 'race', 100, null);
    emit(racer.player, 'fr:race:state', { active: false });

    if (message) {
        toast(racer.player, message, { tone: 'bad' });
    }
}

export function closeRace(race: Race): void {
    for (const racer of racerList(race)) {
        leaveRace(race, racer, null);
    }

    races.delete(race.id);
}
