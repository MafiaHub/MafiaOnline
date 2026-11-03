// Calls the city's emergency services answer (the ambulance, the fire
// brigade): where on the streets they come in, the wrecks they leave, how
// they read in the feed and when the next one is due.
import * as maps from './maps';
import * as roads from './roads';
import { name, text, type FeedPart } from './notify';
import { onMissionChange, onStart } from './lifecycle';
import { flatDistance, pick, type Placement, type Point } from './util';
import { spawnVehicle } from './vehicles';

/** Burnt-out cars for a crash, a car fire or a pile-up. */
export const WRECKS: readonly string[] = [
    'fordtfor00.i3d',
    'chev00.i3d',
    'forafo00.i3d',
    'foraca00.i3d',
];

/** "near Salieri's Bar", or "in the city" away from every named place. */
export function where(position: Point): string {
    const nearest = maps
        .places()
        .map((entry) => ({ entry, gap: flatDistance(entry.position, position) }))
        .sort((a, b) => a.gap - b.gap)[0];

    return nearest && nearest.gap < 700 ? `near ${nearest.entry.name}` : 'in the city';
}

/** A road graph point `minimum` to `maximum` metres from `origin` (anywhere without one); any point after 30 misses. */
export function roadPointNear(
    origin: Point | null,
    minimum: number,
    maximum: number,
): roads.RoadSpot {
    const graph = roads.current();

    for (let attempt = 0; attempt < 30; attempt++) {
        const candidate = graph.roadPoint();
        const gap = origin ? flatDistance(candidate.position, origin) : minimum;

        if (gap >= minimum && gap <= maximum) {
            return candidate;
        }
    }

    return graph.roadPoint();
}

/** A spot beside a road, `side` metres off the lane on either side, facing along it. */
export function roadside(
    origin: Point | null,
    minimum: number,
    maximum: number,
    side: number,
): Placement {
    const spot = roadPointNear(origin, minimum, maximum);
    const flip = Math.random() < 0.5 ? -1 : 1;
    const { x, y, z } = spot.position;

    return {
        position: {
            x: x + spot.direction.z * side * flip,
            y,
            z: z - spot.direction.x * side * flip,
        },
        heading: Math.atan2(spot.direction.x, spot.direction.z),
    };
}

/** A wreck standing at ground level `position` (dropped from 0.6 m up), engine off and tagged `fr.wreck`. */
export function spawnWreck(position: Point, heading: number): Vehicle | null {
    return spawnVehicle(
        pick(WRECKS),
        { x: position.x, y: position.y + 0.6, z: position.z },
        heading,
        { engineOff: true, fullTank: false, state: { 'fr.wreck': true } },
    );
}

/** How far the nearest of `players` is from `position`, or null with nobody. */
export function nearestGap(players: readonly Player[], position: Point): number | null {
    return players.length
        ? Math.min(...players.map((player) => flatDistance(player.position, position)))
        : null;
}

/** "Ann, Bob, Cid" for a feed line, the names in `color`. */
export function names(players: readonly Player[], color: string): FeedPart[] {
    return players.flatMap((player, index) => [index ? text(', ') : text(''), name(player, color)]);
}

/**
 * When the next call comes in: `firstMs` after the resource starts or the
 * city changes, then as each call sets it.
 */
export class CallClock {
    private next: number;

    // Ticks may run before the start event (lib/shift.ts timers start as the
    // module loads): no call is due before then either.
    constructor(private readonly firstMs: number) {
        this.next = Date.now() + firstMs;
        onStart(() => this.after(firstMs));
        onMissionChange(() => this.after(this.firstMs));
    }

    /** True when a call is due now; the one after comes `everyMs` later. */
    due(now: number, everyMs: number): boolean {
        if (now < this.next) {
            return false;
        }

        this.next = now + everyMs;

        return true;
    }

    /** The next call `ms` from now. */
    after(ms: number): void {
        this.next = Date.now() + ms;
    }

    /** The next call no later than `ms` from now. */
    within(ms: number): void {
        this.next = Math.min(this.next, Date.now() + ms);
    }
}
